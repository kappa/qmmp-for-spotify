#include <QJsonArray>
#include <QJsonDocument>
#include "spotifyjson.h"

using namespace Qt::StringLiterals;

namespace {

QJsonObject toObject(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

template <typename T, typename Parser>
SpotifyPage<T> parsePage(const QJsonObject &object, Parser parseItem)
{
    SpotifyPage<T> page;
    const QJsonArray items = object.value("items"_L1).toArray();
    for(const QJsonValue &value : items)
    {
        const T item = parseItem(value.toObject());
        if(item.isValid())
            page.items << item;
    }
    page.next = object.value("next"_L1).toString();
    return page;
}

}

SpotifyTrack SpotifyJson::parseTrack(const QJsonObject &object, const QString &albumName)
{
    const QString type = object.value("type"_L1).toString(u"track"_s);
    if(type != "track"_L1 || object.value("is_local"_L1).toBool() || !object.value("is_playable"_L1).toBool(true))
        return SpotifyTrack();

    SpotifyTrack track;
    track.id = object.value("id"_L1).toString();
    track.title = object.value("name"_L1).toString();
    const QJsonArray artists = object.value("artists"_L1).toArray();
    for(const QJsonValue &artist : artists)
        track.artists << artist.toObject().value("name"_L1).toString();
    track.album = object.contains("album"_L1) ? object.value("album"_L1).toObject().value("name"_L1).toString() : albumName;
    track.trackNumber = object.value("track_number"_L1).toInt();
    track.durationMs = qint64(object.value("duration_ms"_L1).toDouble());
    return track;
}

SpotifyTrack SpotifyJson::parseSingleTrack(const QByteArray &json)
{
    return parseTrack(toObject(json));
}

QList<SpotifyDevice> SpotifyJson::parseDevices(const QByteArray &json)
{
    QList<SpotifyDevice> devices;
    const QJsonArray items = toObject(json).value("devices"_L1).toArray();
    for(const QJsonValue &value : items)
    {
        const QJsonObject object = value.toObject();
        SpotifyDevice device;
        device.id = object.value("id"_L1).toString();
        device.name = object.value("name"_L1).toString();
        device.type = object.value("type"_L1).toString();
        device.isActive = object.value("is_active"_L1).toBool();
        if(device.isValid())
            devices << device;
    }
    return devices;
}

PlayerState SpotifyJson::parsePlayerState(const QByteArray &json)
{
    PlayerState state;
    const QJsonObject object = toObject(json);
    if(object.isEmpty())
        return state;
    const QJsonObject device = object.value("device"_L1).toObject();
    state.hasPlayback = true;
    state.deviceId = device.value("id"_L1).toString();
    state.deviceName = device.value("name"_L1).toString();
    state.isPlaying = object.value("is_playing"_L1).toBool();
    state.progressMs = qint64(object.value("progress_ms"_L1).toDouble());
    state.track = parseTrack(object.value("item"_L1).toObject());
    return state;
}

SpotifyPage<SpotifyPlaylist> SpotifyJson::parsePlaylists(const QByteArray &json)
{
    return parsePage<SpotifyPlaylist>(toObject(json), [](const QJsonObject &object) {
        SpotifyPlaylist playlist;
        playlist.id = object.value("id"_L1).toString();
        playlist.name = object.value("name"_L1).toString();
        const QString countKey = object.contains("tracks"_L1) ? u"tracks"_s : u"items"_s;
        playlist.trackCount = object.value(countKey).toObject().value("total"_L1).toInt();
        return playlist;
    });
}

SpotifyPage<SpotifyTrack> SpotifyJson::parseTrackItems(const QByteArray &json)
{
    return parsePage<SpotifyTrack>(toObject(json), [](const QJsonObject &entry) {
        const QString key = entry.contains("item"_L1) ? u"item"_s : u"track"_s;
        return parseTrack(entry.value(key).toObject());
    });
}

SpotifyPage<SpotifyTrack> SpotifyJson::parseAlbumTracks(const QByteArray &json, const QString &albumName)
{
    return parsePage<SpotifyTrack>(toObject(json), [&albumName](const QJsonObject &object) {
        return parseTrack(object, albumName);
    });
}

SpotifyPage<SpotifyTrack> SpotifyJson::parseAlbum(const QByteArray &json, QString *albumName)
{
    const QJsonObject object = toObject(json);
    const QString name = object.value("name"_L1).toString();
    if(albumName)
        *albumName = name;
    return parsePage<SpotifyTrack>(object.value("tracks"_L1).toObject(), [&name](const QJsonObject &track) {
        return parseTrack(track, name);
    });
}

QString SpotifyJson::parseDisplayName(const QByteArray &json)
{
    const QJsonObject object = toObject(json);
    const QString name = object.value("display_name"_L1).toString();
    return name.isEmpty() ? object.value("id"_L1).toString() : name;
}
