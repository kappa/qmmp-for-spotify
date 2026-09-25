#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QSettings>
#include "followerlogic.h"
#include "spotifyapi.h"
#include "spotifyauth.h"
#include "spotifyclient.h"
#include "spotifypoller.h"

using namespace Qt::StringLiterals;

namespace {

const qint64 kNoticeRepeatMs = 30000;

QString tokenFile()
{
    return QFileInfo(QSettings().fileName()).absolutePath() + u"/spotify/token.json"_s;
}

}

SpotifyClient *SpotifyClient::instance()
{
    static SpotifyClient *client = nullptr;
    if(!client)
        client = new SpotifyClient(qApp);
    return client;
}

SpotifyClient::SpotifyClient(QObject *parent) : QObject(parent)
{
    m_auth = new SpotifyAuth(tokenFile(), this);
    m_auth->setClientId(clientId());
    m_auth->setPort(port());
    m_api = new SpotifyApi(m_auth, this);
    m_poller = new SpotifyPoller(m_api, this);
    m_poller->setSelectedDevice(deviceId());
    m_poller->setEnabled(m_auth->hasCredentials());

    connect(m_poller, &SpotifyPoller::stateChanged, this, [this](const PlayerState &state) {
        if(endsSuppression(state, deviceId(), m_suppressedTrack))
            m_suppressedTrack.clear();
    });
    connect(m_auth, &SpotifyAuth::loggedIn, this, [this] {
        m_poller->setEnabled(true);
        notify(u"Logged in to Spotify"_s);
    });
    connect(m_auth, &SpotifyAuth::loggedOut, this, [this](const QString &reason) {
        m_poller->setEnabled(false);
        notify(reason);
    });
    connect(m_auth, &SpotifyAuth::loginFailed, this, [this](const QString &message) { notify(message); });
}

QString SpotifyClient::clientId() const
{
    return QSettings().value(u"Spotify/client_id"_s).toString();
}

quint16 SpotifyClient::port() const
{
    return quint16(QSettings().value(u"Spotify/port"_s, 8898).toUInt());
}

void SpotifyClient::setCredentialsConfig(const QString &clientId, quint16 port)
{
    QSettings settings;
    settings.setValue(u"Spotify/client_id"_s, clientId);
    settings.setValue(u"Spotify/port"_s, port);
    m_auth->setClientId(clientId);
    m_auth->setPort(port);
}

QString SpotifyClient::deviceId() const
{
    return QSettings().value(u"Spotify/device_id"_s).toString();
}

QString SpotifyClient::deviceName() const
{
    return QSettings().value(u"Spotify/device_name"_s).toString();
}

void SpotifyClient::setDevice(const QString &id, const QString &name)
{
    QSettings settings;
    settings.setValue(u"Spotify/device_id"_s, id);
    settings.setValue(u"Spotify/device_name"_s, name);
    m_poller->setSelectedDevice(id);
    emit deviceChanged();
}

void SpotifyClient::setEngineTracks(const QStringList &ids, bool awaiting)
{
    m_engineTrackIds = ids;
    m_engineAwaiting = awaiting;
    m_poller->setEngineActive(!ids.isEmpty());
}

bool SpotifyClient::takeStartupAttach()
{
    const bool first = !m_startupAttachTaken;
    m_startupAttachTaken = true;
    return first;
}

void SpotifyClient::notify(const QString &message)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if(now - m_noticeTimes.value(message, -kNoticeRepeatMs) < kNoticeRepeatMs)
        return;
    m_noticeTimes.insert(message, now);
    qWarning("%s", qPrintable(message));
    emit notice(message);
}
