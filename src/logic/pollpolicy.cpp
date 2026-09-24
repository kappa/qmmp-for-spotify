#include <QtGlobal>
#include "pollpolicy.h"

int pollIntervalMs(bool engineActive, bool selectedDevicePlaying, int failures)
{
    if(failures > 0)
        return qMin(2000 << qMin(failures - 1, 4), 30000);
    return engineActive || selectedDevicePlaying ? 2000 : 10000;
}
