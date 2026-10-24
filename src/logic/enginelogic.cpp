#include "enginelogic.h"
#include "spotifylink.h"

using namespace Qt::StringLiterals;

namespace {

// polls that arrive this soon after a pause/resume/seek may predate it
const qint64 kCommandGraceMs = 2500;
// how close to the end a track must be for "stopped" or "moved on" to mean "finished"
const qint64 kEndWindowMs = 3000;
// the device rewinds to 0 when a track ends with nothing queued
const qint64 kRewoundMs = 1500;
// Spotify queue entries cannot be removed, so hold the next track and queue it only this
// close to the end
const qint64 kQueueLeadMs = 1500;

QString trackUri(const QString &id)
{
    return u"spotify:track:"_s + id;
}

}

EngineLogic::EngineLogic(EngineSink *sink, Clock clock) : m_sink(sink), m_clock(std::move(clock))
{}

void EngineLogic::setSelectedDevice(const QString &deviceId)
{
    if(deviceId != m_device)
        m_deviceName.clear();
    m_device = deviceId;
}

bool EngineLogic::enqueue(const QString &path, qint64 offsetMs)
{
    const SpotifyLink link = SpotifyLink::parse(path);
    if(link.kind != SpotifyLink::Track)
        return false;

    const Entry entry { path, link.id, qMax<qint64>(0, offsetMs), link.unavailable };
    if(m_current.isEmpty())
    {
        m_current = entry;
        return true;
    }
    m_next = entry;
    m_nextQueue = Queue::NotSent;
    return true;
}

void EngineLogic::play()
{
    m_sink->cancelStartRequest();
    // a play in the same call stack as stop() (restart, Next) supersedes the pause stop() deferred
    m_cancelledPause = m_pendingPause;
    m_pendingPause = false;
    // a seek or a start not yet sent for the previous track is moot
    m_pendingSeekMs = -1;
    m_startPending = false;
    const bool restart = !m_current.isEmpty() && m_current.id == m_stoppedId;
    m_stoppedId.clear();
    const bool lastIsStale = m_stoppedAwaiting;
    m_stoppedAwaiting = false;
    if(m_current.isEmpty())
        return;
    if(m_device.isEmpty())
    {
        m_current = Entry();
        m_next = Entry();
        m_sink->reportError(u"Spotify: no device selected. Pick one in Tools → Spotify → Device…"_s, true);
        return;
    }
    m_trackReported = false;
    m_commandAt = -1;
    m_activeGeneration = 0;
    if(!restart && !lastIsStale && m_current.offsetMs == 0 && deviceOnCurrentTrack() && m_last.isPlaying)
    {
        apply(m_last);
        return;
    }
    m_progressMs = m_current.offsetMs;
    m_progressAt = m_clock();
    setState(EngineState::Buffering);
    startOnDevice(m_current.offsetMs);
}

void EngineLogic::pause()
{
    if(m_state == EngineState::Playing)
    {
        m_progressMs = elapsed();
        m_progressAt = m_clock();
        m_commandAt = m_clock();
        m_sink->cancelStartRequest();
        m_sink->apiPause();
        setState(EngineState::Paused);
    }
    else if(m_state == EngineState::Paused)
    {
        m_progressAt = m_clock();
        m_commandAt = m_clock();
        m_sink->apiResume();
        setState(EngineState::Playing);
    }
}

void EngineLogic::seek(qint64 positionMs)
{
    if(m_state != EngineState::Playing && m_state != EngineState::Paused)
        return;
    m_progressMs = positionMs;
    m_progressAt = m_clock();
    m_commandAt = m_clock();
    m_pendingSeekMs = positionMs;
    m_seekAt = m_clock();
}

void EngineLogic::flushSeek()
{
    if(m_pendingSeekMs < 0 || m_clock() - m_seekAt < kSeekDebounceMs)
        return;
    const qint64 positionMs = m_pendingSeekMs;
    m_pendingSeekMs = -1;
    // polls from before the seek reached the device show the old position
    m_commandAt = m_clock();
    m_sink->apiSeek(positionMs);
}

void EngineLogic::flushStart()
{
    if(!m_startPending)
        return;
    const qint64 sentAt = m_sink->lastStartSentAt();
    const qint64 wait = sentAt < 0 ? 0 : sentAt + kStartGapMs - m_clock();
    if(wait > 0)
    {
        // early: the pending start must not be lost
        m_sink->scheduleStartFlush(wait);
        return;
    }
    m_startPending = false;
    m_sink->setLastStartSentAt(m_clock());
    // the start timeout counts from the request actually sent
    m_playSentAt = m_clock();
    if(m_skippingToQueued)
        m_sink->apiNext();
    else
        m_sink->apiPlay(trackUri(m_current.id), m_pendingStartMs, m_activeGeneration);
}

void EngineLogic::stop()
{
    // SoundCore::play() stops again after MediaPlayer::stop(); that second stop must not wipe
    // the first one's restart candidate and pending pause
    if(m_current.isEmpty())
        return;
    m_sink->cancelStartRequest();
    // m_state is our belief; m_last may predate a resume we just sent. A paused device is left alone (403).
    const bool active = m_awaiting || (m_state == EngineState::Playing && deviceOnCurrentTrack());
    m_pendingPause = active;
    m_stoppedId = m_current.id;
    m_stoppedAwaiting = m_awaiting;
    m_activeGeneration = 0;
    m_current = Entry();
    m_pendingSeekMs = -1;
    m_startPending = false;
    m_next = Entry();
    m_nextQueue = Queue::NotSent;
    m_awaiting = false;
    m_trackReported = false;
    m_track = SpotifyTrack();
    m_commandAt = -1;
    setState(EngineState::Stopped);
}

void EngineLogic::updateState(const PlayerState &state)
{
    m_last = state;
    const bool onDevice = state.hasPlayback && state.deviceId == m_device;
    if(onDevice && !state.deviceName.isEmpty())
        m_deviceName = state.deviceName;
    if(m_current.isEmpty() || m_state == EngineState::Stopped)
        return;

    if(onDevice && state.track.id == m_current.id)
    {
        if(!m_awaiting && m_state == EngineState::Playing && !state.isPlaying && state.progressMs < kRewoundMs && nearEnd())
            finish();
        // the poll may predate a held play: only polls after it is sent can confirm it
        else if(!m_startPending || m_skippingToQueued)
            apply(state);
    }
    else if(onDevice && !m_next.isEmpty() && state.track.id == m_next.id)
    {
        switchToNext(false);
        apply(state);
    }
    else if(!m_awaiting && m_state == EngineState::Playing && nearEnd())
    {
        finish();
    }
    else if(!onDevice && !m_awaiting && m_state == EngineState::Playing)
    {
        if(!inCommandGrace())
            deviceStopped();
        // polling stops on "nothing active": look again when the poll can be trusted
        else if(!state.hasPlayback)
            m_sink->pollAfter(m_commandAt + kCommandGraceMs - m_clock());
    }
    // a poll may arrive inside the queue-lead window
    maybeQueueNext();
}

void EngineLogic::flushPendingPause()
{
    m_stoppedId.clear();
    if(!m_pendingPause)
        return;
    m_pendingPause = false;
    m_sink->cancelStartRequest();
    m_sink->apiPause();
}

void EngineLogic::playFailed(quint64 generation, const QString &message, bool systemic)
{
    if(generation == 0 || generation != m_activeGeneration)
        return;
    // a failed skip to the queued track is about our queue mechanism, not the track
    fail(message, systemic || m_skippingToQueued);
}

// the queue call failed (or its result never reached us): the entry never reached the
// device, so the next-track-ends fallback must send apiPlay instead of skipping to it; the
// queue call is not sent again, or a lasting failure would repeat it on every tick
void EngineLogic::queueFailed()
{
    if(m_nextQueue == Queue::Sent)
        m_nextQueue = Queue::Failed;
}

void EngineLogic::checkStartTimeout()
{
    if(!m_awaiting || m_activeGeneration == 0 || m_clock() - m_playSentAt < kStartTimeoutMs)
        return;
    // one silent track is skipped; a device that starts none of two in a row is the problem
    const bool systemic = ++m_startTimeouts >= 2;
    if(systemic)
        m_startTimeouts = 0;
    fail(u"Spotify: the device did not start playing"_s, systemic);
}

// the device keeps playing: a pause could not be sent without a token anyway
void EngineLogic::sessionEnded()
{
    m_pendingPause = false;
    m_cancelledPause = false;
    m_stoppedId.clear();
    if(!m_current.isEmpty())
        fail(QString(), true);
}

void EngineLogic::tick()
{
    maybeQueueNext();
}

qint64 EngineLogic::elapsed() const
{
    qint64 value = m_progressMs;
    if(m_state == EngineState::Playing && !m_awaiting)
        value += m_clock() - m_progressAt;
    if(m_track.durationMs > 0)
        value = qMin(value, m_track.durationMs);
    return qMax<qint64>(0, value);
}

QStringList EngineLogic::trackIds() const
{
    QStringList ids;
    if(!m_current.isEmpty())
        ids << m_current.id;
    if(!m_next.isEmpty())
        ids << m_next.id;
    return ids;
}

void EngineLogic::setState(EngineState state)
{
    if(m_state == state)
        return;
    m_state = state;
    m_sink->reportState(state);
}

void EngineLogic::apply(const PlayerState &state)
{
    m_awaiting = false;
    // the device reached the queued track by itself: a held skip would jump past it
    m_startPending = false;
    m_cancelledPause = false;
    m_startTimeouts = 0;
    m_track = state.track;
    if(!inCommandGrace())
    {
        m_progressMs = state.progressMs;
        m_progressAt = m_clock();
        setState(state.isPlaying ? EngineState::Playing : EngineState::Paused);
    }
    if(!m_trackReported)
    {
        m_trackReported = true;
        m_sink->reportTrack(m_current.path, state.track);
    }
}

void EngineLogic::startOnDevice(qint64 positionMs)
{
    // qmmp skips it like a track Spotify refused, without the message
    if(m_current.unavailable)
    {
        fail(QString(), false);
        return;
    }
    m_awaiting = true;
    m_skippingToQueued = false;
    m_activeGeneration = ++m_generation;
    sendStart(positionMs);
}

// the next track is already sitting in Spotify's queue: skip to it instead of apiPlay,
// which would start a second copy and leave the queued entry to play again after it
void EngineLogic::skipOnDevice()
{
    m_awaiting = true;
    m_skippingToQueued = true;
    m_activeGeneration = ++m_generation;
    sendStart(0);
}

// the first start of a burst goes out at once; later ones within kStartGapMs replace each other
// and the latest follows when the gap ends (a held Next key sends about one play per second)
void EngineLogic::sendStart(qint64 positionMs)
{
    m_pendingStartMs = positionMs;
    m_startPending = true;
    flushStart();
}

// a systemic failure (FatalError) stops qmmp; a track-specific one (NormalError) makes qmmp
// skip to the next track, and qmmp itself stops after a whole playlist of failures. The
// pause the failed play cancelled comes back: the next play cancels it again, qmmp's stop
// sends it, so the previous track does not keep playing on the device.
void EngineLogic::fail(const QString &message, bool systemic)
{
    m_sink->cancelStartRequest();
    m_pendingPause = m_cancelledPause;
    m_cancelledPause = false;
    m_current = Entry();
    m_pendingSeekMs = -1;
    m_startPending = false;
    m_next = Entry();
    m_nextQueue = Queue::NotSent;
    m_awaiting = false;
    m_trackReported = false;
    m_activeGeneration = 0;
    m_state = EngineState::Stopped;
    m_sink->reportError(message, systemic);
}

void EngineLogic::switchToNext(bool startNow)
{
    m_sink->reportFinished();
    m_sink->reportState(EngineState::Stopped);
    m_current = m_next;
    m_pendingSeekMs = -1;
    m_startPending = false;
    m_next = Entry();
    m_nextQueue = Queue::NotSent;
    m_trackReported = false;
    m_track = SpotifyTrack();
    m_progressMs = 0;
    m_progressAt = m_clock();
    m_commandAt = -1;
    m_state = EngineState::Buffering;
    m_sink->reportState(EngineState::Buffering);
    if(startNow)
        startOnDevice(0);
}

void EngineLogic::finish()
{
    if(!m_next.isEmpty())
    {
        // an unavailable next track was not queued: the device stops or moves on to Autoplay,
        // and if qmmp finds nothing playable after it, its stop pauses the device
        if(m_next.unavailable)
            m_cancelledPause = m_last.hasPlayback && m_last.deviceId == m_device;
        const bool queued = m_nextQueue == Queue::Sent;
        switchToNext(!queued);
        if(queued)
            skipOnDevice();
        return;
    }
    // qmmp moves on to a non-Spotify item or its playlist ended: the device must not keep
    // playing (Spotify's Autoplay may already have started a recommendation). The pause is
    // deferred like stop()'s: if qmmp plays a Spotify track next, that play() cancels it.
    if(m_last.hasPlayback && m_last.deviceId == m_device)
    {
        m_pendingPause = true;
        m_sink->suppressFollow(m_last.track.id);
    }
    m_current = Entry();
    m_pendingSeekMs = -1;
    m_startPending = false;
    m_awaiting = false;
    m_trackReported = false;
    m_sink->reportFinished();
    setState(EngineState::Stopped);
}

// the device was closed or playback moved elsewhere: stop the clock and say so once;
// a later poll of our track playing on the device resumes following through apply()
void EngineLogic::deviceStopped()
{
    m_progressMs = elapsed();
    m_progressAt = m_clock();
    setState(EngineState::Paused);
    m_sink->reportNotice(u"Spotify: playback stopped on %1"_s.arg(m_deviceName.isEmpty() ? u"the device"_s : m_deviceName));
}

void EngineLogic::maybeQueueNext()
{
    if(m_state != EngineState::Playing || m_awaiting || m_next.isEmpty() || m_next.unavailable
            || m_nextQueue != Queue::NotSent)
        return;
    if(m_track.durationMs <= 0 || m_track.durationMs - elapsed() > kQueueLeadMs)
        return;
    m_nextQueue = Queue::Sent;
    m_sink->apiQueue(trackUri(m_next.id));
}

bool EngineLogic::nearEnd() const
{
    return m_track.durationMs > 0 && elapsed() >= m_track.durationMs - kEndWindowMs;
}

bool EngineLogic::inCommandGrace() const
{
    return m_commandAt >= 0 && m_clock() - m_commandAt < kCommandGraceMs;
}

bool EngineLogic::deviceOnCurrentTrack() const
{
    return m_last.hasPlayback && m_last.deviceId == m_device && m_last.track.id == m_current.id;
}
