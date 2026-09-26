#ifndef SPOTIFYAPI_H
#define SPOTIFYAPI_H

#include <deque>
#include <functional>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include "requestbudget.h"
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
    // not sent: over the request budget (RequestBudget)
    bool budgetExceeded = false;
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
    // paged loads (these four) and track() for link expansion are bulk requests: they wait for
    // a slot in the request budget instead of failing
    void myPlaylists(ListCallback<SpotifyPlaylist> callback);
    void playlistTracks(const QString &playlistId, ListCallback<SpotifyTrack> callback);
    void likedTracks(ListCallback<SpotifyTrack> callback);
    void albumTracks(const QString &albumId, ListCallback<SpotifyTrack> callback);
    void track(const QString &trackId, std::function<void(const ApiResult &, const SpotifyTrack &)> callback);
    // GET of any Web API path, e.g. "/me/player?market=from_token"
    void get(const QString &pathQuery, Callback callback);

    // after a 429 no request is sent until this time (ms since the epoch; kept across restarts),
    // after a budget overrun for 1 min (not kept)
    qint64 blockedUntil() const;
    bool isBlocked() const;

signals:
    // a request failed with 429 from Spotify
    void rateLimited(const QString &message);
    // a request was not sent: too many in a short time, all requests are held back for 1 min
    void budgetExceeded(const QString &message);

private:
    // bulk: one of many requests of a paged load or a link expansion; it waits for a free slot
    // in its own budget instead of failing (woken: its turn came, it goes before the queue)
    void request(const QByteArray &verb, const QUrl &url, const QByteArray &body, Callback callback, bool retried = false,
                 bool bulk = false, bool woken = false);
    // true when the request was held or refused instead of sent
    bool gate(const QByteArray &verb, const QUrl &url, const QByteArray &body, const Callback &callback, bool retried,
              bool bulk);
    void refuseOverBudget(const QByteArray &verb, const QUrl &url, const Callback &callback);
    void waitForBulkSlot(std::function<void()> send, bool first);
    void scheduleBulkWake();
    void wakeBulk();
    qint64 overrunRemainingMs() const;
    void blockFor(int retryAfterSeconds);
    void clearBlock();
    template <typename T>
    void collect(const QUrl &url, std::function<SpotifyPage<T>(const QByteArray &)> parse, QList<T> items,
                 ListCallback<T> callback, int pages = 0);

    SpotifyAuth *m_auth;
    QNetworkAccessManager *m_network;
    QString m_dataDir;
    qint64 m_blockedUntil = 0;
    // the budgets and the overrun block run on a monotonic clock: a wall-clock step must not
    // freeze or free them
    QElapsedTimer m_clock;
    qint64 m_overrunBlockedUntil = 0;
    RequestBudget m_interactiveBudget { kInteractiveBudget };
    RequestBudget m_bulkBudget { kBulkBudget };
    // bulk requests waiting for a slot, woken in order, one per free slot
    std::deque<std::function<void()>> m_bulkWaiters;
    QTimer m_bulkTimer;
};

#endif
