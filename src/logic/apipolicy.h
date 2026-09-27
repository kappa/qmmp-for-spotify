#ifndef APIPOLICY_H
#define APIPOLICY_H

#include <QByteArray>
#include <QString>

struct ApiDecision
{
    enum Action { Success, RefreshAndRetry, RetryAfter, Fail };

    Action action = Fail;
    int retryAfterMs = 0;
    QString message;
    bool deviceProblem = false;
    QString reason;
    bool rateLimited = false;
};

// seconds from a Retry-After header; missing, garbage or negative → 30
int parseRetryAfterSeconds(const QByteArray &header);
// a 429 extends a block of all requests; it never shortens one, but lasts at most 24 h from now
qint64 extendBlock(qint64 blockedUntilMs, qint64 nowMs, int retryAfterSeconds);
// a block read back from disk, capped at 24 h from now (a corrupted file or a changed clock)
qint64 capBlock(qint64 blockedUntilMs, qint64 nowMs);
bool isBlocked(qint64 blockedUntilMs, qint64 nowMs);
// a request made during a short block waits for its end; one made during a long block fails
enum class GateAction { Send, Hold, Refuse };
// from the remaining 429 block and the remaining local block after a budget overrun; a bulk
// request (paged load, link expansion) waits through the overrun block, however long
GateAction gateAction(qint64 rateLimitRemainingMs, qint64 overrunRemainingMs, bool bulk);
QString rateLimitMessage(qint64 remainingMs);
bool isRateLimitMessage(const QString &message);
// a request not sent because too many were sent in a short time: something loops
QString budgetExceededMessage();
// a failed load of <what> (a playlist, Liked Songs, links): a rate-limit message names what failed,
// so it is not hidden with the other rate-limit notices of the block
QString loadFailureMessage(const QString &what, const QString &apiMessage);
// the local block a budget overrun imposes through the rate-limit gate (1 min, never shortening)
qint64 budgetOverrunBlock(qint64 blockedUntilMs, qint64 nowMs);
// a short wait is retried once automatically; a long one fails at once
ApiDecision rateLimitDecision(qint64 waitMs, bool retried);

ApiDecision classifyReply(int httpStatus, const QString &networkError, const QByteArray &retryAfter,
                          const QByteArray &body, bool retried);
// Spotify answers a pause of an already-paused device with 403
bool isAlreadyPausedReply(int httpStatus, const QString &reason);
// a play of a track not available in the user's country: "Restriction violated"
bool isRestrictedTrackReply(int httpStatus, const QString &message);
// a failed play that every other track would hit too: qmmp must stop instead of skipping
bool isSystemicPlayFailure(int httpStatus, bool deviceProblem, const QString &reason);

#endif
