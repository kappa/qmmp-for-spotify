#ifndef SPOTIFYCLIENT_H
#define SPOTIFYCLIENT_H

#include <QHash>
#include <QObject>
#include <QStringList>

class SpotifyApi;
class SpotifyAuth;
class SpotifyPoller;

class SpotifyClient : public QObject
{
    Q_OBJECT
public:
    static SpotifyClient *instance();

    SpotifyAuth *auth() const { return m_auth; }
    SpotifyApi *api() const { return m_api; }
    SpotifyPoller *poller() const { return m_poller; }

    QString clientId() const;
    quint16 port() const;
    void setCredentialsConfig(const QString &clientId, quint16 port);

    QString deviceId() const;
    QString deviceName() const;
    void setDevice(const QString &id, const QString &name);

    void setEngineTracks(const QStringList &ids, bool awaiting);
    QStringList engineTrackIds() const { return m_engineTrackIds; }
    bool isEngineAwaiting() const { return m_engineAwaiting; }
    // the engine paused this track when qmmp left Spotify; the follower must not return to it
    void setSuppressedTrack(const QString &id) { m_suppressedTrack = id; }
    QString suppressedTrack() const { return m_suppressedTrack; }

    void notify(const QString &message);

signals:
    void deviceChanged();
    void notice(const QString &message);

private:
    explicit SpotifyClient(QObject *parent);

    SpotifyAuth *m_auth;
    SpotifyApi *m_api;
    SpotifyPoller *m_poller;
    QStringList m_engineTrackIds;
    bool m_engineAwaiting = false;
    QString m_suppressedTrack;
    QHash<QString, qint64> m_noticeTimes;
};

#endif
