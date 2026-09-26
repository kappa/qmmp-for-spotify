#ifndef REQUESTBUDGET_H
#define REQUESTBUDGET_H

#include <deque>
#include <QtGlobal>

// A safety net against request loops: at most kMaxRequests Web API requests in any rolling
// kWindowMs.
class RequestBudget
{
public:
    static constexpr int kMaxRequests = 30;
    static constexpr qint64 kWindowMs = 10000;

    // counts a request sent at nowMs; false: over the budget, it must not be sent
    bool take(qint64 nowMs);

private:
    std::deque<qint64> m_sent;
};

#endif
