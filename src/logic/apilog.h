#ifndef APILOG_H
#define APILOG_H

#include <QByteArray>
#include <QDateTime>
#include <QString>

const qint64 kApiLogMaxBytes = 5 * 1024 * 1024;

// one tab-separated line: time, verb, path and query, status (0 = network error), duration, note
QString apiLogLine(const QDateTime &time, const QByteArray &verb, const QString &pathQuery, int status,
                   qint64 durationMs, const QString &note);
QString apiLogNote(int status, const QByteArray &retryAfter, bool retried, const QString &networkError);

#endif
