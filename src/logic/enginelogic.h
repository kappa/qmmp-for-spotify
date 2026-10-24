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
    // an empty message fails quietly (an unavailable track skipped, a logout)
    virtual void reportError(const QString &message, bool fatal) = 0;
    virtual void reportNotice(const QString &message) = 0;
    // a coalesced track start is due in delayMs: call flushStart() then
    virtual void scheduleStartFlush(qint64 delayMs) = 0;
    // the last start request (play, skip to the queued track) is superseded: if it has not
    // been answered, it must never reach the device (a request stuck offline)
    virtual void cancelStartRequest() = 0;
    // a poll in delayMs, also after polling stopped for "nothing active"
    virtual void pollAfter(qint64 delayMs) = 0;
};

class EngineLogic
{
public:
    using Clock = std::function<qint64()>;

    static constexpr qint64 kStartTimeoutMs = 10000;
    // seeks are sent this long after the last one of a burst (a held key, a scrolled slider)
    static constexpr qint64 kSeekDebounceMs = 300;
    // track starts (play, skip to the queued track) go out at most once per this long; the
    // latest of a burst (a held Next key) follows when it ends
    static constexpr qint64 kStartGapMs = 1000;

    EngineLogic(EngineSink *sink, Clock clock);

    void setSelectedDevice(const QString &deviceId);
    bool enqueue(const QString &path, qint64 offsetMs = 0);
    void play();
    void pause();
    void seek(qint64 positionMs);
    // sends the pending seek once kSeekDebounceMs passed since the last seek()
    void flushSeek();
    // sends the coalesced track start once kStartGapMs passed since the last one was sent
    void flushStart();
    void stop();
    void flushPendingPause();
    void updateState(const PlayerState &state);
    void playFailed(quint64 generation, const QString &message, bool systemic);
    void queueFailed();
    void checkStartTimeout();
    void tick();
    // logged out: qmmp stops without a request and without an error message
    void sessionEnded();

    EngineState state() const { return m_state; }
    qint64 elapsed() const;
    QStringList trackIds() const;
    bool isAwaitingStart() const { return m_awaiting; }
    quint64 activeGeneration() const { return m_activeGeneration; }

private:
    // the queue request for the next track is sent at most once: a failed one is not resent,
    // the track-end fallback starts the track instead
    enum class Queue { NotSent, Sent, Failed };

    struct Entry
    {
        QString path;
        QString id;
        qint64 offsetMs = 0;
        // not playable in the user's country: failed at once, without a request
        bool unavailable = false;

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
    void sendStart(qint64 positionMs);
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
    Queue m_nextQueue = Queue::NotSent;
    bool m_awaiting = false;
    bool m_trackReported = false;
    EngineState m_state = EngineState::Stopped;
    SpotifyTrack m_track;
    qint64 m_progressMs = 0;
    qint64 m_progressAt = 0;
    qint64 m_commandAt = -1;
    // the last seek() of a burst, sent by flushSeek(); -1: none
    qint64 m_pendingSeekMs = -1;
    qint64 m_seekAt = 0;
    // the coalesced track start (play from m_pendingStartMs, or the skip to the queued track)
    bool m_startPending = false;
    qint64 m_pendingStartMs = 0;
    qint64 m_startSentAt = -1;
    bool m_pendingPause = false;
    // the pause play() cancelled, restored if that play fails
    bool m_cancelledPause = false;
    // start timeouts since the device last started one of our tracks: the second one stops qmmp
    int m_startTimeouts = 0;
    // the active command is the skip to the already-queued next track
    bool m_skippingToQueued = false;
    QString m_stoppedId;
    // qmmp stopped while a start was unconfirmed: m_last may predate it, so the next play()
    // must not adopt the track m_last shows (Next, then Previous before a poll)
    bool m_stoppedAwaiting = false;
    quint64 m_generation = 0;
    quint64 m_activeGeneration = 0;
    qint64 m_playSentAt = 0;
};

#endif
