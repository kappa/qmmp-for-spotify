#include <QDateTime>
#include <QPointer>
#include "pollpolicy.h"
#include "spotifyapi.h"
#include "spotifypoller.h"

SpotifyPoller::SpotifyPoller(SpotifyApi *api, QObject *parent) : QObject(parent), m_api(api)
{
    m_timer.setSingleShot(true);
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

void SpotifyPoller::setEngineActive(bool active)
{
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

void SpotifyPoller::pollSoon()
{
    if(!m_enabled || m_inFlight)
        return;
    // while rate limited the next poll stays at the end of the block
    if(m_api->isBlocked())
        scheduleNext();
    else
        m_timer.start(300);
}

void SpotifyPoller::poll()
{
    if(!m_enabled || m_inFlight)
        return;
    m_inFlight = true;
    QPointer<SpotifyPoller> self(this);
    m_api->playerState([self](const ApiResult &result, const PlayerState &state) {
        if(!self)
            return;
        self->m_inFlight = false;
        if(result.ok)
        {
            self->m_failures = 0;
            self->m_last = state;
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
        // a coarse timer may fire minutes early on a long block
        m_timer.setTimerType(Qt::PreciseTimer);
        m_timer.start(rateLimitedPollDelayMs(m_api->blockedUntil(), QDateTime::currentMSecsSinceEpoch()));
        return;
    }
    const bool selectedPlaying = m_last.hasPlayback && m_last.isPlaying && m_last.deviceId == m_device;
    m_timer.setTimerType(Qt::CoarseTimer);
    m_timer.start(pollIntervalMs(m_engineActive, selectedPlaying, m_failures));
}
