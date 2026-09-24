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

QString trackUri(const QString &id)
{
    return u"spotify:track:"_s + id;
}

}

EngineLogic::EngineLogic(EngineSink *sink, Clock clock) : m_sink(sink), m_clock(std::move(clock))
{}

void EngineLogic::setSelectedDevice(const QString &deviceId)
{
    m_device = deviceId;
}

bool EngineLogic::enqueue(const QString &path, qint64 offsetMs)
{
    const SpotifyLink link = SpotifyLink::parse(path);
    if(link.kind != SpotifyLink::Track)
        return false;

    const Entry entry { path, link.id, qMax<qint64>(0, offsetMs) };
    if(m_current.isEmpty())
    {
        m_current = entry;
        return true;
    }
    m_next = entry;
    m_nextQueued = false;
    if(m_state == EngineState::Playing || m_state == EngineState::Paused)
    {
        m_sink->apiQueue(trackUri(m_next.id));
        m_nextQueued = true;
    }
    return true;
}

void EngineLogic::play()
{
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
    if(m_current.offsetMs == 0 && deviceOnCurrentTrack() && m_last.isPlaying)
    {
        apply(m_last);
        return;
    }
    m_awaiting = true;
    m_progressMs = m_current.offsetMs;
    m_progressAt = m_clock();
    setState(EngineState::Buffering);
    m_sink->apiPlay(trackUri(m_current.id), m_current.offsetMs);
}

void EngineLogic::pause()
{
    if(m_state == EngineState::Playing)
    {
        m_progressMs = elapsed();
        m_progressAt = m_clock();
        m_commandAt = m_clock();
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
    m_sink->apiSeek(positionMs);
}

void EngineLogic::stop()
{
    if(!m_current.isEmpty() && (m_awaiting || (m_state == EngineState::Playing && deviceOnCurrentTrack() && m_last.isPlaying)))
        m_sink->apiPause();
    m_current = Entry();
    m_next = Entry();
    m_nextQueued = false;
    m_awaiting = false;
    m_trackReported = false;
    m_track = SpotifyTrack();
    setState(EngineState::Stopped);
}

void EngineLogic::updateState(const PlayerState &state)
{
    m_last = state;
    if(m_current.isEmpty() || m_state == EngineState::Stopped)
        return;

    const bool onDevice = state.hasPlayback && state.deviceId == m_device;
    if(onDevice && state.track.id == m_current.id)
    {
        if(!m_awaiting && m_state == EngineState::Playing && !state.isPlaying && state.progressMs < kRewoundMs && nearEnd())
            finish();
        else
            apply(state);
        return;
    }
    if(onDevice && !m_next.isEmpty() && state.track.id == m_next.id)
    {
        switchToNext(false);
        apply(state);
        return;
    }
    if(!m_awaiting && m_state == EngineState::Playing && nearEnd())
        finish();
}

void EngineLogic::playFailed(const QString &message, bool fatal)
{
    m_current = Entry();
    m_next = Entry();
    m_nextQueued = false;
    m_awaiting = false;
    m_trackReported = false;
    m_state = EngineState::Stopped;
    m_sink->reportError(message, fatal);
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
    if(!m_next.isEmpty() && !m_nextQueued)
    {
        m_sink->apiQueue(trackUri(m_next.id));
        m_nextQueued = true;
    }
}

void EngineLogic::switchToNext(bool startOnDevice)
{
    m_sink->reportFinished();
    m_sink->reportState(EngineState::Stopped);
    m_current = m_next;
    m_next = Entry();
    m_nextQueued = false;
    m_trackReported = false;
    m_track = SpotifyTrack();
    m_progressMs = 0;
    m_progressAt = m_clock();
    m_commandAt = -1;
    m_state = EngineState::Buffering;
    m_sink->reportState(EngineState::Buffering);
    if(startOnDevice)
    {
        m_awaiting = true;
        m_sink->apiPlay(trackUri(m_current.id), 0);
    }
}

void EngineLogic::finish()
{
    if(!m_next.isEmpty())
    {
        switchToNext(true);
        return;
    }
    m_current = Entry();
    m_awaiting = false;
    m_trackReported = false;
    m_sink->reportFinished();
    setState(EngineState::Stopped);
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
