#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QTimer>
#include "apilog.h"
#include "apipolicy.h"
#include "spotifyapi.h"
#include "spotifyapilog.h"
#include "spotifyauth.h"
#include "spotifyjson.h"

using namespace Qt::StringLiterals;

namespace {

const int kMaxPages = 200;
const int kTransferTimeoutMs = 15000;
const int kCommandTimeoutMs = 5000;
// precise timers never fire early; the margin absorbs millisecond rounding of the clock
const int kRetryMarginMs = 50;

qint64 now()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString pathQuery(const QUrl &url)
{
    return url.toString(QUrl::RemoveScheme | QUrl::RemoveAuthority);
}

QUrl endpoint(const QString &path, const QUrlQuery &query = QUrlQuery())
{
    QUrl url(u"https://api.spotify.com/v1"_s + path);
    url.setQuery(query);
    return url;
}

QUrlQuery deviceQuery(const QString &deviceId)
{
    QUrlQuery query;
    if(!deviceId.isEmpty())
        query.addQueryItem(u"device_id"_s, deviceId);
    return query;
}

QUrlQuery pageQuery()
{
    QUrlQuery query;
    query.addQueryItem(u"limit"_s, u"50"_s);
    return query;
}

}

void ApiRequest::cancel()
{
    m_cancelled = true;
    if(m_reply && m_reply->isRunning())
        m_reply->abort();
}

SpotifyApi::SpotifyApi(SpotifyAuth *auth, QObject *parent)
    : QObject(parent), m_auth(auth), m_network(new QNetworkAccessManager(this)), m_dataDir(auth->dataDir())
{
    // a past, missing or unreadable block leaves requests open
    QFile file(m_dataDir + u"/ratelimit"_s);
    if(file.open(QIODevice::ReadOnly))
        m_blockedUntil = capBlock(file.readAll().trimmed().toLongLong(), now());
    connect(auth, &SpotifyAuth::loggedOut, this, &SpotifyApi::clearBlock);
    m_clock.start();
    m_bulkTimer.setSingleShot(true);
    m_bulkTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_bulkTimer, &QTimer::timeout, this, &SpotifyApi::wakeBulk);
}

qint64 SpotifyApi::overrunRemainingMs() const
{
    return qMax<qint64>(m_overrunBlockedUntil - m_clock.elapsed(), 0);
}

qint64 SpotifyApi::blockedUntil() const
{
    const qint64 overrun = overrunRemainingMs();
    return overrun > 0 ? qMax(m_blockedUntil, now() + overrun) : m_blockedUntil;
}

bool SpotifyApi::isBlocked() const
{
    return ::isBlocked(blockedUntil(), now());
}

// a short block holds the request until it ends, then it passes the gate again; during a long
// one the callback runs on the next event-loop turn: callers may be in the middle of a state change.
// Bulk requests wait out a budget overrun block of any length.
bool SpotifyApi::gate(const QByteArray &verb, const QUrl &url, const QByteArray &body, const Callback &callback,
                      Kind kind, const ApiRequestHandle &handle, bool retried)
{
    const qint64 rateLimitRemaining = qMax<qint64>(m_blockedUntil - now(), 0);
    const qint64 remaining = qMax(rateLimitRemaining, overrunRemainingMs());
    switch(gateAction(rateLimitRemaining, overrunRemainingMs(), kind == Kind::Bulk))
    {
    case GateAction::Send:
        return false;
    case GateAction::Hold:
        QTimer::singleShot(int(remaining) + kRetryMarginMs, Qt::PreciseTimer, this,
                           [=] { request(verb, url, body, callback, kind, handle, retried); });
        return true;
    case GateAction::Refuse:
        break;
    }
    logApiRequest(m_dataDir, verb, pathQuery(url), 429, 0, retried ? u"retry blocked"_s : u"blocked"_s);
    ApiResult result;
    result.status = 429;
    result.rateLimited = true;
    result.message = rateLimitMessage(remaining);
    QTimer::singleShot(0, this, [callback, result] { callback(result); });
    return true;
}

void SpotifyApi::waitForBulkSlot(std::function<void()> send, bool first)
{
    if(first)
        m_bulkWaiters.push_front(std::move(send));
    else
        m_bulkWaiters.push_back(std::move(send));
    scheduleBulkWake();
}

void SpotifyApi::scheduleBulkWake()
{
    if(m_bulkWaiters.empty())
        return;
    const int delay = int(m_bulkBudget.waitMs(m_clock.elapsed())) + kRetryMarginMs;
    if(!m_bulkTimer.isActive() || m_bulkTimer.remainingTime() > delay)
        m_bulkTimer.start(delay);
}

// one waiter per free slot, oldest first; each goes through the gate and the token again
void SpotifyApi::wakeBulk()
{
    for(int free = m_bulkBudget.available(m_clock.elapsed()); free > 0 && !m_bulkWaiters.empty(); free--)
    {
        const std::function<void()> send = std::move(m_bulkWaiters.front());
        m_bulkWaiters.pop_front();
        send();
    }
    scheduleBulkWake();
}

// something sends in a loop: all requests stop for a while, through the rate-limit gate
void SpotifyApi::refuseOverBudget(const QByteArray &verb, const QUrl &url, const Callback &callback)
{
    logApiRequest(m_dataDir, verb, pathQuery(url), 0, 0, u"budget"_s);
    m_overrunBlockedUntil = budgetOverrunBlock(m_overrunBlockedUntil, m_clock.elapsed());
    ApiResult result;
    result.message = budgetExceededMessage();
    result.budgetExceeded = true;
    emit budgetExceeded(result.message);
    // like a refusal at the gate: callers may be in the middle of a state change
    QTimer::singleShot(0, this, [callback, result] { callback(result); });
}

void SpotifyApi::clearBlock()
{
    m_blockedUntil = 0;
    m_overrunBlockedUntil = 0;
    QFile::remove(m_dataDir + u"/ratelimit"_s);
}

void SpotifyApi::blockFor(int retryAfterSeconds)
{
    const qint64 blockedUntil = extendBlock(m_blockedUntil, now(), retryAfterSeconds);
    if(blockedUntil == m_blockedUntil)
        return;
    m_blockedUntil = blockedUntil;
    QDir().mkpath(m_dataDir);
    QSaveFile file(m_dataDir + u"/ratelimit"_s);
    if(!file.open(QIODevice::WriteOnly))
    {
        qWarning("Spotify: cannot write %s", qPrintable(file.fileName()));
        return;
    }
    file.write(QByteArray::number(m_blockedUntil) + '\n');
    file.commit();
}

void SpotifyApi::request(const QByteArray &verb, const QUrl &url, const QByteArray &body, Callback callback, Kind kind,
                         ApiRequestHandle handle, bool retried, bool woken)
{
    // a cancelled request held by the gate, waiting for a slot or due for a retry is dropped
    if(handle && handle->m_cancelled)
        return;
    if(gate(verb, url, body, callback, kind, handle, retried))
        return;
    m_auth->withToken([=](bool ok) {
        if(handle && handle->m_cancelled)
            return;
        if(!ok)
        {
            ApiResult result;
            result.message = u"Spotify: not logged in. Log in in the Spotify plugin settings"_s;
            callback(result);
            return;
        }
        // a 429 may have arrived while the token was being refreshed
        if(gate(verb, url, body, callback, kind, handle, retried))
            return;
        // token refreshes are not counted. Bulk requests are finite (pages, pasted links): they
        // are paced by their own budget and wait in line; the interactive budget is a safety net
        // against request loops and does not see bulk traffic
        if(kind == Kind::Bulk)
        {
            if((!woken && !m_bulkWaiters.empty()) || !m_bulkBudget.take(m_clock.elapsed()))
            {
                waitForBulkSlot([=] { request(verb, url, body, callback, kind, handle, retried, true); }, woken);
                return;
            }
        }
        else if(!m_interactiveBudget.take(m_clock.elapsed()))
        {
            refuseOverBudget(verb, url, callback);
            return;
        }
        QNetworkRequest networkRequest(url);
        networkRequest.setRawHeader("Authorization", "Bearer " + m_auth->accessToken().toUtf8());
        networkRequest.setHeader(QNetworkRequest::ContentLengthHeader, body.size());
        if(!body.isEmpty())
            networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
        networkRequest.setTransferTimeout(kind == Kind::Command ? kCommandTimeoutMs : kTransferTimeoutMs);
        QElapsedTimer clock;
        clock.start();
        QNetworkReply *reply = m_network->sendCustomRequest(networkRequest, verb, body);
        if(handle)
            handle->m_reply = reply;
        connect(reply, &QNetworkReply::finished, this, [=] {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray data = reply->readAll();
            const QString networkError = status == 0 ? reply->errorString() : QString();
            const QByteArray retryAfter = reply->rawHeader("Retry-After");
            logApiRequest(m_dataDir, verb, pathQuery(url), status, clock.elapsed(),
                          apiLogNote(status, retryAfter, retried, networkError));
            // aborted (or answered after being superseded): the result belongs to nobody
            if(handle && handle->m_cancelled)
                return;
            ApiDecision decision = classifyReply(status, networkError, retryAfter, data, retried);
            if(decision.rateLimited)
            {
                blockFor(parseRetryAfterSeconds(retryAfter));
                // an earlier 429 may have set a longer block than this one
                decision = rateLimitDecision(blockedUntil() - now(), retried);
                if(decision.action == ApiDecision::Fail)
                    emit rateLimited(decision.message);
            }
            switch(decision.action)
            {
            case ApiDecision::Success:
                callback(ApiResult { true, status, QString(), false, data, QString(), false, false });
                break;
            case ApiDecision::RefreshAndRetry:
                m_auth->refresh([=](bool refreshed) {
                    if(refreshed)
                        request(verb, url, body, callback, kind, handle, true);
                    else
                        callback(ApiResult { false, status, u"Spotify session expired, please log in again"_s, false, data,
                                             QString(), false, false });
                });
                break;
            case ApiDecision::RetryAfter:
                QTimer::singleShot(decision.retryAfterMs + kRetryMarginMs, Qt::PreciseTimer, this,
                                   [=] { request(verb, url, body, callback, kind, handle, true); });
                break;
            case ApiDecision::Fail:
                callback(ApiResult { false, status, decision.message, decision.deviceProblem, data, decision.reason,
                                     decision.rateLimited, false });
                break;
            }
        });
    });
}

template <typename T>
void SpotifyApi::collect(const QUrl &url, std::function<SpotifyPage<T>(const QByteArray &)> parse, QList<T> items,
                         ListCallback<T> callback, int pages)
{
    request("GET", url, QByteArray(), [=](const ApiResult &result) mutable {
        if(!result.ok)
        {
            callback(result, items);
            return;
        }
        const SpotifyPage<T> page = parse(result.body);
        items << page.items;
        if(page.next.isEmpty() || pages + 1 >= kMaxPages)
        {
            callback(result, items);
            return;
        }
        collect<T>(QUrl(page.next), parse, items, callback, pages + 1);
    }, Kind::Bulk);
}

void SpotifyApi::devices(ListCallback<SpotifyDevice> callback)
{
    request("GET", endpoint(u"/me/player/devices"_s), QByteArray(), [callback](const ApiResult &result) {
        callback(result, result.ok ? SpotifyJson::parseDevices(result.body) : QList<SpotifyDevice>());
    });
}

void SpotifyApi::playerState(std::function<void(const ApiResult &, const PlayerState &)> callback)
{
    request("GET", endpoint(u"/me/player"_s), QByteArray(), [callback](const ApiResult &result) {
        callback(result, result.ok ? SpotifyJson::parsePlayerState(result.body) : PlayerState());
    });
}

void SpotifyApi::displayName(std::function<void(const ApiResult &, const QString &)> callback)
{
    request("GET", endpoint(u"/me"_s), QByteArray(), [callback](const ApiResult &result) {
        callback(result, result.ok ? SpotifyJson::parseDisplayName(result.body) : QString());
    });
}

void SpotifyApi::userId(std::function<void(const ApiResult &, const QString &)> callback)
{
    request("GET", endpoint(u"/me"_s), QByteArray(), [callback](const ApiResult &result) {
        callback(result, result.ok ? SpotifyJson::parseUserId(result.body) : QString());
    });
}

ApiRequestHandle SpotifyApi::play(const QString &deviceId, const QStringList &uris, qint64 positionMs, Callback callback)
{
    const QJsonObject body { { u"uris"_s, QJsonArray::fromStringList(uris) }, { u"position_ms"_s, positionMs } };
    const ApiRequestHandle handle = std::make_shared<ApiRequest>();
    request("PUT", endpoint(u"/me/player/play"_s, deviceQuery(deviceId)),
            QJsonDocument(body).toJson(QJsonDocument::Compact), callback, Kind::Command, handle);
    return handle;
}

void SpotifyApi::pause(const QString &deviceId, Callback callback)
{
    request("PUT", endpoint(u"/me/player/pause"_s, deviceQuery(deviceId)), QByteArray(), callback, Kind::Command);
}

void SpotifyApi::resume(const QString &deviceId, Callback callback)
{
    request("PUT", endpoint(u"/me/player/play"_s, deviceQuery(deviceId)), QByteArray(), callback, Kind::Command);
}

void SpotifyApi::seek(const QString &deviceId, qint64 positionMs, Callback callback)
{
    QUrlQuery query = deviceQuery(deviceId);
    query.addQueryItem(u"position_ms"_s, QString::number(positionMs));
    request("PUT", endpoint(u"/me/player/seek"_s, query), QByteArray(), callback, Kind::Command);
}

void SpotifyApi::queue(const QString &deviceId, const QString &uri, Callback callback)
{
    QUrlQuery query = deviceQuery(deviceId);
    query.addQueryItem(u"uri"_s, uri);
    request("POST", endpoint(u"/me/player/queue"_s, query), QByteArray(), callback, Kind::Command);
}

ApiRequestHandle SpotifyApi::next(const QString &deviceId, Callback callback)
{
    const ApiRequestHandle handle = std::make_shared<ApiRequest>();
    request("POST", endpoint(u"/me/player/next"_s, deviceQuery(deviceId)), QByteArray(), callback, Kind::Command, handle);
    return handle;
}

void SpotifyApi::setVolume(const QString &deviceId, int percent, Callback callback)
{
    QUrlQuery query = deviceQuery(deviceId);
    query.addQueryItem(u"volume_percent"_s, QString::number(percent));
    request("PUT", endpoint(u"/me/player/volume"_s, query), QByteArray(), callback, Kind::Command);
}

void SpotifyApi::myPlaylists(ListCallback<SpotifyPlaylist> callback)
{
    collect<SpotifyPlaylist>(endpoint(u"/me/playlists"_s, pageQuery()), SpotifyJson::parsePlaylists, {}, callback);
}

void SpotifyApi::playlistTracks(const QString &playlistId, ListCallback<SpotifyTrack> callback)
{
    QUrlQuery query = pageQuery();
    query.addQueryItem(u"additional_types"_s, u"track"_s);
    collect<SpotifyTrack>(endpoint(u"/playlists/%1/items"_s.arg(playlistId), query), SpotifyJson::parseTrackItems, {}, callback);
}

void SpotifyApi::likedTracks(ListCallback<SpotifyTrack> callback)
{
    collect<SpotifyTrack>(endpoint(u"/me/tracks"_s, pageQuery()), SpotifyJson::parseTrackItems, {}, callback);
}

void SpotifyApi::albumTracks(const QString &albumId, ListCallback<SpotifyTrack> callback)
{
    request("GET", endpoint(u"/albums/"_s + albumId), QByteArray(), [=](const ApiResult &result) {
        if(!result.ok)
        {
            callback(result, {});
            return;
        }
        QString name;
        const SpotifyPage<SpotifyTrack> page = SpotifyJson::parseAlbum(result.body, &name);
        if(page.next.isEmpty())
        {
            callback(result, page.items);
            return;
        }
        collect<SpotifyTrack>(QUrl(page.next), [name](const QByteArray &json) {
            return SpotifyJson::parseAlbumTracks(json, name);
        }, page.items, callback);
    }, Kind::Bulk);
}

void SpotifyApi::track(const QString &trackId, std::function<void(const ApiResult &, const SpotifyTrack &)> callback)
{
    request("GET", endpoint(u"/tracks/"_s + trackId), QByteArray(), [callback](const ApiResult &result) {
        callback(result, result.ok ? SpotifyJson::parseSingleTrack(result.body) : SpotifyTrack());
    }, Kind::Bulk);
}

void SpotifyApi::get(const QString &pathQuery, Callback callback)
{
    request("GET", QUrl(u"https://api.spotify.com/v1"_s + pathQuery), QByteArray(), callback);
}
