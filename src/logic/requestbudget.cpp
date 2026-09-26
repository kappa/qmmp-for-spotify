#include "requestbudget.h"

bool RequestBudget::take(qint64 nowMs)
{
    while(!m_sent.empty() && nowMs - m_sent.front() >= kWindowMs)
        m_sent.pop_front();
    if(int(m_sent.size()) >= kMaxRequests)
        return false;
    m_sent.push_back(nowMs);
    return true;
}
