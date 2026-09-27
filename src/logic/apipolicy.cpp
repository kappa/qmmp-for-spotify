#include <QJsonDocument>
#include <QJsonObject>
#include <limits>
#include "apipolicy.h"

using namespace Qt::StringLiterals;

namespace {

const int kMaxRetryAfterSeconds = 10;
const int kDefaultRetryAfterSeconds = 30;
const qint64 kMaxBlockMs = 24 * 3600 * 1000;
const int kBudgetOverrunBlockSeconds = 60;

}

int parseRetryAfterSeconds(const QByteArray &header)
{
    bool ok = false;
    const int seconds = header.trimmed().toInt(&ok);
    return ok && seconds >= 0 ? seconds : kDefaultRetryAfterSeconds;
}

qint64 extendBlock(qint64 blockedUntilMs, qint64 nowMs, int retryAfterSeconds)
{
    return capBlock(qMax(blockedUntilMs, nowMs + qint64(retryAfterSeconds) * 1000), nowMs);
}

qint64 capBlock(qint64 blockedUntilMs, qint64 nowMs)
{
    return qMin(blockedUntilMs, nowMs + kMaxBlockMs);
}

bool isBlocked(qint64 blockedUntilMs, qint64 nowMs)
{
    return nowMs < blockedUntilMs;
}

GateAction gateAction(qint64 rateLimitRemainingMs, qint64 overrunRemainingMs, bool bulk)
{
    const qint64 remaining = qMax(rateLimitRemainingMs, overrunRemainingMs);
    if(remaining <= 0)
        return GateAction::Send;
    if(remaining <= qint64(kMaxRetryAfterSeconds) * 1000)
        return GateAction::Hold;
    // a bulk request is finite work: it waits out the local block, not Spotify's long one
    if(bulk && rateLimitRemainingMs <= qint64(kMaxRetryAfterSeconds) * 1000)
        return GateAction::Hold;
    return GateAction::Refuse;
}

QString loadFailureMessage(const QString &what, const QString &apiMessage)
{
    if(!isRateLimitMessage(apiMessage))
        return apiMessage;
    return u"Spotify: couldn't load %1: %2"_s.arg(what, apiMessage.mid(u"Spotify: "_s.size()));
}

QString rateLimitMessage(qint64 remainingMs)
{
    const qint64 seconds = (qMax(remainingMs, qint64(0)) + 999) / 1000;
    if(seconds < 60)
        return u"Spotify: rate limited, try again in %1 s"_s.arg(seconds);
    return u"Spotify: rate limited, try again in %1 min"_s.arg((seconds + 59) / 60);
}

bool isRateLimitMessage(const QString &message)
{
    return message.startsWith("Spotify: rate limited, "_L1);
}

QString budgetExceededMessage()
{
    return u"Spotify: too many requests, pausing for 1 min"_s;
}

qint64 budgetOverrunBlock(qint64 blockedUntilMs, qint64 nowMs)
{
    return extendBlock(blockedUntilMs, nowMs, kBudgetOverrunBlockSeconds);
}

ApiDecision rateLimitDecision(qint64 waitMs, bool retried)
{
    ApiDecision decision;
    decision.rateLimited = true;
    decision.retryAfterMs = int(qMin(waitMs, qint64(std::numeric_limits<int>::max())));
    // a long wait would leave a user command hanging silently
    if(!retried && waitMs <= kMaxRetryAfterSeconds * 1000)
        decision.action = ApiDecision::RetryAfter;
    else
        decision.message = rateLimitMessage(waitMs);
    return decision;
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
    if(httpStatus == 429)
        return rateLimitDecision(qint64(parseRetryAfterSeconds(retryAfter)) * 1000, retried);

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
    else if(!message.isEmpty())
        decision.message = u"Spotify: "_s + message;
    else
        decision.message = u"Spotify: request failed (HTTP %1)"_s.arg(httpStatus);
    return decision;
}

bool isAlreadyPausedReply(int httpStatus, const QString &reason)
{
    // Spotify reports it as "Player command failed: Restriction violated", reason UNKNOWN
    return httpStatus == 403 && (reason == "UNKNOWN"_L1 || reason == "ALREADY_PAUSED"_L1);
}

bool isRestrictedTrackReply(int httpStatus, const QString &message)
{
    return httpStatus == 403 && message.contains("Restriction violated"_L1, Qt::CaseInsensitive);
}

bool isSystemicPlayFailure(int httpStatus, bool deviceProblem, const QString &reason)
{
    // status 0: no network, or not logged in; 401 here means the session could not be refreshed
    return httpStatus == 0 || httpStatus == 401 || httpStatus == 429 || httpStatus >= 500 || deviceProblem
           || reason == "PREMIUM_REQUIRED"_L1;
}
