#ifndef SPOTIFYCLIENT_H
#define SPOTIFYCLIENT_H

#include <QElapsedTimer>
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
    // one clock for every engine instance, so the times they keep here compare
    qint64 clockMs() const { return m_clock.elapsed(); }
    // when the last track start was sent (clockMs(), -1: never); kept here because qmmp
    // deletes the engine for each non-Spotify item
    qint64 lastStartSentAt() const { return m_lastStartSentAt; }
    void setLastStartSentAt(qint64 at) { m_lastStartSentAt = at; }

    // true only on the first call in this process: the follower attaches to the device's
    // playback once at startup, not again when qmmp re-creates the plugin
    bool takeStartupAttach();

    void notify(const QString &message);

signals:
    void deviceChanged();
    // logged out (in the settings, or the session expired): the engine stops without requests
    void sessionEnded();
    void notice(const QString &message);

private:
    explicit SpotifyClient(QObject *parent);

    SpotifyAuth *m_auth;
    SpotifyApi *m_api;
    SpotifyPoller *m_poller;
    QStringList m_engineTrackIds;
    bool m_engineAwaiting = false;
    QString m_suppressedTrack;
    QElapsedTimer m_clock;
    qint64 m_lastStartSentAt = -1;
    bool m_startupAttachTaken = false;
    QHash<QString, qint64> m_noticeTimes;
    qint64 m_rateLimitNoticeUntil = 0;
};

#endif
