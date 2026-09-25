#ifndef SPOTIFYJSON_H
#define SPOTIFYJSON_H

#include <QByteArray>
#include <QJsonObject>
#include "spotifytypes.h"

namespace SpotifyJson
{
SpotifyTrack parseTrack(const QJsonObject &object, const QString &albumName = QString());
SpotifyTrack parseSingleTrack(const QByteArray &json);
QList<SpotifyDevice> parseDevices(const QByteArray &json);
PlayerState parsePlayerState(const QByteArray &json);
SpotifyPage<SpotifyPlaylist> parsePlaylists(const QByteArray &json);
SpotifyPage<SpotifyTrack> parseTrackItems(const QByteArray &json);
SpotifyPage<SpotifyTrack> parseAlbumTracks(const QByteArray &json, const QString &albumName);
SpotifyPage<SpotifyTrack> parseAlbum(const QByteArray &json, QString *albumName);
QString parseDisplayName(const QByteArray &json);
QString parseUserId(const QByteArray &json);
}

#endif
