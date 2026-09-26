#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include "apilog.h"
#include "spotifyapilog.h"

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcSpotifyApi, "spotify.api", QtInfoMsg)

void logApiRequest(const QString &dir, const QByteArray &verb, const QString &pathQuery, int status,
                   qint64 durationMs, const QString &note)
{
    const QString line = apiLogLine(QDateTime::currentDateTime(), verb, pathQuery, status, durationMs, note);
    qCDebug(lcSpotifyApi, "%s", qPrintable(line));

    const QString path = dir + u"/api.log"_s;
    if(QFileInfo(path).size() > kApiLogMaxBytes)
    {
        QFile::remove(path + u".1"_s);
        QFile::rename(path, path + u".1"_s);
    }
    QDir().mkpath(dir);
    QFile file(path);
    const bool created = !file.exists();
    if(!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return;
    if(created)
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(line.toUtf8() + '\n');
}
