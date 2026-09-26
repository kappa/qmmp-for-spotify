#ifndef POLLPOLICY_H
#define POLLPOLICY_H

#include <QtGlobal>

int pollIntervalMs(bool engineActive, bool selectedDevicePlaying, int failures);
// while requests are blocked the next poll waits for the block to end
int rateLimitedPollDelayMs(qint64 blockedUntilMs, qint64 nowMs);

#endif
