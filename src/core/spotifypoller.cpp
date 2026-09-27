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
    m_pollAgain = false;
    if(enabled)
        pollSoon();
    else
        m_timer.stop();
}

void SpotifyPoller::setEngineActive(bool active, bool awaiting)
{
    if(awaiting && !m_engineAwaiting)
        m_awaitingSince = QDateTime::currentMSecsSinceEpoch();
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
    userCommand();
    pollSoon();
}

void SpotifyPoller::setFocused(bool focused)
{
    if(m_focused == focused)
        return;
    m_focused = focused;
    if(focused)
        pollOnInteraction();
}

void SpotifyPoller::pollSoon()
{
    // automatic requests (queue replies, track ends) do not restart a lost connection
    if(!m_enabled || isPollHardFail(m_failures))
        return;
    if(m_inFlight)
    {
        // the reply in flight may predate the command
        m_pollAgain = true;
        return;
    }
    // while rate limited the next poll stays at the end of the block
    if(m_api->isBlocked())
    {
        scheduleNext();
        return;
    }
    // failing commands (a queue call while offline) must not shorten the backoff
    const int delay = pollSoonDelayMs(m_failures, m_timer.isActive() ? m_timer.remainingTime() : -1, m_lastPollAt,
                                      QDateTime::currentMSecsSinceEpoch());
    if(delay != kKeepTimer)
        arm(delay);
}

void SpotifyPoller::userCommand()
{
    // a fresh retry sequence
    if(isPollHardFail(m_failures))
        m_failures = 0;
    // the backoff holds automatic polls back, not the user's
    else if(m_failures > 0)
        pollEarly();
}

void SpotifyPoller::pollOnInteraction()
{
    if(isPollHardFail(m_failures))
        m_failures = 0;
    pollEarly();
}

void SpotifyPoller::pollEarly()
{
    if(!m_enabled)
        return;
    // the reply in flight is fresh
    if(m_inFlight)
        return;
    if(m_api->isBlocked())
    {
        if(!m_timer.isActive())
            scheduleNext();
        return;
    }
    const PollPlan next = plan();
    const int delay = interactionDelayMs(m_lastPollAt, QDateTime::currentMSecsSinceEpoch(),
                                         m_timer.isActive() ? m_timer.remainingTime() : -1, next.delayMs);
    // brought forward to the policy's poll, which may be a track-end one
    if(delay != kKeepTimer)
        arm(delay, delay == next.delayMs ? next.kind : PollKind::Regular);
}

void SpotifyPoller::poll()
{
    if(!m_enabled || m_inFlight)
        return;
    m_inFlight = true;
    m_lastPollAt = QDateTime::currentMSecsSinceEpoch();
    if(m_armedKind == PollKind::BeforeEnd)
        m_beforeEndPolled = true;
    else if(m_armedKind == PollKind::AfterEnd)
        m_afterEndPolled = true;
    m_armedKind = PollKind::Regular;
    QPointer<SpotifyPoller> self(this);
    m_api->playerState([self](const ApiResult &result, const PlayerState &state) {
        if(!self)
            return;
        self->m_inFlight = false;
        // gaps between polls count from the reply: a replayed poll must not follow a slow one
        // within moments
        self->m_lastPollAt = QDateTime::currentMSecsSinceEpoch();
        if(result.ok)
        {
            self->m_failures = 0;
            self->m_last = state;
            self->m_hadPlayback = state.hasPlayback;
            self->m_predictedEnd = predictedTrackEndMs(state, QDateTime::currentMSecsSinceEpoch());
            if(startsTrackEndCycle(self->m_endTrack, self->m_endProgress, state))
            {
                self->m_beforeEndPolled = false;
                self->m_afterEndPolled = false;
            }
            if(state.hasPlayback)
            {
                self->m_endTrack = state.track.id;
                self->m_endProgress = state.progressMs;
            }
            emit self->stateChanged(state);
        }
        // held back locally: nothing is wrong with the connection
        else if(!result.rateLimited && !result.budgetExceeded)
            self->m_failures++;
        self->scheduleNext();
        if(self->m_pollAgain)
        {
            self->m_pollAgain = false;
            self->pollSoon();
        }
    });
}

void SpotifyPoller::scheduleNext()
{
    if(!m_enabled)
        return;
    if(m_api->isBlocked())
    {
        arm(rateLimitedPollDelayMs(m_api->blockedUntil(), m_lastPollAt, QDateTime::currentMSecsSinceEpoch()));
        return;
    }
    const PollPlan next = plan();
    if(next.delayMs != kStopPolling)
    {
        arm(next.delayMs, next.kind);
        return;
    }
    m_timer.stop();
    if(isPollHardFail(m_failures))
        emit connectionLost();
}

void SpotifyPoller::arm(int delayMs, PollKind kind)
{
    m_armedKind = kind;
    m_timer.start(delayMs);
}

PollPlan SpotifyPoller::plan() const
{
    PollInputs in;
    in.focused = m_focused;
    in.engineAwaiting = m_engineAwaiting;
    in.awaitingSinceMs = m_awaitingSince;
    in.selectedPlaying = m_last.hasPlayback && m_last.isPlaying && m_last.deviceId == m_device;
    in.hadPlayback = m_hadPlayback;
    in.failures = m_failures;
    in.predictedEndMs = m_predictedEnd;
    in.beforeEndPolled = m_beforeEndPolled;
    in.afterEndPolled = m_afterEndPolled;
    in.nowMs = QDateTime::currentMSecsSinceEpoch();
    return planNextPoll(in);
}
