#include <limits>
#include <QtGlobal>
#include "pollpolicy.h"
#include "spotifytypes.h"

namespace {

const int kActiveMs = 2000;
const int kIdleMs = 60000;
const int kFirstRetryMs = 2000;
// the extra poll before a track ends lands ahead of the engine's queue step (1.5 s before the end)
const qint64 kBeforeEndMs = 5000;
const qint64 kInteractionGapMs = 5000;
// polls fast while waiting for a start at most this long: a device that never starts our
// track must not be polled every 2 s for good
const qint64 kAwaitingFastMs = 20000;

}

int nextPollDelayMs(const PollInputs &in)
{
    if(isPollHardFail(in.failures))
        return kStopPolling;
    if(in.failures > 0)
        return kFirstRetryMs << (in.failures - 1);
    // the engine polls through a 204: a device that has not started yet reports none
    if(!in.hadPlayback && !in.engineActive)
        return kStopPolling;
    const bool awaiting = in.engineAwaiting && in.nowMs - in.awaitingSinceMs < kAwaitingFastMs;
    if(awaiting || (in.visible && (in.engineActive || in.selectedPlaying)))
        return kActiveMs;
    if(in.visible || !in.selectedPlaying || in.predictedEndMs <= 0)
        return kIdleMs;
    const qint64 untilBeforeEnd = in.predictedEndMs - kBeforeEndMs - in.nowMs;
    // a nearer poll counts as inside the last 5 s: a stalled progress would repeat it at once
    return untilBeforeEnd >= kActiveMs ? int(qMin<qint64>(untilBeforeEnd, kIdleMs)) : kIdleMs;
}

bool isPollHardFail(int failures)
{
    return failures > kMaxPollRetries;
}

qint64 predictedTrackEndMs(const PlayerState &state, qint64 pollTimeMs)
{
    if(!state.hasPlayback || !state.isPlaying || state.track.durationMs <= 0)
        return 0;
    return pollTimeMs + qMax<qint64>(state.track.durationMs - state.progressMs, 0);
}

int interactionDelayMs(qint64 lastPollMs, qint64 nowMs, int remainingMs, int policyDelayMs)
{
    const qint64 throttledFor = lastPollMs + kInteractionGapMs - nowMs;
    if(throttledFor <= 0)
        return remainingMs >= 0 && remainingMs <= kPollSoonMs ? kKeepTimer : kPollSoonMs;
    // polling stopped less than 5 s ago: the trigger must not be lost
    if(remainingMs < 0)
        return int(throttledFor);
    // the state is fresh, but a window that became visible may need a sooner poll
    return policyDelayMs != kStopPolling && policyDelayMs < remainingMs ? policyDelayMs : kKeepTimer;
}

int rateLimitedPollDelayMs(qint64 blockedUntilMs, qint64 nowMs)
{
    // polls one second after the block ends, so the gate has surely opened
    const qint64 delay = qMax(blockedUntilMs - nowMs, qint64(0)) + 1000;
    return int(qMin(delay, qint64(std::numeric_limits<int>::max())));
}
