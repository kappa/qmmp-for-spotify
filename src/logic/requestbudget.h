#ifndef REQUESTBUDGET_H
#define REQUESTBUDGET_H

#include <deque>
#include <QtGlobal>

// A safety net against request loops: at most kMaxRequests Web API requests in any rolling
// kWindowMs. Bulk requests (paged loads, link expansion) wait for a slot instead of failing,
// and may hold at most kMaxBulkRequests of them, so polls and playback commands always have room.
class RequestBudget
{
public:
    static constexpr int kMaxRequests = 30;
    static constexpr int kMaxBulkRequests = 20;
    static constexpr qint64 kWindowMs = 10000;

    // counts a request sent at nowMs (a monotonic clock); false: over the budget, it must not be sent
    bool take(qint64 nowMs, bool bulk = false);
    // how long until take() can succeed for this kind of request; 0: now
    qint64 waitMs(qint64 nowMs, bool bulk) const;

private:
    struct Sent
    {
        qint64 at;
        bool bulk;
    };

    std::deque<Sent> m_sent;
};

#endif
