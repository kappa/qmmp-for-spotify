#include "requestbudget.h"

RequestBudget::RequestBudget(int maxRequests) : m_max(maxRequests)
{}

bool RequestBudget::take(qint64 nowMs)
{
    while(!m_sent.empty() && nowMs - m_sent.front() >= kWindowMs)
        m_sent.pop_front();
    if(int(m_sent.size()) >= m_max)
        return false;
    m_sent.push_back(nowMs);
    return true;
}

// the window frees one slot at a time, oldest first
qint64 RequestBudget::waitMs(qint64 nowMs) const
{
    int live = 0;
    qint64 oldest = nowMs;
    for(const qint64 at : m_sent)
    {
        if(nowMs - at >= kWindowMs)
            continue;
        if(live++ == 0)
            oldest = at;
    }
    return live >= m_max ? oldest + kWindowMs - nowMs : 0;
}

int RequestBudget::available(qint64 nowMs) const
{
    int live = 0;
    for(const qint64 at : m_sent)
    {
        if(nowMs - at < kWindowMs)
            live++;
    }
    return qMax(m_max - live, 0);
}
