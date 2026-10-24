#ifndef APILOG_H
#define APILOG_H

#include <QByteArray>
#include <QDateTime>
#include <QString>

const qint64 kApiLogMaxBytes = 5 * 1024 * 1024;

// one tab-separated line: time, verb, path and query, status (0 = network error), duration, note
QString apiLogLine(const QDateTime &time, const QByteArray &verb, const QString &pathQuery, int status,
                   qint64 durationMs, const QString &note);
// retry-after shows the seconds applied, 30 when the header is missing; a request the plugin
// aborted itself (superseded) is noted "cancelled", not with Qt's "Operation canceled"
QString apiLogNote(int status, const QByteArray &retryAfter, bool retried, const QString &networkError,
                   bool cancelled = false);

#endif
