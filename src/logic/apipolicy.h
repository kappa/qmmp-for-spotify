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
};

ApiDecision classifyReply(int httpStatus, const QString &networkError, const QByteArray &retryAfter,
                          const QByteArray &body, bool retried);
// Spotify answers a pause of an already-paused device with 403
bool isAlreadyPausedReply(int httpStatus, const QString &reason);
// a failed play that every other track would hit too: qmmp must stop instead of skipping
bool isSystemicPlayFailure(int httpStatus, bool deviceProblem, const QString &reason);

#endif
