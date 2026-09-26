#ifndef REQUESTBUDGET_H
#define REQUESTBUDGET_H

#include <deque>
#include <QtGlobal>

// Web API requests per rolling window: interactive ones (polls, playback, volume, dialogs) as a
// safety net against request loops, bulk ones (paged loads, link expansion) as pacing
const int kInteractiveBudget = 30;
const int kBulkBudget = 20;

// at most maxRequests requests in any rolling kWindowMs
class RequestBudget
{
public:
    static constexpr qint64 kWindowMs = 10000;

    explicit RequestBudget(int maxRequests);

    // counts a request sent at nowMs (a monotonic clock); false: over the budget, it must not be sent
    bool take(qint64 nowMs);
    // how long until take() can succeed; 0: now
    qint64 waitMs(qint64 nowMs) const;
    // requests take() would allow now
    int available(qint64 nowMs) const;

private:
    int m_max;
    std::deque<qint64> m_sent;
};

#endif
