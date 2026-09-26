#include "requestbudget.h"

bool RequestBudget::take(qint64 nowMs, bool bulk)
{
    while(!m_sent.empty() && nowMs - m_sent.front().at >= kWindowMs)
        m_sent.pop_front();
    if(waitMs(nowMs, bulk) > 0)
        return false;
    m_sent.push_back({ nowMs, bulk });
    return true;
}

// the window frees one slot at a time, oldest first
qint64 RequestBudget::waitMs(qint64 nowMs, bool bulk) const
{
    int total = 0;
    int bulkCount = 0;
    qint64 totalFreeAt = nowMs;
    qint64 bulkFreeAt = nowMs;
    for(const Sent &sent : m_sent)
    {
        if(nowMs - sent.at >= kWindowMs)
            continue;
        if(total++ == 0)
            totalFreeAt = sent.at + kWindowMs;
        if(sent.bulk && bulkCount++ == 0)
            bulkFreeAt = sent.at + kWindowMs;
    }
    qint64 freeAt = total >= kMaxRequests ? totalFreeAt : nowMs;
    if(bulk && bulkCount >= kMaxBulkRequests)
        freeAt = qMax(freeAt, bulkFreeAt);
    return freeAt - nowMs;
}
