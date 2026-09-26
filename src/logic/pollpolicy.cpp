#include <limits>
#include <QtGlobal>
#include "pollpolicy.h"

int pollIntervalMs(bool engineActive, bool selectedDevicePlaying, int failures)
{
    if(failures > 0)
        return qMin(2000 << qMin(failures - 1, 4), 30000);
    return engineActive || selectedDevicePlaying ? 2000 : 10000;
}

int rateLimitedPollDelayMs(qint64 blockedUntilMs, qint64 nowMs)
{
    // polls one second after the block ends, so the gate has surely opened
    const qint64 delay = qMax(blockedUntilMs - nowMs, qint64(0)) + 1000;
    return int(qMin(delay, qint64(std::numeric_limits<int>::max())));
}
