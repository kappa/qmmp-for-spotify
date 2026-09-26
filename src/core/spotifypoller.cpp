#include <QDateTime>
#include <QPointer>
#include "pollpolicy.h"
#include "spotifyapi.h"
#include "spotifypoller.h"

SpotifyPoller::SpotifyPoller(SpotifyApi *api, QObject *parent) : QObject(parent), m_api(api)
{
    m_timer.setSingleShot(true);
    // a coarse timer may fire minutes early on a long rate-limit block, or late enough to
    // miss the poll before a track ends
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &SpotifyPoller::poll);
}

void SpotifyPoller::setEnabled(bool enabled)
{
    m_enabled = enabled;
    m_failures = 0;
    if(enabled)
        pollSoon();
    else
        m_timer.stop();
}

void SpotifyPoller::setEngineActive(bool active, bool awaiting)
{
    m_engineAwaiting = awaiting;
    if(m_engineActive == active)
        return;
    m_engineActive = active;
    if(active)
        pollSoon();
}

void SpotifyPoller::setSelectedDevice(const QString &deviceId)
{
    m_device = deviceId;
    pollSoon();
}

void SpotifyPoller::setVisible(bool visible)
{
    if(m_visible == visible)
        return;
    m_visible = visible;
    if(visible)
        pollOnInteraction();
}

void SpotifyPoller::pollSoon()
{
    if(!m_enabled || m_inFlight)
        return;
    // after a hard fail polling resumes with a fresh retry sequence
    if(isPollHardFail(m_failures))
        m_failures = 0;
    // while rate limited the next poll stays at the end of the block
    if(m_api->isBlocked())
        scheduleNext();
    else
        m_timer.start(300);
}

void SpotifyPoller::pollOnInteraction()
{
    if(interactionPolls(m_lastPollAt, QDateTime::currentMSecsSinceEpoch()))
    {
        pollSoon();
        return;
    }
    // the state is fresh, but a window that became visible may need a sooner poll
    if(!m_enabled || m_inFlight || !m_timer.isActive() || m_api->isBlocked())
        return;
    const int delay = nextDelayMs();
    if(delay != kStopPolling && delay < m_timer.remainingTime())
        m_timer.start(delay);
}

void SpotifyPoller::poll()
{
    if(!m_enabled || m_inFlight)
        return;
    m_inFlight = true;
    m_lastPollAt = QDateTime::currentMSecsSinceEpoch();
    QPointer<SpotifyPoller> self(this);
    m_api->playerState([self](const ApiResult &result, const PlayerState &state) {
        if(!self)
            return;
        self->m_inFlight = false;
        if(result.ok)
        {
            self->m_failures = 0;
            self->m_last = state;
            self->m_hadPlayback = state.hasPlayback;
            self->m_predictedEnd = predictedTrackEndMs(state, QDateTime::currentMSecsSinceEpoch());
            emit self->stateChanged(state);
        }
        else if(!result.rateLimited)
            self->m_failures++;
        self->scheduleNext();
    });
}

void SpotifyPoller::scheduleNext()
{
    if(!m_enabled)
        return;
    if(m_api->isBlocked())
    {
        m_timer.start(rateLimitedPollDelayMs(m_api->blockedUntil(), QDateTime::currentMSecsSinceEpoch()));
        return;
    }
    const int delay = nextDelayMs();
    if(delay != kStopPolling)
    {
        m_timer.start(delay);
        return;
    }
    m_timer.stop();
    if(isPollHardFail(m_failures))
        emit connectionLost();
}

int SpotifyPoller::nextDelayMs() const
{
    PollInputs in;
    in.visible = m_visible;
    in.engineActive = m_engineActive;
    in.engineAwaiting = m_engineAwaiting;
    in.selectedPlaying = m_last.hasPlayback && m_last.isPlaying && m_last.deviceId == m_device;
    in.hadPlayback = m_hadPlayback;
    in.failures = m_failures;
    in.predictedEndMs = m_predictedEnd;
    in.nowMs = QDateTime::currentMSecsSinceEpoch();
    return nextPollDelayMs(in);
}
