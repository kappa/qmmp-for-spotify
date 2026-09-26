#include <QStringList>
#include "apilog.h"

using namespace Qt::StringLiterals;

QString apiLogLine(const QDateTime &time, const QByteArray &verb, const QString &pathQuery, int status,
                   qint64 durationMs, const QString &note)
{
    return QStringList { time.toString(u"yyyy-MM-dd'T'HH:mm:ss.zzz"_s), QString::fromLatin1(verb), pathQuery,
                         QString::number(status), QString::number(durationMs), note }.join(u'\t');
}

QString apiLogNote(int status, const QByteArray &retryAfter, bool retried, const QString &networkError)
{
    QStringList notes;
    if(retried)
        notes << u"retry"_s;
    if(status == 429)
        notes << u"retry-after="_s + QString::fromLatin1(retryAfter.trimmed());
    if(status == 0)
        notes << networkError;
    return notes.join(u' ');
}
