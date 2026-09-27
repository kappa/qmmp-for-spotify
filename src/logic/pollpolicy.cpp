#include <limits>
#include <QtGlobal>
#include "pollpolicy.h"
#include "spotifytypes.h"

namespace {

// awaiting a start; also the floor of a track-end poll
const int kActiveMs = 2000;
const int kPlayingMs = 5000;
const int kIdleMs = 60000;
const int kFirstRetryMs = 2000;
// the extra poll before a track ends lands ahead of the engine's queue step (1.5 s before the end)
const qint64 kBeforeEndMs = 5000;
// the extra poll after a track ends shows the device on the next track, or the engine's
// "did not advance" fallback acts on fresh state
const qint64 kAfterEndMs = 1500;
const qint64 kInteractionGapMs = 5000;
// command replies poll at most once per this long after the last poll
const qint64 kConfirmGapMs = 2000;
// polls fast while waiting for a start at most this long: a device that never starts our
// track must not be polled every 2 s for good
const qint64 kAwaitingFastMs = 20000;

int retryDelayMs(int failures)
{
    return kFirstRetryMs << (failures - 1);
}

}

PollPlan planNextPoll(const PollInputs &in)
{
    PollPlan plan;
    if(isPollHardFail(in.failures))
        return plan;
    if(in.failures > 0)
    {
        plan.delayMs = retryDelayMs(in.failures);
        return plan;
    }
    // polling goes on through a 204 only while the engine waits for a start: a device that
    // has not started yet reports none
    if(!in.hadPlayback && !in.engineAwaiting)
        return plan;
    if(in.engineAwaiting && in.nowMs - in.awaitingSinceMs < kAwaitingFastMs)
    {
        plan.delayMs = kActiveMs;
        return plan;
    }
    plan.delayMs = in.focused && in.selectedPlaying ? kPlayingMs : kIdleMs;
    if(!in.selectedPlaying || in.predictedEndMs <= 0)
        return plan;
    // each fires once per track, and one nearer than 2 s is skipped: a stalled progress
    // would repeat it
    const struct { qint64 at; bool done; PollKind kind; } ends[] = {
        { in.predictedEndMs - kBeforeEndMs, in.beforeEndPolled, PollKind::BeforeEnd },
        { in.predictedEndMs + kAfterEndMs, in.afterEndPolled, PollKind::AfterEnd },
    };
    for(const auto &end : ends)
    {
        const qint64 delay = end.at - in.nowMs;
        if(end.done || delay < kActiveMs)
            continue;
        if(delay < plan.delayMs)
        {
            plan.delayMs = int(delay);
            plan.kind = end.kind;
        }
        break;
    }
    return plan;
}

int nextPollDelayMs(const PollInputs &in)
{
    return planNextPoll(in).delayMs;
}

bool startsTrackEndCycle(const QString &trackId, qint64 progressMs, const PlayerState &state)
{
    if(!state.hasPlayback)
        return false;
    // small backward steps are jitter between polls, not a restart
    return state.track.id != trackId || state.progressMs < progressMs - kActiveMs;
}

bool isPollHardFail(int failures)
{
    return failures > kMaxPollRetries;
}

int pollSoonDelayMs(int failures, int remainingMs, qint64 lastPollMs, qint64 nowMs)
{
    if(failures > 0)
        return isPollHardFail(failures) || remainingMs >= 0 ? kKeepTimer : retryDelayMs(failures);
    // a burst of commands (Next clicks, seeks) must not turn into a poll per command
    const qint64 delay = qMax<qint64>(kPollSoonMs, lastPollMs + kConfirmGapMs - nowMs);
    // an armed poll that is not too early (the device needs a moment) confirms as well
    if(remainingMs >= kPollSoonMs && remainingMs <= delay)
        return kKeepTimer;
    return int(delay);
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
    // the state is fresh, but qmmp becoming focused may need a sooner poll
    return policyDelayMs != kStopPolling && policyDelayMs < remainingMs ? policyDelayMs : kKeepTimer;
}

int rateLimitedPollDelayMs(qint64 blockedUntilMs, qint64 lastPollMs, qint64 nowMs)
{
    // polls one second after the block ends, so the gate has surely opened, and not within
    // 2 s of the last poll
    const qint64 delay = qMax(qMax(blockedUntilMs - nowMs, qint64(0)) + 1000, lastPollMs + kConfirmGapMs - nowMs);
    return int(qMin(delay, qint64(std::numeric_limits<int>::max())));
}
