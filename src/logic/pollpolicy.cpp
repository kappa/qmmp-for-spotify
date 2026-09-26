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
    if(in.engineAwaiting || (in.visible && (in.engineActive || in.selectedPlaying)))
        return kActiveMs;
    if(in.visible || !in.selectedPlaying || in.predictedEndMs <= 0)
        return kIdleMs;
    const qint64 untilBeforeEnd = in.predictedEndMs - kBeforeEndMs - in.nowMs;
    return untilBeforeEnd > 0 ? int(qMin<qint64>(untilBeforeEnd, kIdleMs)) : kIdleMs;
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

bool interactionPolls(qint64 lastPollMs, qint64 nowMs)
{
    return nowMs - lastPollMs >= kInteractionGapMs;
}

int rateLimitedPollDelayMs(qint64 blockedUntilMs, qint64 nowMs)
{
    // polls one second after the block ends, so the gate has surely opened
    const qint64 delay = qMax(blockedUntilMs - nowMs, qint64(0)) + 1000;
    return int(qMin(delay, qint64(std::numeric_limits<int>::max())));
}
