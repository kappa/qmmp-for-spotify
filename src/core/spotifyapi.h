#ifndef SPOTIFYAPI_H
#define SPOTIFYAPI_H

#include <functional>
#include <QObject>
#include <QUrl>
#include <QUrlQuery>
#include "spotifytypes.h"

class QNetworkAccessManager;
class SpotifyAuth;

struct ApiResult
{
    bool ok = false;
    int status = 0;
    QString message;
    bool deviceProblem = false;
    QByteArray body;
    QString reason;
    // Spotify answered 429, or the request was refused locally while a 429 block lasts
    bool rateLimited = false;
};

class SpotifyApi : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const ApiResult &)>;
    template <typename T>
    using ListCallback = std::function<void(const ApiResult &, const QList<T> &)>;

    explicit SpotifyApi(SpotifyAuth *auth, QObject *parent = nullptr);

    void devices(ListCallback<SpotifyDevice> callback);
    void playerState(std::function<void(const ApiResult &, const PlayerState &)> callback);
    void displayName(std::function<void(const ApiResult &, const QString &)> callback);
    void userId(std::function<void(const ApiResult &, const QString &)> callback);
    void play(const QString &deviceId, const QStringList &uris, qint64 positionMs, Callback callback);
    void pause(const QString &deviceId, Callback callback);
    void resume(const QString &deviceId, Callback callback);
    void seek(const QString &deviceId, qint64 positionMs, Callback callback);
    void queue(const QString &deviceId, const QString &uri, Callback callback);
    void next(const QString &deviceId, Callback callback);
    void setVolume(const QString &deviceId, int percent, Callback callback);
    void myPlaylists(ListCallback<SpotifyPlaylist> callback);
    void playlistTracks(const QString &playlistId, ListCallback<SpotifyTrack> callback);
    void likedTracks(ListCallback<SpotifyTrack> callback);
    void albumTracks(const QString &albumId, ListCallback<SpotifyTrack> callback);
    void track(const QString &trackId, std::function<void(const ApiResult &, const SpotifyTrack &)> callback);
    // GET of any Web API path, e.g. "/me/player?market=from_token"
    void get(const QString &pathQuery, Callback callback);

    // after a 429 no request is sent until this time (ms since the epoch); kept across restarts
    qint64 blockedUntil() const { return m_blockedUntil; }
    bool isBlocked() const;

signals:
    // a request failed with 429 from Spotify
    void rateLimited(const QString &message);

private:
    void request(const QByteArray &verb, const QUrl &url, const QByteArray &body, Callback callback, bool retried = false);
    bool refuseWhileBlocked(const QByteArray &verb, const QUrl &url, const Callback &callback);
    void blockFor(int retryAfterSeconds);
    template <typename T>
    void collect(const QUrl &url, std::function<SpotifyPage<T>(const QByteArray &)> parse, QList<T> items,
                 ListCallback<T> callback, int pages = 0);

    SpotifyAuth *m_auth;
    QNetworkAccessManager *m_network;
    QString m_dataDir;
    qint64 m_blockedUntil = 0;
};

#endif
