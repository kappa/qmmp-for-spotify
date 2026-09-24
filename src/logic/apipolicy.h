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
};

ApiDecision classifyReply(int httpStatus, const QString &networkError, const QByteArray &retryAfter,
                          const QByteArray &body, bool retried);

#endif
