#include <QJsonDocument>
#include <QJsonObject>
#include "apipolicy.h"

using namespace Qt::StringLiterals;

namespace {

const int kMaxRetryAfterSeconds = 10;

}

ApiDecision classifyReply(int httpStatus, const QString &networkError, const QByteArray &retryAfter,
                          const QByteArray &body, bool retried)
{
    ApiDecision decision;
    if(httpStatus == 0)
    {
        decision.message = u"Spotify: network error: %1"_s.arg(networkError.isEmpty() ? u"no response"_s : networkError);
        return decision;
    }
    if(httpStatus >= 200 && httpStatus < 300)
    {
        decision.action = ApiDecision::Success;
        return decision;
    }
    if(httpStatus == 401 && !retried)
    {
        decision.action = ApiDecision::RefreshAndRetry;
        return decision;
    }
    if(httpStatus == 429 && !retried)
    {
        bool ok = false;
        const int seconds = retryAfter.trimmed().toInt(&ok);
        // a long wait would leave a user command hanging silently
        if(ok && seconds > kMaxRetryAfterSeconds)
        {
            decision.message = u"Spotify: rate limited, try again in %1 min"_s.arg((seconds + 59) / 60);
            return decision;
        }
        decision.action = ApiDecision::RetryAfter;
        decision.retryAfterMs = (ok && seconds >= 0 ? seconds : 5) * 1000;
        return decision;
    }

    const QJsonObject error = QJsonDocument::fromJson(body).object().value("error"_L1).toObject();
    const QString message = error.value("message"_L1).toString();
    const QString reason = error.value("reason"_L1).toString();
    decision.reason = reason;
    // its message ("Cannot control device volume") would pass for a device problem
    if(reason == "VOLUME_CONTROL_DISALLOW"_L1)
        decision.message = u"Spotify: this device does not allow volume control"_s;
    else if(reason == "NO_ACTIVE_DEVICE"_L1 || message.contains("device"_L1, Qt::CaseInsensitive))
    {
        decision.deviceProblem = true;
        decision.message = u"Spotify: the selected device is not available. Pick one in Tools → Spotify → Device…"_s;
    }
    else if(httpStatus == 401)
        decision.message = u"Spotify: authorization failed, please log in again"_s;
    else if(httpStatus == 429)
        decision.message = u"Spotify: rate limit exceeded, try again later"_s;
    else if(!message.isEmpty())
        decision.message = u"Spotify: "_s + message;
    else
        decision.message = u"Spotify: request failed (HTTP %1)"_s.arg(httpStatus);
    return decision;
}
