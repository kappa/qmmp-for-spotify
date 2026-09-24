#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QOAuthOobReplyHandler>
#include <QSaveFile>
#include "spotifyauth.h"

using namespace Qt::StringLiterals;

SpotifyAuth::SpotifyAuth(const QString &tokenFile, QObject *parent) : QObject(parent), m_tokenFile(tokenFile)
{
    m_flow = new QOAuth2AuthorizationCodeFlow(this);
    m_flow->setAuthorizationUrl(QUrl(u"https://accounts.spotify.com/authorize"_s));
    m_flow->setTokenUrl(QUrl(u"https://accounts.spotify.com/api/token"_s));
    m_flow->setRequestedScopeTokens({ "user-read-playback-state", "user-modify-playback-state",
                                      "user-read-currently-playing", "playlist-read-private",
                                      "playlist-read-collaborative", "user-library-read" });
    m_flow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
    m_flow->setAutoRefresh(false);
    m_flow->setReplyHandler(new QOAuthOobReplyHandler(this));
    m_flow->setRefreshToken(loadRefreshToken());

    connect(m_flow, &QAbstractOAuth::authorizeWithBrowser, this, [](const QUrl &url) { QDesktopServices::openUrl(url); });
    connect(m_flow, &QAbstractOAuth2::refreshTokenChanged, this, [this](const QString &token) {
        if(!token.isEmpty())
            saveRefreshToken(token);
    });
    connect(m_flow, &QAbstractOAuth::tokenChanged, this, [this](const QString &token) {
        if(m_refreshing && !token.isEmpty())
            finishWaiting(true);
    });
    connect(m_flow, &QAbstractOAuth::granted, this, &SpotifyAuth::onGranted);
    connect(m_flow, &QAbstractOAuth::requestFailed, this, [this](QAbstractOAuth::Error error) {
        onFailed(error == QAbstractOAuth::Error::ServerError);
    });
    connect(m_flow, &QAbstractOAuth2::serverReportedErrorOccurred, this,
            [this](const QString &error, const QString &, const QUrl &) {
        onFailed(error == "invalid_grant"_L1 || error == "invalid_client"_L1);
    });
}

void SpotifyAuth::setClientId(const QString &clientId)
{
    m_clientId = clientId;
    m_flow->setClientIdentifier(clientId);
}

void SpotifyAuth::setPort(quint16 port)
{
    m_port = port;
}

bool SpotifyAuth::hasCredentials() const
{
    return !m_flow->refreshToken().isEmpty();
}

bool SpotifyAuth::accessTokenValid() const
{
    const QDateTime expiration = m_flow->expirationAt();
    return !m_flow->token().isEmpty() && expiration.isValid()
            && QDateTime::currentDateTime().secsTo(expiration) > 60;
}

QString SpotifyAuth::accessToken() const
{
    return m_flow->token();
}

QString SpotifyAuth::redirectUri(quint16 port)
{
    return u"http://127.0.0.1:%1/callback"_s.arg(port);
}

void SpotifyAuth::login()
{
    if(m_clientId.isEmpty())
    {
        emit loginFailed(u"Spotify: set the client ID first"_s);
        return;
    }
    if(m_listener)
    {
        m_listener->close();
        m_listener->deleteLater();
        m_listener = nullptr;
    }
    auto *listener = new QOAuthHttpServerReplyHandler(QHostAddress::LocalHost, m_port, this);
    if(!listener->isListening())
    {
        delete listener;
        emit loginFailed(u"Spotify: port %1 is busy, choose another one in the settings"_s.arg(m_port));
        return;
    }
    listener->setCallbackHost(u"127.0.0.1"_s);
    listener->setCallbackPath(u"/callback"_s);
    listener->setCallbackText(u"Spotify login complete. You can close this tab."_s);
    qInfo("Spotify redirect URI: %s", qPrintable(listener->callback()));
    m_flow->setReplyHandler(listener);
    m_listener = listener;
    m_loggingIn = true;
    m_flow->grant();
}

void SpotifyAuth::logout()
{
    clearCredentials();
    emit loggedOut(u"Logged out of Spotify"_s);
}

void SpotifyAuth::withToken(Callback callback)
{
    if(accessTokenValid())
    {
        callback(true);
        return;
    }
    refresh(std::move(callback));
}

void SpotifyAuth::refresh(Callback callback)
{
    if(!hasCredentials() || m_clientId.isEmpty())
    {
        callback(false);
        return;
    }
    m_waiting << std::move(callback);
    if(m_refreshing)
        return;
    m_refreshing = true;
    m_flow->refreshTokens();
}

void SpotifyAuth::onGranted()
{
    if(m_listener)
        m_listener->close();
    const bool wasLoggingIn = m_loggingIn;
    m_loggingIn = false;
    finishWaiting(true);
    if(wasLoggingIn)
        emit loggedIn();
}

void SpotifyAuth::onFailed(bool sessionInvalid)
{
    if(m_listener)
        m_listener->close();
    const bool wasLoggingIn = m_loggingIn;
    m_loggingIn = false;
    finishWaiting(false);
    if(wasLoggingIn)
        emit loginFailed(u"Spotify login failed"_s);
    else if(sessionInvalid && hasCredentials())
    {
        clearCredentials();
        emit loggedOut(u"Spotify session expired, please log in again"_s);
    }
}

void SpotifyAuth::finishWaiting(bool ok)
{
    m_refreshing = false;
    const QList<Callback> waiting = std::move(m_waiting);
    m_waiting.clear();
    for(const Callback &callback : waiting)
        callback(ok);
}

void SpotifyAuth::clearCredentials()
{
    m_flow->setToken(QString());
    m_flow->setRefreshToken(QString());
    QFile::remove(m_tokenFile);
}

void SpotifyAuth::saveRefreshToken(const QString &token)
{
    QDir().mkpath(QFileInfo(m_tokenFile).absolutePath());
    QSaveFile file(m_tokenFile);
    if(!file.open(QIODevice::WriteOnly))
    {
        qWarning("Spotify: cannot write %s", qPrintable(m_tokenFile));
        return;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(QJsonObject { { u"refresh_token"_s, token } }).toJson());
    file.commit();
}

QString SpotifyAuth::loadRefreshToken() const
{
    QFile file(m_tokenFile);
    if(!file.open(QIODevice::ReadOnly))
        return QString();
    return QJsonDocument::fromJson(file.readAll()).object().value("refresh_token"_L1).toString();
}
