#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include "apipolicy.h"
#include "spotifyapi.h"
#include "spotifyauth.h"
#include "spotifyjson.h"

using namespace Qt::StringLiterals;

namespace {

const int kMaxPages = 200;

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
    : QObject(parent), m_auth(auth), m_network(new QNetworkAccessManager(this))
{}

void SpotifyApi::request(const QByteArray &verb, const QUrl &url, const QByteArray &body, Callback callback, bool retried)
{
    m_auth->withToken([=](bool ok) {
        if(!ok)
        {
            ApiResult result;
            result.message = u"Spotify: not logged in. Log in in the Spotify plugin settings"_s;
            callback(result);
            return;
        }
        QNetworkRequest networkRequest(url);
        networkRequest.setRawHeader("Authorization", "Bearer " + m_auth->accessToken().toUtf8());
        networkRequest.setHeader(QNetworkRequest::ContentLengthHeader, body.size());
        if(!body.isEmpty())
            networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
        networkRequest.setTransferTimeout(15000);
        QNetworkReply *reply = m_network->sendCustomRequest(networkRequest, verb, body);
        connect(reply, &QNetworkReply::finished, this, [=] {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray data = reply->readAll();
            const QString networkError = status == 0 ? reply->errorString() : QString();
            const ApiDecision decision = classifyReply(status, networkError, reply->rawHeader("Retry-After"), data, retried);
            switch(decision.action)
            {
            case ApiDecision::Success:
                callback(ApiResult { true, status, QString(), false, data, QString() });
                break;
            case ApiDecision::RefreshAndRetry:
                m_auth->refresh([=](bool refreshed) {
                    if(refreshed)
                        request(verb, url, body, callback, true);
                    else
                        callback(ApiResult { false, status, u"Spotify session expired, please log in again"_s, false, data, QString() });
                });
                break;
            case ApiDecision::RetryAfter:
                QTimer::singleShot(decision.retryAfterMs, this, [=] { request(verb, url, body, callback, true); });
                break;
            case ApiDecision::Fail:
                callback(ApiResult { false, status, decision.message, decision.deviceProblem, data, decision.reason });
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
    });
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
    });
}

void SpotifyApi::track(const QString &trackId, std::function<void(const ApiResult &, const SpotifyTrack &)> callback)
{
    request("GET", endpoint(u"/tracks/"_s + trackId), QByteArray(), [callback](const ApiResult &result) {
        callback(result, result.ok ? SpotifyJson::parseSingleTrack(result.body) : SpotifyTrack());
    });
}
