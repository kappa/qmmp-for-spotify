#ifndef SPOTIFYAUTH_H
#define SPOTIFYAUTH_H

#include <functional>
#include <QList>
#include <QObject>
#include <QString>

class QOAuth2AuthorizationCodeFlow;
class QOAuthHttpServerReplyHandler;

class SpotifyAuth : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(bool ok)>;

    explicit SpotifyAuth(const QString &tokenFile, QObject *parent = nullptr);

    void setClientId(const QString &clientId);
    void setPort(quint16 port);
    bool hasCredentials() const;
    bool accessTokenValid() const;
    QString accessToken() const;

    void login();
    void logout();
    void withToken(Callback callback);
    void refresh(Callback callback);

    static QString redirectUri(quint16 port);

signals:
    void loggedIn();
    void loggedOut(const QString &reason);
    void loginFailed(const QString &message);

private:
    void onGranted();
    void onFailed(bool sessionInvalid);
    void finishWaiting(bool ok);
    void clearCredentials();
    void saveRefreshToken(const QString &token);
    QString loadRefreshToken() const;

    QOAuth2AuthorizationCodeFlow *m_flow;
    QOAuthHttpServerReplyHandler *m_listener = nullptr;
    QString m_tokenFile;
    QString m_clientId;
    quint16 m_port = 8898;
    bool m_loggingIn = false;
    bool m_refreshing = false;
    QList<Callback> m_waiting;
};

#endif
