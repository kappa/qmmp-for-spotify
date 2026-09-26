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

SpotifyApi::SpotifyApi(SpotifyAuth *auth, QObject *parent)
    : QObject(parent), m_auth(auth), m_network(new QNetworkAccessManager(this)), m_dataDir(auth->dataDir())
{
    // a past, missing or unreadable block leaves requests open
    QFile file(m_dataDir + u"/ratelimit"_s);
    if(file.open(QIODevice::ReadOnly))
        m_blockedUntil = capBlock(file.readAll().trimmed().toLongLong(), now());
    connect(auth, &SpotifyAuth::loggedOut, this, &SpotifyApi::clearBlock);
    m_budgetClock.start();
}

bool SpotifyApi::isBlocked() const
{
    return ::isBlocked(blockedUntil(), now());
}

// a short block holds the request until it ends, then it passes the gate again; during a long
// one the callback runs on the next event-loop turn: callers may be in the middle of a state change
bool SpotifyApi::gate(const QByteArray &verb, const QUrl &url, const QByteArray &body, const Callback &callback,
                      bool retried, bool bulk)
{
    switch(gateAction(blockedUntil(), now()))
    {
    case GateAction::Send:
        return false;
    case GateAction::Hold:
        QTimer::singleShot(int(blockedUntil() - now()) + kRetryMarginMs, Qt::PreciseTimer, this,
                           [=] { request(verb, url, body, callback, retried, bulk); });
        return true;
    case GateAction::Refuse:
        break;
    }
    logApiRequest(m_dataDir, verb, pathQuery(url), 429, 0, retried ? u"retry blocked"_s : u"blocked"_s);
    ApiResult result;
    result.status = 429;
    result.rateLimited = true;
    result.message = rateLimitMessage(blockedUntil() - now());
    QTimer::singleShot(0, this, [callback, result] { callback(result); });
    return true;
}

// something sends in a loop: all requests stop for a while, through the rate-limit gate
void SpotifyApi::refuseOverBudget(const QByteArray &verb, const QUrl &url, const Callback &callback)
{
    logApiRequest(m_dataDir, verb, pathQuery(url), 0, 0, u"budget"_s);
    m_overrunBlockedUntil = budgetOverrunBlock(m_overrunBlockedUntil, now());
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

void SpotifyApi::request(const QByteArray &verb, const QUrl &url, const QByteArray &body, Callback callback, bool retried,
                         bool bulk)
{
    if(gate(verb, url, body, callback, retried, bulk))
        return;
    m_auth->withToken([=](bool ok) {
        if(!ok)
        {
            ApiResult result;
            result.message = u"Spotify: not logged in. Log in in the Spotify plugin settings"_s;
            callback(result);
            return;
        }
        // a 429 may have arrived while the token was being refreshed
        if(gate(verb, url, body, callback, retried, bulk))
            return;
        // a safety net against request loops; token refreshes do not count. Bulk requests are
        // finite (pages, pasted links) and wait for a slot, through the gate again
        if(!m_budget.take(m_budgetClock.elapsed(), bulk))
        {
            if(bulk)
                QTimer::singleShot(int(m_budget.waitMs(m_budgetClock.elapsed(), true)) + kRetryMarginMs, Qt::PreciseTimer,
                                   this, [=] { request(verb, url, body, callback, retried, bulk); });
            else
                refuseOverBudget(verb, url, callback);
            return;
        }
        QNetworkRequest networkRequest(url);
        networkRequest.setRawHeader("Authorization", "Bearer " + m_auth->accessToken().toUtf8());
        networkRequest.setHeader(QNetworkRequest::ContentLengthHeader, body.size());
        if(!body.isEmpty())
            networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
        networkRequest.setTransferTimeout(15000);
        QElapsedTimer clock;
        clock.start();
        QNetworkReply *reply = m_network->sendCustomRequest(networkRequest, verb, body);
        connect(reply, &QNetworkReply::finished, this, [=] {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray data = reply->readAll();
            const QString networkError = status == 0 ? reply->errorString() : QString();
            const QByteArray retryAfter = reply->rawHeader("Retry-After");
            logApiRequest(m_dataDir, verb, pathQuery(url), status, clock.elapsed(),
                          apiLogNote(status, retryAfter, retried, networkError));
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
                        request(verb, url, body, callback, true, bulk);
                    else
                        callback(ApiResult { false, status, u"Spotify session expired, please log in again"_s, false, data,
                                             QString(), false, false });
                });
                break;
            case ApiDecision::RetryAfter:
                QTimer::singleShot(decision.retryAfterMs + kRetryMarginMs, Qt::PreciseTimer, this,
                                   [=] { request(verb, url, body, callback, true, bulk); });
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
    }, false, true);
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

void SpotifyApi::play(const QString &deviceId, const QStringList &uris, qint64 positionMs, Callback callback)
{
    const QJsonObject body { { u"uris"_s, QJsonArray::fromStringList(uris) }, { u"position_ms"_s, positionMs } };
    request("PUT", endpoint(u"/me/player/play"_s, deviceQuery(deviceId)),
            QJsonDocument(body).toJson(QJsonDocument::Compact), callback);
}

void SpotifyApi::pause(const QString &deviceId, Callback callback)
{
    request("PUT", endpoint(u"/me/player/pause"_s, deviceQuery(deviceId)), QByteArray(), callback);
}

void SpotifyApi::resume(const QString &deviceId, Callback callback)
{
    request("PUT", endpoint(u"/me/player/play"_s, deviceQuery(deviceId)), QByteArray(), callback);
}

void SpotifyApi::seek(const QString &deviceId, qint64 positionMs, Callback callback)
{
    QUrlQuery query = deviceQuery(deviceId);
    query.addQueryItem(u"position_ms"_s, QString::number(positionMs));
    request("PUT", endpoint(u"/me/player/seek"_s, query), QByteArray(), callback);
}

void SpotifyApi::queue(const QString &deviceId, const QString &uri, Callback callback)
{
    QUrlQuery query = deviceQuery(deviceId);
    query.addQueryItem(u"uri"_s, uri);
    request("POST", endpoint(u"/me/player/queue"_s, query), QByteArray(), callback);
}

void SpotifyApi::next(const QString &deviceId, Callback callback)
{
    request("POST", endpoint(u"/me/player/next"_s, deviceQuery(deviceId)), QByteArray(), callback);
}

void SpotifyApi::setVolume(const QString &deviceId, int percent, Callback callback)
{
    QUrlQuery query = deviceQuery(deviceId);
    query.addQueryItem(u"volume_percent"_s, QString::number(percent));
    request("PUT", endpoint(u"/me/player/volume"_s, query), QByteArray(), callback);
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
    }, false, true);
}

void SpotifyApi::track(const QString &trackId, std::function<void(const ApiResult &, const SpotifyTrack &)> callback)
{
    request("GET", endpoint(u"/tracks/"_s + trackId), QByteArray(), [callback](const ApiResult &result) {
        callback(result, result.ok ? SpotifyJson::parseSingleTrack(result.body) : SpotifyTrack());
    }, false, true);
}

void SpotifyApi::get(const QString &pathQuery, Callback callback)
{
    request("GET", QUrl(u"https://api.spotify.com/v1"_s + pathQuery), QByteArray(), callback);
}
