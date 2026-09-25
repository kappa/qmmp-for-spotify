#ifndef ENGINELOGIC_H
#define ENGINELOGIC_H

#include <functional>
#include <QString>
#include <QStringList>
#include "spotifytypes.h"

enum class EngineState { Stopped, Buffering, Playing, Paused };

class EngineSink
{
public:
    virtual ~EngineSink() = default;
    virtual void apiPlay(const QString &uri, qint64 positionMs, quint64 generation) = 0;
    virtual void apiPause() = 0;
    virtual void apiResume() = 0;
    virtual void apiSeek(qint64 positionMs) = 0;
    virtual void apiQueue(const QString &uri) = 0;
    virtual void apiNext() = 0;
    // the follower must not pull qmmp back to this track
    virtual void suppressFollow(const QString &trackId) = 0;
    virtual void reportState(EngineState state) = 0;
    virtual void reportTrack(const QString &path, const SpotifyTrack &track) = 0;
    virtual void reportFinished() = 0;
    virtual void reportError(const QString &message, bool fatal) = 0;
    virtual void reportNotice(const QString &message) = 0;
};

class EngineLogic
{
public:
    using Clock = std::function<qint64()>;

    static constexpr qint64 kStartTimeoutMs = 10000;

    EngineLogic(EngineSink *sink, Clock clock);

    void setSelectedDevice(const QString &deviceId);
    bool enqueue(const QString &path, qint64 offsetMs = 0);
    void play();
    void pause();
    void seek(qint64 positionMs);
    void stop();
    void flushPendingPause();
    void updateState(const PlayerState &state);
    void playFailed(quint64 generation, const QString &message, bool systemic);
    void queueFailed();
    void checkStartTimeout();
    void tick();

    EngineState state() const { return m_state; }
    qint64 elapsed() const;
    QStringList trackIds() const;
    bool isAwaitingStart() const { return m_awaiting; }
    quint64 activeGeneration() const { return m_activeGeneration; }

private:
    struct Entry
    {
        QString path;
        QString id;
        qint64 offsetMs = 0;

        bool isEmpty() const { return id.isEmpty(); }
    };

    void setState(EngineState state);
    void apply(const PlayerState &state);
    void startOnDevice(qint64 positionMs);
    void fail(const QString &message, bool systemic);
    void switchToNext(bool startNow);
    void finish();
    void deviceStopped();
    void skipOnDevice();
    void maybeQueueNext();
    bool nearEnd() const;
    bool inCommandGrace() const;
    bool deviceOnCurrentTrack() const;

    EngineSink *m_sink;
    Clock m_clock;
    QString m_device;
    QString m_deviceName;
    PlayerState m_last;
    Entry m_current;
    Entry m_next;
    bool m_nextQueued = false;
    bool m_awaiting = false;
    bool m_trackReported = false;
    EngineState m_state = EngineState::Stopped;
    SpotifyTrack m_track;
    qint64 m_progressMs = 0;
    qint64 m_progressAt = 0;
    qint64 m_commandAt = -1;
    bool m_pendingPause = false;
    // the pause play() cancelled, restored if that play fails
    bool m_cancelledPause = false;
    // start timeouts since the device last started one of our tracks: the second one stops qmmp
    int m_startTimeouts = 0;
    // the active command is the skip to the already-queued next track
    bool m_skippingToQueued = false;
    QString m_stoppedId;
    quint64 m_generation = 0;
    quint64 m_activeGeneration = 0;
    qint64 m_playSentAt = 0;
};

#endif
