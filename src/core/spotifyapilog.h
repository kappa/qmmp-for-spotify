#ifndef SPOTIFYAPILOG_H
#define SPOTIFYAPILOG_H

#include <QByteArray>
#include <QLoggingCategory>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(lcSpotifyApi)

// appends one line to <dir>/api.log (mode 0600, rotated to api.log.1 above 5 MB)
// and mirrors it to the spotify.api logging category
void logApiRequest(const QString &dir, const QByteArray &verb, const QString &pathQuery, int status,
                   qint64 durationMs, const QString &note);

#endif
