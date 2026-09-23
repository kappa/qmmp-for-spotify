# qmmp-spotify Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** qmmp plugins that control Spotify Connect devices through the Spotify Web API, load Spotify playlists/liked songs into qmmp playlists, accept Spotify links, and follow what the device plays.

**Architecture:** A qmmp-free static library (`spotifylogic`) holds all decision logic (link parsing, JSON parsing, API error policy, engine state machine, follower decisions, poll intervals) and is unit-tested. A shared library (`qmmp-spotify-core`) adds OAuth (Qt NetworkAuth, PKCE), the Web API client and the poller, behind one `SpotifyClient` singleton. Three thin qmmp plugins (Transport, Engine, General) glue that into qmmp. A small `spotify-cli` tool exercises login and the API without qmmp.

**Tech Stack:** C++17, Qt 6 (Core, Gui, Network, NetworkAuth, Widgets, Test), CMake ≥ 3.18, qmmp 2.4.2 via pkg-config.

**Spec:** `docs/superpowers/specs/2026-09-22-qmmp-spotify-design.md`

## Global Constraints

- Qt 6 (dev machine has 6.11.2), C++17, CMake ≥ 3.18.
- Target qmmp 2.4.2, built from source by the user; found via pkg-config (`qmmp`, `qmmpui`); plugins install into its `plugindir`. The distro's qmmp is not a target.
- Web API only. No librespot, no search, no playlist editing, qmmp is not a Connect target.
- Nothing hardcoded: the client ID comes from settings (or `SPOTIFY_CLIENT_ID` for the CLI). No client secret; PKCE.
- Redirect URI `http://127.0.0.1:<port>/callback`, default port 8898.
- Refresh token stored in `<qmmp config dir>/spotify/token.json`, mode 0600.
- Canonical playlist path for a track: `spotify://track/<id>`.
- Polling `GET /me/player`: 2 s while active, 10 s idle, exponential backoff to 30 s on errors.
- Tests are minimal: verify behaviour, no `// Arrange` style comments, fake only what is checked.
- `qmmp-2.4.2/` and `ncspot-1.4.0/` are reference only and never committed (already in `.gitignore`).
- Commit only after the task's tests (and manual checks, for plugin tasks) pass.

## Review Focus

1. A poll that was in flight when the user pressed pause arrives afterwards saying "playing" — qmmp must not flip back to Playing (Task 4 `pauseIgnoresStalePoll`).
2. The next track was queued but the device did not advance to it (queue request lost) — the engine must start it explicitly when the current track ends (Task 4 `startsNextWhenDeviceDidNotAdvance`).
3. The user starts a track in qmmp while the device still reports the previous one for up to 2 s — the follower must not "follow" the old track and fight the user (Task 5 `ignoresWhileEngineAwaiting`).
4. Links pasted from the browser carry `?si=` tracking queries and `intl-xx/` prefixes (Task 1 `parse` rows "web with query", "web intl").
5. Playlists contain local files, podcast episodes and removed tracks (`"track": null`) — they must be skipped without crashing (Task 2 `trackItemsSkipUnplayable`).

---

## File Structure

```
CMakeLists.txt                     top level; logic+tests always, plugins only when qmmp is found
src/logic/                         static lib, Qt Core only, no qmmp, no network
  spotifylink.{h,cpp}              parse/format spotify:// , spotify: , open.spotify.com links
  spotifytypes.h                   SpotifyTrack, SpotifyDevice, SpotifyPlaylist, PlayerState, SpotifyPage
  spotifyjson.{h,cpp}              Web API JSON → types
  apipolicy.{h,cpp}                HTTP reply → success / refresh / retry-after / fail(message)
  enginelogic.{h,cpp}              engine state machine behind an EngineSink interface
  followerlogic.{h,cpp}            follower decision + "needs expansion" check
  pollpolicy.{h,cpp}               poll interval
src/core/                          shared lib qmmp-spotify-core
  spotifyauth.{h,cpp}              OAuth PKCE login, refresh, token file
  spotifyapi.{h,cpp}               Web API calls with retry and paging
  spotifypoller.{h,cpp}            GET /me/player polling
  spotifyclient.{h,cpp}            singleton: settings, auth, api, poller, engine tracks, notices
src/transport/                     qmmp Transports plugin: accept Spotify links
src/engine/                        qmmp Engines plugin: control the device
src/general/                       qmmp General plugin: menu, dialogs, follower, link expansion
tools/spotify-cli.cpp              dev tool: login and API calls from the shell
tests/                             Qt Test executables for src/logic
```

---

### Task 1: Project scaffold and link parsing

**Files:**
- Create: `CMakeLists.txt`, `src/logic/CMakeLists.txt`, `src/logic/spotifylink.h`, `src/logic/spotifylink.cpp`, `tests/CMakeLists.txt`, `tests/tst_spotifylink.cpp`

**Interfaces:**
- Produces: `struct SpotifyLink { enum Kind { Invalid, Track, Album, Playlist }; Kind kind; QString id; bool isValid() const; QString uri() const; QString path() const; static SpotifyLink parse(const QString &); static QString trackPath(const QString &id); }`; CMake targets `spotifylogic` (static) and the `spotify_test(<name>)` CMake function.

- [ ] **Step 1: Write the build files**

`CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.18)
project(qmmp-spotify VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Network NetworkAuth Widgets Test)

enable_testing()
add_subdirectory(src/logic)
add_subdirectory(tests)
```

`src/logic/CMakeLists.txt`:
```cmake
add_library(spotifylogic STATIC
    spotifylink.cpp
)
target_include_directories(spotifylogic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(spotifylogic PUBLIC Qt6::Core)
```

`tests/CMakeLists.txt`:
```cmake
function(spotify_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE spotifylogic Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

spotify_test(tst_spotifylink)
```

- [ ] **Step 2: Write the failing test**

`tests/tst_spotifylink.cpp`:
```cpp
#include <QtTest>
#include "spotifylink.h"

using namespace Qt::StringLiterals;

class TestSpotifyLink : public QObject
{
    Q_OBJECT
private slots:
    void parse_data();
    void parse();
    void rejects_data();
    void rejects();
    void formats();
};

void TestSpotifyLink::parse_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("id");
    QTest::newRow("qmmp path") << u"spotify://track/4uLU6hMCjMI75M1A2tKUQC"_s << int(SpotifyLink::Track) << u"4uLU6hMCjMI75M1A2tKUQC"_s;
    QTest::newRow("uri") << u"spotify:album:1DFixLWuPkv3KT3TnV35m3"_s << int(SpotifyLink::Album) << u"1DFixLWuPkv3KT3TnV35m3"_s;
    QTest::newRow("web") << u"https://open.spotify.com/playlist/37i9dQZF1DXcBWIGoYBM5M"_s << int(SpotifyLink::Playlist) << u"37i9dQZF1DXcBWIGoYBM5M"_s;
    QTest::newRow("web with query") << u"https://open.spotify.com/track/4uLU6hMCjMI75M1A2tKUQC?si=a1b2c3"_s << int(SpotifyLink::Track) << u"4uLU6hMCjMI75M1A2tKUQC"_s;
    QTest::newRow("web intl") << u"https://open.spotify.com/intl-de/album/1DFixLWuPkv3KT3TnV35m3"_s << int(SpotifyLink::Album) << u"1DFixLWuPkv3KT3TnV35m3"_s;
    QTest::newRow("surrounding spaces") << u"  spotify:track:4uLU6hMCjMI75M1A2tKUQC\n"_s << int(SpotifyLink::Track) << u"4uLU6hMCjMI75M1A2tKUQC"_s;
}

void TestSpotifyLink::parse()
{
    QFETCH(QString, text);
    QFETCH(int, kind);
    QFETCH(QString, id);
    const SpotifyLink link = SpotifyLink::parse(text);
    QCOMPARE(int(link.kind), kind);
    QCOMPARE(link.id, id);
}

void TestSpotifyLink::rejects_data()
{
    QTest::addColumn<QString>("text");
    QTest::newRow("artist") << u"spotify:artist:0OdUWJ0sBjDrqHygGUXeCF"_s;
    QTest::newRow("other host") << u"https://example.com/track/4uLU6hMCjMI75M1A2tKUQC"_s;
    QTest::newRow("missing id") << u"https://open.spotify.com/track/"_s;
    QTest::newRow("local file") << u"file:///music/a.mp3"_s;
    QTest::newRow("empty") << QString();
}

void TestSpotifyLink::rejects()
{
    QFETCH(QString, text);
    QVERIFY(!SpotifyLink::parse(text).isValid());
}

void TestSpotifyLink::formats()
{
    const SpotifyLink link = SpotifyLink::parse(u"https://open.spotify.com/track/abc?si=x"_s);
    QCOMPARE(link.uri(), u"spotify:track:abc"_s);
    QCOMPARE(link.path(), u"spotify://track/abc"_s);
    QCOMPARE(SpotifyLink::trackPath(u"abc"_s), u"spotify://track/abc"_s);
}

QTEST_APPLESS_MAIN(TestSpotifyLink)
#include "tst_spotifylink.moc"
```

- [ ] **Step 3: Write the header and an empty implementation, run the test to see it fail**

`src/logic/spotifylink.h`:
```cpp
#ifndef SPOTIFYLINK_H
#define SPOTIFYLINK_H

#include <QString>

struct SpotifyLink
{
    enum Kind { Invalid, Track, Album, Playlist };

    Kind kind = Invalid;
    QString id;

    bool isValid() const { return kind != Invalid; }
    QString uri() const;
    QString path() const;

    static SpotifyLink parse(const QString &text);
    static QString trackPath(const QString &id);
};

#endif
```

`src/logic/spotifylink.cpp` (temporary):
```cpp
#include "spotifylink.h"

QString SpotifyLink::uri() const { return QString(); }
QString SpotifyLink::path() const { return QString(); }
QString SpotifyLink::trackPath(const QString &) { return QString(); }
SpotifyLink SpotifyLink::parse(const QString &) { return SpotifyLink(); }
```

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: `tst_spotifylink` FAILS (`parse(qmmp path)` compares kind 0 with 1).

- [ ] **Step 4: Implement**

`src/logic/spotifylink.cpp`:
```cpp
#include <QRegularExpression>
#include "spotifylink.h"

using namespace Qt::StringLiterals;

namespace {

QString kindName(SpotifyLink::Kind kind)
{
    switch(kind)
    {
    case SpotifyLink::Track: return u"track"_s;
    case SpotifyLink::Album: return u"album"_s;
    case SpotifyLink::Playlist: return u"playlist"_s;
    default: return QString();
    }
}

SpotifyLink::Kind kindFromName(const QString &name)
{
    if(name == "track"_L1)
        return SpotifyLink::Track;
    if(name == "album"_L1)
        return SpotifyLink::Album;
    if(name == "playlist"_L1)
        return SpotifyLink::Playlist;
    return SpotifyLink::Invalid;
}

}

QString SpotifyLink::uri() const
{
    return u"spotify:%1:%2"_s.arg(kindName(kind), id);
}

QString SpotifyLink::path() const
{
    return u"spotify://%1/%2"_s.arg(kindName(kind), id);
}

QString SpotifyLink::trackPath(const QString &id)
{
    return u"spotify://track/"_s + id;
}

SpotifyLink SpotifyLink::parse(const QString &text)
{
    static const QRegularExpression patterns[] = {
        QRegularExpression(u"^spotify://(track|album|playlist)/([A-Za-z0-9]+)/?$"_s),
        QRegularExpression(u"^spotify:(track|album|playlist):([A-Za-z0-9]+)$"_s),
        QRegularExpression(u"^https?://open\\.spotify\\.com/(?:intl-[A-Za-z-]+/)?(track|album|playlist)/([A-Za-z0-9]+)(?:[/?#].*)?$"_s),
    };
    const QString trimmed = text.trimmed();
    for(const QRegularExpression &pattern : patterns)
    {
        const QRegularExpressionMatch match = pattern.match(trimmed);
        if(match.hasMatch())
            return SpotifyLink { kindFromName(match.captured(1)), match.captured(2) };
    }
    return SpotifyLink();
}
```

- [ ] **Step 5: Run the tests**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: `tst_spotifylink` PASSES.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/logic tests
git commit -m "Add build scaffold and Spotify link parsing"
```

---

### Task 2: Types and Web API JSON parsing

**Files:**
- Create: `src/logic/spotifytypes.h`, `src/logic/spotifyjson.h`, `src/logic/spotifyjson.cpp`, `tests/tst_spotifyjson.cpp`
- Modify: `src/logic/CMakeLists.txt` (add `spotifyjson.cpp`), `tests/CMakeLists.txt` (add `spotify_test(tst_spotifyjson)`)

**Interfaces:**
- Produces (used by every later task):
```cpp
struct SpotifyTrack { QString id; QString title; QStringList artists; QString album; int trackNumber = 0; qint64 durationMs = 0; bool isValid() const; };
struct SpotifyDevice { QString id; QString name; QString type; bool isActive = false; bool isValid() const; };
struct SpotifyPlaylist { QString id; QString name; int trackCount = 0; bool isValid() const; };
struct PlayerState { bool hasPlayback = false; QString deviceId; QString deviceName; bool isPlaying = false; qint64 progressMs = 0; SpotifyTrack track; };
template <typename T> struct SpotifyPage { QList<T> items; QString next; };
namespace SpotifyJson {
  SpotifyTrack parseTrack(const QJsonObject &object, const QString &albumName = QString());
  SpotifyTrack parseSingleTrack(const QByteArray &json);
  QList<SpotifyDevice> parseDevices(const QByteArray &json);
  PlayerState parsePlayerState(const QByteArray &json);
  SpotifyPage<SpotifyPlaylist> parsePlaylists(const QByteArray &json);
  SpotifyPage<SpotifyTrack> parseTrackItems(const QByteArray &json);
  SpotifyPage<SpotifyTrack> parseAlbumTracks(const QByteArray &json, const QString &albumName);
  SpotifyPage<SpotifyTrack> parseAlbum(const QByteArray &json, QString *albumName);
  QString parseDisplayName(const QByteArray &json);
}
```

- [ ] **Step 1: Write the failing test**

Add `spotifyjson.cpp` to `spotifylogic` sources and `spotify_test(tst_spotifyjson)` to `tests/CMakeLists.txt`.

`tests/tst_spotifyjson.cpp` (the JSON follows the shapes documented at developer.spotify.com; `playlist items` entries appear both with the old `"track"` key and the Feb 2026 `"item"` key):
```cpp
#include <QtTest>
#include "spotifyjson.h"

using namespace Qt::StringLiterals;

class TestSpotifyJson : public QObject
{
    Q_OBJECT
private slots:
    void devices();
    void playerStatePlaying();
    void playerStateNothingPlaying();
    void playerStateEpisode();
    void playlists();
    void trackItemsSkipUnplayable();
    void album();
    void displayName();
};

void TestSpotifyJson::devices()
{
    const QList<SpotifyDevice> devices = SpotifyJson::parseDevices(R"({"devices":[
        {"id":"d1","is_active":true,"name":"Kitchen","type":"Speaker"},
        {"id":null,"is_active":false,"name":"Restricted","type":"Computer"},
        {"id":"d2","is_active":false,"name":"Phone","type":"Smartphone"}]})");
    QCOMPARE(devices.size(), 2);
    QCOMPARE(devices.at(0).name, u"Kitchen"_s);
    QVERIFY(devices.at(0).isActive);
    QCOMPARE(devices.at(1).id, u"d2"_s);
}

void TestSpotifyJson::playerStatePlaying()
{
    const PlayerState s = SpotifyJson::parsePlayerState(R"({
        "device":{"id":"d1","name":"Kitchen"},"is_playing":true,"progress_ms":42000,
        "currently_playing_type":"track",
        "item":{"type":"track","id":"t1","name":"Song","duration_ms":200000,"track_number":3,
                "artists":[{"name":"A"},{"name":"B"}],"album":{"name":"LP"}}})");
    QVERIFY(s.hasPlayback);
    QCOMPARE(s.deviceId, u"d1"_s);
    QVERIFY(s.isPlaying);
    QCOMPARE(s.progressMs, qint64(42000));
    QCOMPARE(s.track.id, u"t1"_s);
    QCOMPARE(s.track.artists, (QStringList { u"A"_s, u"B"_s }));
    QCOMPARE(s.track.album, u"LP"_s);
    QCOMPARE(s.track.trackNumber, 3);
    QCOMPARE(s.track.durationMs, qint64(200000));
}

void TestSpotifyJson::playerStateNothingPlaying()
{
    QVERIFY(!SpotifyJson::parsePlayerState(QByteArray()).hasPlayback);
}

void TestSpotifyJson::playerStateEpisode()
{
    const PlayerState s = SpotifyJson::parsePlayerState(R"({
        "device":{"id":"d1","name":"Kitchen"},"is_playing":true,"progress_ms":1,
        "item":{"type":"episode","id":"e1","name":"Podcast"}})");
    QVERIFY(s.hasPlayback);
    QVERIFY(!s.track.isValid());
}

void TestSpotifyJson::playlists()
{
    const SpotifyPage<SpotifyPlaylist> page = SpotifyJson::parsePlaylists(R"({
        "items":[{"id":"p1","name":"Old style","tracks":{"total":12}},
                 {"id":"p2","name":"New style","items":{"total":7}}],
        "next":"https://api.spotify.com/v1/me/playlists?offset=50&limit=50"})");
    QCOMPARE(page.items.size(), 2);
    QCOMPARE(page.items.at(0).trackCount, 12);
    QCOMPARE(page.items.at(1).trackCount, 7);
    QCOMPARE(page.next, u"https://api.spotify.com/v1/me/playlists?offset=50&limit=50"_s);
}

void TestSpotifyJson::trackItemsSkipUnplayable()
{
    const SpotifyPage<SpotifyTrack> page = SpotifyJson::parseTrackItems(R"({"items":[
        {"track":{"type":"track","id":"t1","name":"One","duration_ms":1000,"artists":[],"album":{"name":"X"}}},
        {"item":{"type":"track","id":"t2","name":"Two","duration_ms":2000,"artists":[],"album":{"name":"Y"}}},
        {"track":{"type":"track","id":null,"name":"Local","is_local":true}},
        {"track":{"type":"episode","id":"e1","name":"Episode"}},
        {"track":null},
        {"track":{"type":"track","id":"t3","name":"Gone","is_playable":false}}],
        "next":null})");
    QCOMPARE(page.items.size(), 2);
    QCOMPARE(page.items.at(0).id, u"t1"_s);
    QCOMPARE(page.items.at(1).album, u"Y"_s);
    QVERIFY(page.next.isEmpty());
}

void TestSpotifyJson::album()
{
    QString name;
    const SpotifyPage<SpotifyTrack> page = SpotifyJson::parseAlbum(R"({"name":"LP","tracks":{
        "items":[{"type":"track","id":"t1","name":"One","track_number":1,"duration_ms":1000,"artists":[{"name":"A"}]}],
        "next":"https://api.spotify.com/v1/albums/a1/tracks?offset=50"}})", &name);
    QCOMPARE(name, u"LP"_s);
    QCOMPARE(page.items.size(), 1);
    QCOMPARE(page.items.at(0).album, u"LP"_s);
    QCOMPARE(page.next, u"https://api.spotify.com/v1/albums/a1/tracks?offset=50"_s);
}

void TestSpotifyJson::displayName()
{
    QCOMPARE(SpotifyJson::parseDisplayName(R"({"id":"alex123","display_name":"Alex"})"), u"Alex"_s);
    QCOMPARE(SpotifyJson::parseDisplayName(R"({"id":"alex123","display_name":null})"), u"alex123"_s);
}

QTEST_APPLESS_MAIN(TestSpotifyJson)
#include "tst_spotifyjson.moc"
```

- [ ] **Step 2: Write the types and header, stub the functions, run to see it fail**

`src/logic/spotifytypes.h`:
```cpp
#ifndef SPOTIFYTYPES_H
#define SPOTIFYTYPES_H

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

struct SpotifyTrack
{
    QString id;
    QString title;
    QStringList artists;
    QString album;
    int trackNumber = 0;
    qint64 durationMs = 0;

    bool isValid() const { return !id.isEmpty(); }
};

struct SpotifyDevice
{
    QString id;
    QString name;
    QString type;
    bool isActive = false;

    bool isValid() const { return !id.isEmpty(); }
};

struct SpotifyPlaylist
{
    QString id;
    QString name;
    int trackCount = 0;

    bool isValid() const { return !id.isEmpty(); }
};

struct PlayerState
{
    bool hasPlayback = false;
    QString deviceId;
    QString deviceName;
    bool isPlaying = false;
    qint64 progressMs = 0;
    SpotifyTrack track;
};

template <typename T>
struct SpotifyPage
{
    QList<T> items;
    QString next;
};

Q_DECLARE_METATYPE(PlayerState)

#endif
```

`src/logic/spotifyjson.h`:
```cpp
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
}

#endif
```

Temporary `src/logic/spotifyjson.cpp`:
```cpp
#include "spotifyjson.h"

SpotifyTrack SpotifyJson::parseTrack(const QJsonObject &, const QString &) { return {}; }
SpotifyTrack SpotifyJson::parseSingleTrack(const QByteArray &) { return {}; }
QList<SpotifyDevice> SpotifyJson::parseDevices(const QByteArray &) { return {}; }
PlayerState SpotifyJson::parsePlayerState(const QByteArray &) { return {}; }
SpotifyPage<SpotifyPlaylist> SpotifyJson::parsePlaylists(const QByteArray &) { return {}; }
SpotifyPage<SpotifyTrack> SpotifyJson::parseTrackItems(const QByteArray &) { return {}; }
SpotifyPage<SpotifyTrack> SpotifyJson::parseAlbumTracks(const QByteArray &, const QString &) { return {}; }
SpotifyPage<SpotifyTrack> SpotifyJson::parseAlbum(const QByteArray &, QString *) { return {}; }
QString SpotifyJson::parseDisplayName(const QByteArray &) { return {}; }
```

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R tst_spotifyjson`
Expected: FAIL in `devices` (0 vs 2).

- [ ] **Step 3: Implement**

`src/logic/spotifyjson.cpp`:
```cpp
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
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add src/logic tests
git commit -m "Add Spotify types and Web API JSON parsing"
```

---

### Task 3: API reply policy

**Files:**
- Create: `src/logic/apipolicy.h`, `src/logic/apipolicy.cpp`, `tests/tst_apipolicy.cpp`
- Modify: `src/logic/CMakeLists.txt` (add `apipolicy.cpp`), `tests/CMakeLists.txt` (add `spotify_test(tst_apipolicy)`)

**Interfaces:**
- Produces:
```cpp
struct ApiDecision { enum Action { Success, RefreshAndRetry, RetryAfter, Fail }; Action action = Fail; int retryAfterMs = 0; QString message; bool deviceProblem = false; };
ApiDecision classifyReply(int httpStatus, const QString &networkError, const QByteArray &retryAfter, const QByteArray &body, bool retried);
```

- [ ] **Step 1: Write the failing test**

`tests/tst_apipolicy.cpp`:
```cpp
#include <QtTest>
#include "apipolicy.h"

using namespace Qt::StringLiterals;

class TestApiPolicy : public QObject
{
    Q_OBJECT
private slots:
    void success();
    void unauthorizedRefreshesOnce();
    void rateLimitHonoursRetryAfter();
    void noActiveDevice();
    void passesSpotifyMessage();
    void networkError();
};

void TestApiPolicy::success()
{
    QCOMPARE(classifyReply(204, QString(), QByteArray(), QByteArray(), false).action, ApiDecision::Success);
}

void TestApiPolicy::unauthorizedRefreshesOnce()
{
    QCOMPARE(classifyReply(401, QString(), QByteArray(), QByteArray(), false).action, ApiDecision::RefreshAndRetry);
    const ApiDecision second = classifyReply(401, QString(), QByteArray(), QByteArray(), true);
    QCOMPARE(second.action, ApiDecision::Fail);
    QVERIFY(second.message.contains(u"log in again"_s));
}

void TestApiPolicy::rateLimitHonoursRetryAfter()
{
    const ApiDecision withHeader = classifyReply(429, QString(), "3", QByteArray(), false);
    QCOMPARE(withHeader.action, ApiDecision::RetryAfter);
    QCOMPARE(withHeader.retryAfterMs, 3000);
    QCOMPARE(classifyReply(429, QString(), QByteArray(), QByteArray(), false).retryAfterMs, 5000);
    QCOMPARE(classifyReply(429, QString(), "3", QByteArray(), true).action, ApiDecision::Fail);
}

void TestApiPolicy::noActiveDevice()
{
    const ApiDecision d = classifyReply(404, QString(), QByteArray(),
        R"({"error":{"status":404,"message":"Player command failed: No active device found","reason":"NO_ACTIVE_DEVICE"}})", false);
    QCOMPARE(d.action, ApiDecision::Fail);
    QVERIFY(d.deviceProblem);
    QVERIFY(d.message.contains(u"Device"_s));
}

void TestApiPolicy::passesSpotifyMessage()
{
    const ApiDecision d = classifyReply(403, QString(), QByteArray(),
        R"({"error":{"status":403,"message":"Player command failed: Premium required","reason":"PREMIUM_REQUIRED"}})", false);
    QCOMPARE(d.message, u"Spotify: Player command failed: Premium required"_s);
    QVERIFY(!d.deviceProblem);
}

void TestApiPolicy::networkError()
{
    const ApiDecision d = classifyReply(0, u"Host not found"_s, QByteArray(), QByteArray(), false);
    QCOMPARE(d.action, ApiDecision::Fail);
    QVERIFY(d.message.contains(u"Host not found"_s));
}

QTEST_APPLESS_MAIN(TestApiPolicy)
#include "tst_apipolicy.moc"
```

- [ ] **Step 2: Header and stub, run to see it fail**

`src/logic/apipolicy.h`:
```cpp
#ifndef APIPOLICY_H
#define APIPOLICY_H

#include <QByteArray>
#include <QString>

struct ApiDecision
{
    enum Action { Success, RefreshAndRetry, RetryAfter, Fail };

    Action action = Fail;
    int retryAfterMs = 0;
    QString message;
    bool deviceProblem = false;
};

ApiDecision classifyReply(int httpStatus, const QString &networkError, const QByteArray &retryAfter,
                          const QByteArray &body, bool retried);

#endif
```

Temporary `src/logic/apipolicy.cpp`:
```cpp
#include "apipolicy.h"

ApiDecision classifyReply(int, const QString &, const QByteArray &, const QByteArray &, bool)
{
    return ApiDecision();
}
```

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R tst_apipolicy`
Expected: FAIL in `success`.

- [ ] **Step 3: Implement**

`src/logic/apipolicy.cpp`:
```cpp
#include <QJsonDocument>
#include <QJsonObject>
#include "apipolicy.h"

using namespace Qt::StringLiterals;

ApiDecision classifyReply(int httpStatus, const QString &networkError, const QByteArray &retryAfter,
                          const QByteArray &body, bool retried)
{
    ApiDecision decision;
    if(httpStatus == 0)
    {
        decision.message = u"Spotify: network error: %1"_s.arg(networkError.isEmpty() ? u"no response"_s : networkError);
        return decision;
    }
    if(httpStatus >= 200 && httpStatus < 300)
    {
        decision.action = ApiDecision::Success;
        return decision;
    }
    if(httpStatus == 401 && !retried)
    {
        decision.action = ApiDecision::RefreshAndRetry;
        return decision;
    }
    if(httpStatus == 429 && !retried)
    {
        bool ok = false;
        const int seconds = retryAfter.trimmed().toInt(&ok);
        decision.action = ApiDecision::RetryAfter;
        decision.retryAfterMs = (ok && seconds >= 0 ? seconds : 5) * 1000;
        return decision;
    }

    const QJsonObject error = QJsonDocument::fromJson(body).object().value("error"_L1).toObject();
    const QString message = error.value("message"_L1).toString();
    const QString reason = error.value("reason"_L1).toString();
    if(reason == "NO_ACTIVE_DEVICE"_L1 || message.contains("device"_L1, Qt::CaseInsensitive))
    {
        decision.deviceProblem = true;
        decision.message = u"Spotify: the selected device is not available. Pick one in Tools → Spotify → Device…"_s;
    }
    else if(httpStatus == 401)
        decision.message = u"Spotify: authorization failed, please log in again"_s;
    else if(httpStatus == 429)
        decision.message = u"Spotify: rate limit exceeded, try again later"_s;
    else if(!message.isEmpty())
        decision.message = u"Spotify: "_s + message;
    else
        decision.message = u"Spotify: request failed (HTTP %1)"_s.arg(httpStatus);
    return decision;
}
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add src/logic tests
git commit -m "Add Web API reply policy"
```

---

### Task 4: Engine logic

The engine's behaviour, independent of qmmp. The qmmp glue (Task 11) implements `EngineSink` with API calls and `StateHandler` dispatches.

Facts from qmmp that shape this (verified in `qmmp-2.4.2/src`):
- `StateHandler::dispatch(elapsed, bitrate)` itself posts the "next track request" 7 s before the end (`qmmp/statehandler.cpp:69-91`), after which qmmp calls `enqueue()` with the next track. So the logic queues the next track when it is enqueued.
- The internal engine switches tracks gaplessly with `sendFinished()`, then a fake `Stopped` → `Buffering` → `Playing` cycle (`qmmp/qmmpaudioengine.cpp:415-431`); `Stopped` also re-arms the next-track request. The logic does the same.
- At the end of a track with nothing queued, qmmp needs `sendFinished()` and then `Stopped`, so `SoundCore` can move to another engine (`qmmp/soundcore.cpp:395`).

**Files:**
- Create: `src/logic/enginelogic.h`, `src/logic/enginelogic.cpp`, `tests/tst_enginelogic.cpp`
- Modify: `src/logic/CMakeLists.txt` (add `enginelogic.cpp`), `tests/CMakeLists.txt` (add `spotify_test(tst_enginelogic)`)

**Interfaces:**
- Consumes: `SpotifyLink` (Task 1), `PlayerState`, `SpotifyTrack` (Task 2).
- Produces:
```cpp
enum class EngineState { Stopped, Buffering, Playing, Paused };
class EngineSink {  // implemented by SpotifyEngine (Task 11)
    virtual void apiPlay(const QString &uri, qint64 positionMs) = 0;
    virtual void apiPause() = 0;
    virtual void apiResume() = 0;
    virtual void apiSeek(qint64 positionMs) = 0;
    virtual void apiQueue(const QString &uri) = 0;
    virtual void reportState(EngineState state) = 0;
    virtual void reportTrack(const QString &path, const SpotifyTrack &track) = 0;
    virtual void reportFinished() = 0;
    virtual void reportError(const QString &message, bool fatal) = 0;
};
class EngineLogic {
    using Clock = std::function<qint64()>;
    EngineLogic(EngineSink *sink, Clock clock);
    void setSelectedDevice(const QString &deviceId);
    bool enqueue(const QString &path, qint64 offsetMs = 0);
    void play(); void pause(); void seek(qint64 positionMs); void stop();
    void updateState(const PlayerState &state);
    void playFailed(const QString &message, bool fatal);
    EngineState state() const; qint64 elapsed() const; QStringList trackIds() const; bool isAwaitingStart() const;
};
```

- [ ] **Step 1: Write the failing test**

`tests/tst_enginelogic.cpp`:
```cpp
#include <QtTest>
#include "enginelogic.h"

using namespace Qt::StringLiterals;

namespace {

class FakeSink : public EngineSink
{
public:
    QStringList calls;

    void apiPlay(const QString &uri, qint64 positionMs) override { calls << u"play %1 %2"_s.arg(uri).arg(positionMs); }
    void apiPause() override { calls << u"pause"_s; }
    void apiResume() override { calls << u"resume"_s; }
    void apiSeek(qint64 positionMs) override { calls << u"seek %1"_s.arg(positionMs); }
    void apiQueue(const QString &uri) override { calls << u"queue "_s + uri; }
    void reportState(EngineState state) override
    {
        static const QStringList names = { u"Stopped"_s, u"Buffering"_s, u"Playing"_s, u"Paused"_s };
        calls << u"state "_s + names.at(int(state));
    }
    void reportTrack(const QString &path, const SpotifyTrack &track) override { calls << u"track %1 %2"_s.arg(path, track.title); }
    void reportFinished() override { calls << u"finished"_s; }
    void reportError(const QString &, bool fatal) override { calls << (fatal ? u"fatal"_s : u"error"_s); }
};

PlayerState deviceState(const QString &device, const QString &trackId, qint64 progressMs, bool isPlaying = true)
{
    PlayerState state;
    state.hasPlayback = true;
    state.deviceId = device;
    state.isPlaying = isPlaying;
    state.progressMs = progressMs;
    state.track.id = trackId;
    state.track.title = u"Title "_s + trackId;
    state.track.durationMs = 200000;
    return state;
}

}

class TestEngineLogic : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void playWithoutDeviceIsFatal();
    void adoptsTrackAlreadyPlaying();
    void startsTrackAndWaitsForDevice();
    void followsRemotePause();
    void pauseIgnoresStalePoll();
    void queuesNextAndSwitchesGaplessly();
    void startsNextWhenDeviceDidNotAdvance();
    void finishesWhenTrackEnds();
    void stopPausesOwnTrack();
    void stopLeavesForeignTrackAlone();
    void ignoresOtherDevices();
    void interpolatesElapsed();
    void playFailureClearsTracks();

private:
    void startPlaying(const QString &trackId);

    qint64 m_now = 0;
    FakeSink m_sink;
    EngineLogic *m_logic = nullptr;
};

void TestEngineLogic::init()
{
    m_now = 1000;
    m_sink.calls.clear();
    m_logic = new EngineLogic(&m_sink, [this] { return m_now; });
    m_logic->setSelectedDevice(u"dev"_s);
}

void TestEngineLogic::cleanup()
{
    delete m_logic;
}

void TestEngineLogic::startPlaying(const QString &trackId)
{
    m_logic->updateState(deviceState(u"dev"_s, trackId, 0));
    QVERIFY(m_logic->enqueue(u"spotify://track/"_s + trackId));
    m_logic->play();
    QCOMPARE(m_logic->state(), EngineState::Playing);
    m_sink.calls.clear();
}

void TestEngineLogic::playWithoutDeviceIsFatal()
{
    m_logic->setSelectedDevice(QString());
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    QCOMPARE(m_sink.calls, QStringList { u"fatal"_s });
}

void TestEngineLogic::adoptsTrackAlreadyPlaying()
{
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 5000));
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    QCOMPARE(m_sink.calls, (QStringList { u"state Playing"_s, u"track spotify://track/a Title a"_s }));
}

void TestEngineLogic::startsTrackAndWaitsForDevice()
{
    m_logic->updateState(deviceState(u"dev"_s, u"old"_s, 1000));
    QVERIFY(m_logic->enqueue(u"https://open.spotify.com/track/a?si=x"_s));
    m_logic->play();
    m_logic->updateState(deviceState(u"dev"_s, u"old"_s, 3000));
    QCOMPARE(m_sink.calls, (QStringList { u"state Buffering"_s, u"play spotify:track:a 0"_s }));
    QVERIFY(m_logic->isAwaitingStart());

    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 500));
    QCOMPARE(m_sink.calls.mid(2), (QStringList { u"state Playing"_s, u"track https://open.spotify.com/track/a?si=x Title a"_s }));
    QVERIFY(!m_logic->isAwaitingStart());
}

void TestEngineLogic::followsRemotePause()
{
    startPlaying(u"a"_s);
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 8000, false));
    QCOMPARE(m_sink.calls, QStringList { u"state Paused"_s });
    m_logic->pause();
    QCOMPARE(m_sink.calls.mid(1), (QStringList { u"resume"_s, u"state Playing"_s }));
}

void TestEngineLogic::pauseIgnoresStalePoll()
{
    startPlaying(u"a"_s);
    m_now += 1000;
    m_logic->pause();
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 1000, true));
    QCOMPARE(m_logic->state(), EngineState::Paused);
    m_now += 3000;
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 1000, false));
    QCOMPARE(m_sink.calls, (QStringList { u"pause"_s, u"state Paused"_s }));
}

void TestEngineLogic::queuesNextAndSwitchesGaplessly()
{
    startPlaying(u"a"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    QCOMPARE(m_logic->trackIds(), (QStringList { u"a"_s, u"b"_s }));
    m_logic->updateState(deviceState(u"dev"_s, u"b"_s, 300));
    QCOMPARE(m_sink.calls, (QStringList { u"queue spotify:track:b"_s, u"finished"_s, u"state Stopped"_s,
                                          u"state Buffering"_s, u"state Playing"_s, u"track spotify://track/b Title b"_s }));
    QCOMPARE(m_logic->trackIds(), QStringList { u"b"_s });
}

void TestEngineLogic::startsNextWhenDeviceDidNotAdvance()
{
    startPlaying(u"a"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_now += 199000;
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 0, false));
    QCOMPARE(m_sink.calls, (QStringList { u"queue spotify:track:b"_s, u"finished"_s, u"state Stopped"_s,
                                          u"state Buffering"_s, u"play spotify:track:b 0"_s }));
    QVERIFY(m_logic->isAwaitingStart());
}

void TestEngineLogic::finishesWhenTrackEnds()
{
    startPlaying(u"a"_s);
    m_now += 198500;
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 0, false));
    QCOMPARE(m_sink.calls, (QStringList { u"finished"_s, u"state Stopped"_s }));
    QVERIFY(m_logic->trackIds().isEmpty());
}

void TestEngineLogic::stopPausesOwnTrack()
{
    startPlaying(u"a"_s);
    m_logic->stop();
    QCOMPARE(m_sink.calls, (QStringList { u"pause"_s, u"state Stopped"_s }));
}

void TestEngineLogic::stopLeavesForeignTrackAlone()
{
    startPlaying(u"a"_s);
    m_logic->updateState(deviceState(u"dev"_s, u"x"_s, 1000));
    m_logic->stop();
    QCOMPARE(m_sink.calls, QStringList { u"state Stopped"_s });
}

void TestEngineLogic::ignoresOtherDevices()
{
    startPlaying(u"a"_s);
    m_logic->updateState(deviceState(u"phone"_s, u"z"_s, 1000, false));
    QVERIFY(m_sink.calls.isEmpty());
    QCOMPARE(m_logic->state(), EngineState::Playing);
}

void TestEngineLogic::interpolatesElapsed()
{
    startPlaying(u"a"_s);
    m_now += 1500;
    QCOMPARE(m_logic->elapsed(), qint64(1500));
    m_logic->pause();
    m_now += 5000;
    QCOMPARE(m_logic->elapsed(), qint64(1500));
}

void TestEngineLogic::playFailureClearsTracks()
{
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    m_logic->playFailed(u"gone"_s, false);
    QCOMPARE(m_sink.calls.last(), u"error"_s);
    QVERIFY(m_logic->trackIds().isEmpty());
    QVERIFY(!m_logic->isAwaitingStart());
}

QTEST_APPLESS_MAIN(TestEngineLogic)
#include "tst_enginelogic.moc"
```

- [ ] **Step 2: Header and stub, run to see it fail**

`src/logic/enginelogic.h`:
```cpp
#ifndef ENGINELOGIC_H
#define ENGINELOGIC_H

#include <functional>
#include <QString>
#include <QStringList>
#include "spotifytypes.h"

enum class EngineState { Stopped, Buffering, Playing, Paused };

class EngineSink
{
public:
    virtual ~EngineSink() = default;
    virtual void apiPlay(const QString &uri, qint64 positionMs) = 0;
    virtual void apiPause() = 0;
    virtual void apiResume() = 0;
    virtual void apiSeek(qint64 positionMs) = 0;
    virtual void apiQueue(const QString &uri) = 0;
    virtual void reportState(EngineState state) = 0;
    virtual void reportTrack(const QString &path, const SpotifyTrack &track) = 0;
    virtual void reportFinished() = 0;
    virtual void reportError(const QString &message, bool fatal) = 0;
};

class EngineLogic
{
public:
    using Clock = std::function<qint64()>;

    EngineLogic(EngineSink *sink, Clock clock);

    void setSelectedDevice(const QString &deviceId);
    bool enqueue(const QString &path, qint64 offsetMs = 0);
    void play();
    void pause();
    void seek(qint64 positionMs);
    void stop();
    void updateState(const PlayerState &state);
    void playFailed(const QString &message, bool fatal);

    EngineState state() const { return m_state; }
    qint64 elapsed() const;
    QStringList trackIds() const;
    bool isAwaitingStart() const { return m_awaiting; }

private:
    struct Entry
    {
        QString path;
        QString id;
        qint64 offsetMs = 0;

        bool isEmpty() const { return id.isEmpty(); }
    };

    void setState(EngineState state);
    void apply(const PlayerState &state);
    void switchToNext(bool startOnDevice);
    void finish();
    bool nearEnd() const;
    bool inCommandGrace() const;
    bool deviceOnCurrentTrack() const;

    EngineSink *m_sink;
    Clock m_clock;
    QString m_device;
    PlayerState m_last;
    Entry m_current;
    Entry m_next;
    bool m_nextQueued = false;
    bool m_awaiting = false;
    bool m_trackReported = false;
    EngineState m_state = EngineState::Stopped;
    SpotifyTrack m_track;
    qint64 m_progressMs = 0;
    qint64 m_progressAt = 0;
    qint64 m_commandAt = -1;
};

#endif
```

Temporary `src/logic/enginelogic.cpp`:
```cpp
#include "enginelogic.h"

EngineLogic::EngineLogic(EngineSink *sink, Clock clock) : m_sink(sink), m_clock(std::move(clock)) {}
void EngineLogic::setSelectedDevice(const QString &deviceId) { m_device = deviceId; }
bool EngineLogic::enqueue(const QString &, qint64) { return false; }
void EngineLogic::play() {}
void EngineLogic::pause() {}
void EngineLogic::seek(qint64) {}
void EngineLogic::stop() {}
void EngineLogic::updateState(const PlayerState &) {}
void EngineLogic::playFailed(const QString &, bool) {}
qint64 EngineLogic::elapsed() const { return 0; }
QStringList EngineLogic::trackIds() const { return {}; }
void EngineLogic::setState(EngineState) {}
void EngineLogic::apply(const PlayerState &) {}
void EngineLogic::switchToNext(bool) {}
void EngineLogic::finish() {}
bool EngineLogic::nearEnd() const { return false; }
bool EngineLogic::inCommandGrace() const { return false; }
bool EngineLogic::deviceOnCurrentTrack() const { return false; }
```

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R tst_enginelogic`
Expected: FAIL (`enqueue` returns false).

- [ ] **Step 3: Implement**

`src/logic/enginelogic.cpp`:
```cpp
#include "enginelogic.h"
#include "spotifylink.h"

using namespace Qt::StringLiterals;

namespace {

// polls that arrive this soon after a pause/resume/seek may predate it
const qint64 kCommandGraceMs = 2500;
// how close to the end a track must be for "stopped" or "moved on" to mean "finished"
const qint64 kEndWindowMs = 3000;
// the device rewinds to 0 when a track ends with nothing queued
const qint64 kRewoundMs = 1500;

QString trackUri(const QString &id)
{
    return u"spotify:track:"_s + id;
}

}

EngineLogic::EngineLogic(EngineSink *sink, Clock clock) : m_sink(sink), m_clock(std::move(clock))
{}

void EngineLogic::setSelectedDevice(const QString &deviceId)
{
    m_device = deviceId;
}

bool EngineLogic::enqueue(const QString &path, qint64 offsetMs)
{
    const SpotifyLink link = SpotifyLink::parse(path);
    if(link.kind != SpotifyLink::Track)
        return false;

    const Entry entry { path, link.id, qMax<qint64>(0, offsetMs) };
    if(m_current.isEmpty())
    {
        m_current = entry;
        return true;
    }
    m_next = entry;
    m_nextQueued = false;
    if(m_state == EngineState::Playing || m_state == EngineState::Paused)
    {
        m_sink->apiQueue(trackUri(m_next.id));
        m_nextQueued = true;
    }
    return true;
}

void EngineLogic::play()
{
    if(m_current.isEmpty())
        return;
    if(m_device.isEmpty())
    {
        m_current = Entry();
        m_next = Entry();
        m_sink->reportError(u"Spotify: no device selected. Pick one in Tools → Spotify → Device…"_s, true);
        return;
    }
    m_trackReported = false;
    if(m_current.offsetMs == 0 && deviceOnCurrentTrack() && m_last.isPlaying)
    {
        apply(m_last);
        return;
    }
    m_awaiting = true;
    m_progressMs = m_current.offsetMs;
    m_progressAt = m_clock();
    setState(EngineState::Buffering);
    m_sink->apiPlay(trackUri(m_current.id), m_current.offsetMs);
}

void EngineLogic::pause()
{
    if(m_state == EngineState::Playing)
    {
        m_progressMs = elapsed();
        m_progressAt = m_clock();
        m_commandAt = m_clock();
        m_sink->apiPause();
        setState(EngineState::Paused);
    }
    else if(m_state == EngineState::Paused)
    {
        m_progressAt = m_clock();
        m_commandAt = m_clock();
        m_sink->apiResume();
        setState(EngineState::Playing);
    }
}

void EngineLogic::seek(qint64 positionMs)
{
    if(m_state != EngineState::Playing && m_state != EngineState::Paused)
        return;
    m_progressMs = positionMs;
    m_progressAt = m_clock();
    m_commandAt = m_clock();
    m_sink->apiSeek(positionMs);
}

void EngineLogic::stop()
{
    if(!m_current.isEmpty() && (m_awaiting || (m_state == EngineState::Playing && deviceOnCurrentTrack() && m_last.isPlaying)))
        m_sink->apiPause();
    m_current = Entry();
    m_next = Entry();
    m_nextQueued = false;
    m_awaiting = false;
    m_trackReported = false;
    m_track = SpotifyTrack();
    setState(EngineState::Stopped);
}

void EngineLogic::updateState(const PlayerState &state)
{
    m_last = state;
    if(m_current.isEmpty() || m_state == EngineState::Stopped)
        return;

    const bool onDevice = state.hasPlayback && state.deviceId == m_device;
    if(onDevice && state.track.id == m_current.id)
    {
        if(!m_awaiting && m_state == EngineState::Playing && !state.isPlaying && state.progressMs < kRewoundMs && nearEnd())
            finish();
        else
            apply(state);
        return;
    }
    if(onDevice && !m_next.isEmpty() && state.track.id == m_next.id)
    {
        switchToNext(false);
        apply(state);
        return;
    }
    if(!m_awaiting && m_state == EngineState::Playing && nearEnd())
        finish();
}

void EngineLogic::playFailed(const QString &message, bool fatal)
{
    m_current = Entry();
    m_next = Entry();
    m_nextQueued = false;
    m_awaiting = false;
    m_trackReported = false;
    m_state = EngineState::Stopped;
    m_sink->reportError(message, fatal);
}

qint64 EngineLogic::elapsed() const
{
    qint64 value = m_progressMs;
    if(m_state == EngineState::Playing && !m_awaiting)
        value += m_clock() - m_progressAt;
    if(m_track.durationMs > 0)
        value = qMin(value, m_track.durationMs);
    return qMax<qint64>(0, value);
}

QStringList EngineLogic::trackIds() const
{
    QStringList ids;
    if(!m_current.isEmpty())
        ids << m_current.id;
    if(!m_next.isEmpty())
        ids << m_next.id;
    return ids;
}

void EngineLogic::setState(EngineState state)
{
    if(m_state == state)
        return;
    m_state = state;
    m_sink->reportState(state);
}

void EngineLogic::apply(const PlayerState &state)
{
    m_awaiting = false;
    m_track = state.track;
    if(!inCommandGrace())
    {
        m_progressMs = state.progressMs;
        m_progressAt = m_clock();
        setState(state.isPlaying ? EngineState::Playing : EngineState::Paused);
    }
    if(!m_trackReported)
    {
        m_trackReported = true;
        m_sink->reportTrack(m_current.path, state.track);
    }
    if(!m_next.isEmpty() && !m_nextQueued)
    {
        m_sink->apiQueue(trackUri(m_next.id));
        m_nextQueued = true;
    }
}

void EngineLogic::switchToNext(bool startOnDevice)
{
    m_sink->reportFinished();
    m_sink->reportState(EngineState::Stopped);
    m_current = m_next;
    m_next = Entry();
    m_nextQueued = false;
    m_trackReported = false;
    m_track = SpotifyTrack();
    m_progressMs = 0;
    m_progressAt = m_clock();
    m_commandAt = -1;
    m_state = EngineState::Buffering;
    m_sink->reportState(EngineState::Buffering);
    if(startOnDevice)
    {
        m_awaiting = true;
        m_sink->apiPlay(trackUri(m_current.id), 0);
    }
}

void EngineLogic::finish()
{
    if(!m_next.isEmpty())
    {
        switchToNext(true);
        return;
    }
    m_current = Entry();
    m_awaiting = false;
    m_trackReported = false;
    m_sink->reportFinished();
    setState(EngineState::Stopped);
}

bool EngineLogic::nearEnd() const
{
    return m_track.durationMs > 0 && elapsed() >= m_track.durationMs - kEndWindowMs;
}

bool EngineLogic::inCommandGrace() const
{
    return m_commandAt >= 0 && m_clock() - m_commandAt < kCommandGraceMs;
}

bool EngineLogic::deviceOnCurrentTrack() const
{
    return m_last.hasPlayback && m_last.deviceId == m_device && m_last.track.id == m_current.id;
}
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: all PASS. If `startsTrackAndWaitsForDevice` fails on call order, check `play()` calls `setState(Buffering)` before `apiPlay` as above.

- [ ] **Step 5: Commit**

```bash
git add src/logic tests
git commit -m "Add engine state machine"
```

---

### Task 5: Follower decision and link-expansion check

**Files:**
- Create: `src/logic/followerlogic.h`, `src/logic/followerlogic.cpp`, `tests/tst_followerlogic.cpp`
- Modify: `src/logic/CMakeLists.txt` (add `followerlogic.cpp`), `tests/CMakeLists.txt` (add `spotify_test(tst_followerlogic)`)

**Interfaces:**
- Consumes: `SpotifyLink`, `PlayerState`.
- Produces:
```cpp
struct FollowDecision { enum Action { None, Select, Append }; Action action = None; int index = -1; };
FollowDecision decideFollow(const PlayerState &state, const QString &selectedDevice, const QStringList &engineTrackIds,
                            bool engineAwaiting, const QStringList &playlistPaths, int currentIndex);
bool needsExpansion(const QString &path, const QString &title);
```

- [ ] **Step 1: Write the failing test**

`tests/tst_followerlogic.cpp`:
```cpp
#include <QtTest>
#include "followerlogic.h"

using namespace Qt::StringLiterals;

namespace {

PlayerState playing(const QString &device, const QString &trackId, bool isPlaying = true)
{
    PlayerState state;
    state.hasPlayback = true;
    state.deviceId = device;
    state.isPlaying = isPlaying;
    state.track.id = trackId;
    return state;
}

const QStringList kPlaylist = { u"spotify://track/a"_s, u"spotify://track/x"_s, u"/music/local.mp3"_s, u"spotify://track/x"_s };

}

class TestFollowerLogic : public QObject
{
    Q_OBJECT
private slots:
    void ignoresWhileEngineAwaiting();
    void ignoresOtherDeviceAndPaused();
    void ignoresEngineTracks();
    void selectsNextOccurrenceAfterCurrent();
    void selectsEarlierOccurrence();
    void appendsMissingTrack();
    void expansion();
};

void TestFollowerLogic::ignoresWhileEngineAwaiting()
{
    QCOMPARE(decideFollow(playing(u"dev"_s, u"x"_s), u"dev"_s, { u"a"_s }, true, kPlaylist, 0).action, FollowDecision::None);
}

void TestFollowerLogic::ignoresOtherDeviceAndPaused()
{
    QCOMPARE(decideFollow(playing(u"phone"_s, u"x"_s), u"dev"_s, {}, false, kPlaylist, 0).action, FollowDecision::None);
    QCOMPARE(decideFollow(playing(u"dev"_s, u"x"_s, false), u"dev"_s, {}, false, kPlaylist, 0).action, FollowDecision::None);
    QCOMPARE(decideFollow(PlayerState(), u"dev"_s, {}, false, kPlaylist, 0).action, FollowDecision::None);
}

void TestFollowerLogic::ignoresEngineTracks()
{
    QCOMPARE(decideFollow(playing(u"dev"_s, u"x"_s), u"dev"_s, { u"a"_s, u"x"_s }, false, kPlaylist, 0).action, FollowDecision::None);
}

void TestFollowerLogic::selectsNextOccurrenceAfterCurrent()
{
    const FollowDecision d = decideFollow(playing(u"dev"_s, u"x"_s), u"dev"_s, { u"a"_s }, false, kPlaylist, 2);
    QCOMPARE(d.action, FollowDecision::Select);
    QCOMPARE(d.index, 3);
}

void TestFollowerLogic::selectsEarlierOccurrence()
{
    const FollowDecision d = decideFollow(playing(u"dev"_s, u"a"_s), u"dev"_s, {}, false, kPlaylist, 3);
    QCOMPARE(d.action, FollowDecision::Select);
    QCOMPARE(d.index, 0);
}

void TestFollowerLogic::appendsMissingTrack()
{
    QCOMPARE(decideFollow(playing(u"dev"_s, u"new"_s), u"dev"_s, {}, false, kPlaylist, -1).action, FollowDecision::Append);
}

void TestFollowerLogic::expansion()
{
    QVERIFY(needsExpansion(u"spotify://album/abc"_s, QString()));
    QVERIFY(needsExpansion(u"https://open.spotify.com/track/abc?si=1"_s, u"https://open.spotify.com/track/abc?si=1"_s));
    QVERIFY(needsExpansion(u"spotify://track/abc"_s, QString()));
    QVERIFY(!needsExpansion(u"spotify://track/abc"_s, u"Song"_s));
    QVERIFY(!needsExpansion(u"/music/a.mp3"_s, QString()));
}

QTEST_APPLESS_MAIN(TestFollowerLogic)
#include "tst_followerlogic.moc"
```

- [ ] **Step 2: Header and stub, run to see it fail**

`src/logic/followerlogic.h`:
```cpp
#ifndef FOLLOWERLOGIC_H
#define FOLLOWERLOGIC_H

#include <QStringList>
#include "spotifytypes.h"

struct FollowDecision
{
    enum Action { None, Select, Append };

    Action action = None;
    int index = -1;
};

FollowDecision decideFollow(const PlayerState &state, const QString &selectedDevice, const QStringList &engineTrackIds,
                            bool engineAwaiting, const QStringList &playlistPaths, int currentIndex);
bool needsExpansion(const QString &path, const QString &title);

#endif
```

Temporary `src/logic/followerlogic.cpp`:
```cpp
#include "followerlogic.h"

FollowDecision decideFollow(const PlayerState &, const QString &, const QStringList &, bool, const QStringList &, int)
{
    return FollowDecision { FollowDecision::Append, -1 };
}

bool needsExpansion(const QString &, const QString &)
{
    return false;
}
```

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R tst_followerlogic`
Expected: FAIL in `ignoresWhileEngineAwaiting`.

- [ ] **Step 3: Implement**

`src/logic/followerlogic.cpp`:
```cpp
#include "followerlogic.h"
#include "spotifylink.h"

FollowDecision decideFollow(const PlayerState &state, const QString &selectedDevice, const QStringList &engineTrackIds,
                            bool engineAwaiting, const QStringList &playlistPaths, int currentIndex)
{
    if(engineAwaiting || selectedDevice.isEmpty() || !state.hasPlayback || !state.isPlaying
            || state.deviceId != selectedDevice || !state.track.isValid() || engineTrackIds.contains(state.track.id))
        return FollowDecision();

    const QString path = SpotifyLink::trackPath(state.track.id);
    int index = playlistPaths.indexOf(path, qMax(0, currentIndex + 1));
    if(index < 0)
        index = playlistPaths.indexOf(path);
    if(index >= 0)
        return FollowDecision { FollowDecision::Select, index };
    return FollowDecision { FollowDecision::Append, -1 };
}

bool needsExpansion(const QString &path, const QString &title)
{
    const SpotifyLink link = SpotifyLink::parse(path);
    if(!link.isValid())
        return false;
    return link.kind != SpotifyLink::Track || path != link.path() || title.isEmpty();
}
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add src/logic tests
git commit -m "Add follower decision and link expansion check"
```

---

### Task 6: OAuth login (SpotifyAuth) and the CLI

Needs a Spotify app. One-time setup (user): at https://developer.spotify.com/dashboard create an app, select "Web API", add redirect URI `http://127.0.0.1:8898/callback`, copy the client ID. Export it: `export SPOTIFY_CLIENT_ID=<client id>`.

Qt NetworkAuth 6.11 API used here (checked in `/usr/include/qt6/QtNetworkAuth`): `QOAuth2AuthorizationCodeFlow::{setAuthorizationUrl, setTokenUrl, setClientIdentifier, setRequestedScopeTokens, setPkceMethod, setAutoRefresh, setReplyHandler, grant, refreshTokens, setRefreshToken, refreshToken, token, setToken, expirationAt}`, signals `authorizeWithBrowser`, `granted`, `tokenChanged`, `refreshTokenChanged`, `requestFailed`, `serverReportedErrorOccurred`; `QOAuthHttpServerReplyHandler(const QHostAddress &, quint16, QObject *)`, `setCallbackHost`, `setCallbackPath`, `setCallbackText`, `callback`, `isListening`, `close`.

**Files:**
- Create: `src/core/CMakeLists.txt`, `src/core/spotifyauth.h`, `src/core/spotifyauth.cpp`, `tools/CMakeLists.txt`, `tools/spotify-cli.cpp`
- Modify: `CMakeLists.txt` (add `add_subdirectory(src/core)` and `add_subdirectory(tools)` after `src/logic`)

**Interfaces:**
- Produces:
```cpp
class SpotifyAuth : public QObject {
    using Callback = std::function<void(bool ok)>;
    SpotifyAuth(const QString &tokenFile, QObject *parent = nullptr);
    void setClientId(const QString &clientId); void setPort(quint16 port);
    bool hasCredentials() const; bool accessTokenValid() const; QString accessToken() const;
    void login(); void logout();
    void withToken(Callback callback);  // refreshes if needed
    void refresh(Callback callback);    // forces refresh
    static QString redirectUri(quint16 port);
signals: void loggedIn(); void loggedOut(const QString &reason); void loginFailed(const QString &message);
};
```
CMake target `qmmp-spotify-core` (shared).

- [ ] **Step 1: Build files**

Top-level `CMakeLists.txt`, after `add_subdirectory(src/logic)`:
```cmake
add_subdirectory(src/core)
add_subdirectory(tools)
```

`src/core/CMakeLists.txt`:
```cmake
add_library(qmmp-spotify-core SHARED
    spotifyauth.cpp
)
target_include_directories(qmmp-spotify-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qmmp-spotify-core PUBLIC spotifylogic Qt6::Gui Qt6::Network Qt6::NetworkAuth)
```

`tools/CMakeLists.txt`:
```cmake
add_executable(spotify-cli spotify-cli.cpp)
target_link_libraries(spotify-cli PRIVATE qmmp-spotify-core)
```

- [ ] **Step 2: Write SpotifyAuth**

`src/core/spotifyauth.h`:
```cpp
#ifndef SPOTIFYAUTH_H
#define SPOTIFYAUTH_H

#include <functional>
#include <QList>
#include <QObject>
#include <QString>

class QOAuth2AuthorizationCodeFlow;
class QOAuthHttpServerReplyHandler;

class SpotifyAuth : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(bool ok)>;

    explicit SpotifyAuth(const QString &tokenFile, QObject *parent = nullptr);

    void setClientId(const QString &clientId);
    void setPort(quint16 port);
    bool hasCredentials() const;
    bool accessTokenValid() const;
    QString accessToken() const;

    void login();
    void logout();
    void withToken(Callback callback);
    void refresh(Callback callback);

    static QString redirectUri(quint16 port);

signals:
    void loggedIn();
    void loggedOut(const QString &reason);
    void loginFailed(const QString &message);

private:
    void onGranted();
    void onFailed(bool sessionInvalid);
    void finishWaiting(bool ok);
    void clearCredentials();
    void saveRefreshToken(const QString &token);
    QString loadRefreshToken() const;

    QOAuth2AuthorizationCodeFlow *m_flow;
    QOAuthHttpServerReplyHandler *m_listener = nullptr;
    QString m_tokenFile;
    QString m_clientId;
    quint16 m_port = 8898;
    bool m_loggingIn = false;
    bool m_refreshing = false;
    QList<Callback> m_waiting;
};

#endif
```

`src/core/spotifyauth.cpp`:
```cpp
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QOAuthOobReplyHandler>
#include <QSaveFile>
#include "spotifyauth.h"

using namespace Qt::StringLiterals;

SpotifyAuth::SpotifyAuth(const QString &tokenFile, QObject *parent) : QObject(parent), m_tokenFile(tokenFile)
{
    m_flow = new QOAuth2AuthorizationCodeFlow(this);
    m_flow->setAuthorizationUrl(QUrl(u"https://accounts.spotify.com/authorize"_s));
    m_flow->setTokenUrl(QUrl(u"https://accounts.spotify.com/api/token"_s));
    m_flow->setRequestedScopeTokens({ "user-read-playback-state", "user-modify-playback-state",
                                      "user-read-currently-playing", "playlist-read-private",
                                      "playlist-read-collaborative", "user-library-read" });
    m_flow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
    m_flow->setAutoRefresh(false);
    m_flow->setReplyHandler(new QOAuthOobReplyHandler(this));
    m_flow->setRefreshToken(loadRefreshToken());

    connect(m_flow, &QAbstractOAuth::authorizeWithBrowser, this, [](const QUrl &url) { QDesktopServices::openUrl(url); });
    connect(m_flow, &QAbstractOAuth2::refreshTokenChanged, this, [this](const QString &token) {
        if(!token.isEmpty())
            saveRefreshToken(token);
    });
    connect(m_flow, &QAbstractOAuth::tokenChanged, this, [this](const QString &token) {
        if(m_refreshing && !token.isEmpty())
            finishWaiting(true);
    });
    connect(m_flow, &QAbstractOAuth::granted, this, &SpotifyAuth::onGranted);
    connect(m_flow, &QAbstractOAuth::requestFailed, this, [this](QAbstractOAuth::Error) { onFailed(false); });
    connect(m_flow, &QAbstractOAuth2::serverReportedErrorOccurred, this,
            [this](const QString &error, const QString &, const QUrl &) {
        onFailed(error == "invalid_grant"_L1 || error == "invalid_client"_L1);
    });
}

void SpotifyAuth::setClientId(const QString &clientId)
{
    m_clientId = clientId;
    m_flow->setClientIdentifier(clientId);
}

void SpotifyAuth::setPort(quint16 port)
{
    m_port = port;
}

bool SpotifyAuth::hasCredentials() const
{
    return !m_flow->refreshToken().isEmpty();
}

bool SpotifyAuth::accessTokenValid() const
{
    const QDateTime expiration = m_flow->expirationAt();
    return !m_flow->token().isEmpty() && expiration.isValid()
            && QDateTime::currentDateTime().secsTo(expiration) > 60;
}

QString SpotifyAuth::accessToken() const
{
    return m_flow->token();
}

QString SpotifyAuth::redirectUri(quint16 port)
{
    return u"http://127.0.0.1:%1/callback"_s.arg(port);
}

void SpotifyAuth::login()
{
    if(m_clientId.isEmpty())
    {
        emit loginFailed(u"Spotify: set the client ID first"_s);
        return;
    }
    auto *listener = new QOAuthHttpServerReplyHandler(QHostAddress::LocalHost, m_port, this);
    if(!listener->isListening())
    {
        delete listener;
        emit loginFailed(u"Spotify: port %1 is busy, choose another one in the settings"_s.arg(m_port));
        return;
    }
    listener->setCallbackHost(u"127.0.0.1"_s);
    listener->setCallbackPath(u"/callback"_s);
    listener->setCallbackText(u"Spotify login complete. You can close this tab."_s);
    qInfo("Spotify redirect URI: %s", qPrintable(listener->callback()));
    m_flow->setReplyHandler(listener);
    if(m_listener)
        m_listener->deleteLater();
    m_listener = listener;
    m_loggingIn = true;
    m_flow->grant();
}

void SpotifyAuth::logout()
{
    clearCredentials();
    emit loggedOut(u"Logged out of Spotify"_s);
}

void SpotifyAuth::withToken(Callback callback)
{
    if(accessTokenValid())
    {
        callback(true);
        return;
    }
    refresh(std::move(callback));
}

void SpotifyAuth::refresh(Callback callback)
{
    if(!hasCredentials() || m_clientId.isEmpty())
    {
        callback(false);
        return;
    }
    m_waiting << std::move(callback);
    if(m_refreshing)
        return;
    m_refreshing = true;
    m_flow->refreshTokens();
}

void SpotifyAuth::onGranted()
{
    if(m_listener)
        m_listener->close();
    const bool wasLoggingIn = m_loggingIn;
    m_loggingIn = false;
    finishWaiting(true);
    if(wasLoggingIn)
        emit loggedIn();
}

void SpotifyAuth::onFailed(bool sessionInvalid)
{
    if(m_listener)
        m_listener->close();
    const bool wasLoggingIn = m_loggingIn;
    m_loggingIn = false;
    finishWaiting(false);
    if(wasLoggingIn)
        emit loginFailed(u"Spotify login failed"_s);
    else if(sessionInvalid && hasCredentials())
    {
        clearCredentials();
        emit loggedOut(u"Spotify session expired, please log in again"_s);
    }
}

void SpotifyAuth::finishWaiting(bool ok)
{
    m_refreshing = false;
    const QList<Callback> waiting = std::move(m_waiting);
    m_waiting.clear();
    for(const Callback &callback : waiting)
        callback(ok);
}

void SpotifyAuth::clearCredentials()
{
    m_flow->setToken(QString());
    m_flow->setRefreshToken(QString());
    QFile::remove(m_tokenFile);
}

void SpotifyAuth::saveRefreshToken(const QString &token)
{
    QDir().mkpath(QFileInfo(m_tokenFile).absolutePath());
    QSaveFile file(m_tokenFile);
    if(!file.open(QIODevice::WriteOnly))
    {
        qWarning("Spotify: cannot write %s", qPrintable(m_tokenFile));
        return;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(QJsonObject { { u"refresh_token"_s, token } }).toJson());
    file.commit();
}

QString SpotifyAuth::loadRefreshToken() const
{
    QFile file(m_tokenFile);
    if(!file.open(QIODevice::ReadOnly))
        return QString();
    return QJsonDocument::fromJson(file.readAll()).object().value("refresh_token"_L1).toString();
}
```

- [ ] **Step 3: Write the CLI (login/logout/token)**

`tools/spotify-cli.cpp`:
```cpp
#include <QGuiApplication>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include "spotifyauth.h"

using namespace Qt::StringLiterals;

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

void done(int code)
{
    out().flush();
    QCoreApplication::exit(code);
}

}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(u"qmmp-spotify-cli"_s);
    const QStringList args = app.arguments().mid(1);
    const QString clientId = qEnvironmentVariable("SPOTIFY_CLIENT_ID");
    if(args.isEmpty() || clientId.isEmpty())
    {
        qCritical("usage: SPOTIFY_CLIENT_ID=<id> spotify-cli login|logout|token");
        return 2;
    }

    SpotifyAuth auth(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                     + u"/qmmp-spotify-cli/token.json"_s);
    auth.setClientId(clientId);
    auth.setPort(8898);

    const QString command = args.at(0);
    QTimer::singleShot(0, &app, [&] {
        if(command == "login"_L1)
        {
            QObject::connect(&auth, &SpotifyAuth::loggedIn, &app, [] { out() << "logged in\n"; done(0); });
            QObject::connect(&auth, &SpotifyAuth::loginFailed, &app, [](const QString &message) { out() << message << '\n'; done(1); });
            auth.login();
        }
        else if(command == "logout"_L1)
        {
            auth.logout();
            done(0);
        }
        else if(command == "token"_L1)
            auth.withToken([](bool ok) { out() << (ok ? "token ok\n" : "no valid token\n"); done(ok ? 0 : 1); });
        else
        {
            out() << "unknown command\n";
            done(2);
        }
    });
    return app.exec();
}
```

- [ ] **Step 4: Build and verify manually**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: builds; existing tests PASS.

Run: `./build/tools/spotify-cli login`
Expected: prints `Spotify redirect URI: http://127.0.0.1:8898/callback`, opens the browser, after approving prints `logged in`. If Spotify reports an invalid redirect URI, compare the printed URI with the one in the dashboard character by character.

Run: `stat -c %a ~/.config/qmmp-spotify-cli/token.json`
Expected: `600`.

Run (in a new process, so it must refresh): `./build/tools/spotify-cli token` twice.
Expected: `token ok` both times.

Run: `echo '{"refresh_token":"broken"}' > ~/.config/qmmp-spotify-cli/token.json && ./build/tools/spotify-cli token`
Expected: `no valid token`, and the token file is deleted (session treated as expired). If the file is not deleted, log which of `requestFailed` / `serverReportedErrorOccurred` fired (temporary `qInfo` in the lambdas) and pass `true` to `onFailed` from the one that does; remove the logging afterwards.

Run: `./build/tools/spotify-cli login` again to restore a working login.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core tools
git commit -m "Add Spotify OAuth login with PKCE and CLI"
```

---

### Task 7: Web API client

**Files:**
- Create: `src/core/spotifyapi.h`, `src/core/spotifyapi.cpp`
- Modify: `src/core/CMakeLists.txt` (add `spotifyapi.cpp`), `tools/spotify-cli.cpp` (full replacement below)

**Interfaces:**
- Consumes: `SpotifyAuth` (Task 6), `classifyReply` (Task 3), `SpotifyJson` (Task 2).
- Produces:
```cpp
struct ApiResult { bool ok = false; int status = 0; QString message; bool deviceProblem = false; QByteArray body; };
class SpotifyApi : public QObject {
    using Callback = std::function<void(const ApiResult &)>;
    template <typename T> using ListCallback = std::function<void(const ApiResult &, const QList<T> &)>;
    explicit SpotifyApi(SpotifyAuth *auth, QObject *parent = nullptr);
    void devices(ListCallback<SpotifyDevice> callback);
    void playerState(std::function<void(const ApiResult &, const PlayerState &)> callback);
    void displayName(std::function<void(const ApiResult &, const QString &)> callback);
    void play(const QString &deviceId, const QStringList &uris, qint64 positionMs, Callback callback);
    void pause(const QString &deviceId, Callback callback);
    void resume(const QString &deviceId, Callback callback);
    void seek(const QString &deviceId, qint64 positionMs, Callback callback);
    void queue(const QString &deviceId, const QString &uri, Callback callback);
    void myPlaylists(ListCallback<SpotifyPlaylist> callback);
    void playlistTracks(const QString &playlistId, ListCallback<SpotifyTrack> callback);
    void likedTracks(ListCallback<SpotifyTrack> callback);
    void albumTracks(const QString &albumId, ListCallback<SpotifyTrack> callback);
    void track(const QString &trackId, std::function<void(const ApiResult &, const SpotifyTrack &)> callback);
};
```

- [ ] **Step 1: Write SpotifyApi**

`src/core/spotifyapi.h`:
```cpp
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
    void play(const QString &deviceId, const QStringList &uris, qint64 positionMs, Callback callback);
    void pause(const QString &deviceId, Callback callback);
    void resume(const QString &deviceId, Callback callback);
    void seek(const QString &deviceId, qint64 positionMs, Callback callback);
    void queue(const QString &deviceId, const QString &uri, Callback callback);
    void myPlaylists(ListCallback<SpotifyPlaylist> callback);
    void playlistTracks(const QString &playlistId, ListCallback<SpotifyTrack> callback);
    void likedTracks(ListCallback<SpotifyTrack> callback);
    void albumTracks(const QString &albumId, ListCallback<SpotifyTrack> callback);
    void track(const QString &trackId, std::function<void(const ApiResult &, const SpotifyTrack &)> callback);

private:
    void request(const QByteArray &verb, const QUrl &url, const QByteArray &body, Callback callback, bool retried = false);
    template <typename T>
    void collect(const QUrl &url, std::function<SpotifyPage<T>(const QByteArray &)> parse, QList<T> items,
                 ListCallback<T> callback, int pages = 0);

    SpotifyAuth *m_auth;
    QNetworkAccessManager *m_network;
};

#endif
```

`src/core/spotifyapi.cpp`:
```cpp
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
                callback(ApiResult { true, status, QString(), false, data });
                break;
            case ApiDecision::RefreshAndRetry:
                m_auth->refresh([=](bool refreshed) {
                    if(refreshed)
                        request(verb, url, body, callback, true);
                    else
                        callback(ApiResult { false, status, u"Spotify session expired, please log in again"_s, false, data });
                });
                break;
            case ApiDecision::RetryAfter:
                QTimer::singleShot(decision.retryAfterMs, this, [=] { request(verb, url, body, callback, true); });
                break;
            case ApiDecision::Fail:
                callback(ApiResult { false, status, decision.message, decision.deviceProblem, data });
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
```

Add `spotifyapi.cpp` to `qmmp-spotify-core` sources.

- [ ] **Step 2: Replace the CLI with the full command set**

`tools/spotify-cli.cpp`:
```cpp
#include <QGuiApplication>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include "spotifyapi.h"
#include "spotifyauth.h"

using namespace Qt::StringLiterals;

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

void done(int code)
{
    out().flush();
    QCoreApplication::exit(code);
}

void finish(const ApiResult &result)
{
    if(!result.ok)
        out() << "error: " << result.message << " (HTTP " << result.status << ")\n";
    done(result.ok ? 0 : 1);
}

void printTracks(const ApiResult &result, const QList<SpotifyTrack> &tracks)
{
    for(const SpotifyTrack &t : tracks)
        out() << t.id << "  " << t.artists.join(u", "_s) << " - " << t.title << " [" << t.album << "] "
              << t.durationMs / 1000 << "s\n";
    out() << tracks.size() << " tracks\n";
    finish(result);
}

}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(u"qmmp-spotify-cli"_s);
    const QStringList args = app.arguments().mid(1);
    const QString clientId = qEnvironmentVariable("SPOTIFY_CLIENT_ID");
    if(args.isEmpty() || clientId.isEmpty())
    {
        qCritical("usage: SPOTIFY_CLIENT_ID=<id> spotify-cli login | logout | token | me | devices | state | playlists"
                  " | items <playlist id> | liked | album <album id> | track <track id>"
                  " | play <device id> <spotify uri> | pause <device id>");
        return 2;
    }

    SpotifyAuth auth(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                     + u"/qmmp-spotify-cli/token.json"_s);
    auth.setClientId(clientId);
    auth.setPort(8898);
    SpotifyApi api(&auth);

    const QString command = args.at(0);
    const QString arg1 = args.value(1);
    const QString arg2 = args.value(2);
    QTimer::singleShot(0, &app, [&] {
        if(command == "login"_L1)
        {
            QObject::connect(&auth, &SpotifyAuth::loggedIn, &app, [] { out() << "logged in\n"; done(0); });
            QObject::connect(&auth, &SpotifyAuth::loginFailed, &app, [](const QString &message) { out() << message << '\n'; done(1); });
            auth.login();
        }
        else if(command == "logout"_L1)
        {
            auth.logout();
            done(0);
        }
        else if(command == "token"_L1)
            auth.withToken([](bool ok) { out() << (ok ? "token ok\n" : "no valid token\n"); done(ok ? 0 : 1); });
        else if(command == "me"_L1)
            api.displayName([](const ApiResult &result, const QString &name) { out() << name << '\n'; finish(result); });
        else if(command == "devices"_L1)
            api.devices([](const ApiResult &result, const QList<SpotifyDevice> &devices) {
                for(const SpotifyDevice &d : devices)
                    out() << d.id << "  " << d.name << " (" << d.type << ")" << (d.isActive ? " active" : "") << '\n';
                finish(result);
            });
        else if(command == "state"_L1)
            api.playerState([](const ApiResult &result, const PlayerState &s) {
                if(!s.hasPlayback)
                    out() << "nothing playing\n";
                else
                    out() << s.deviceName << " (" << s.deviceId << ")" << (s.isPlaying ? " playing " : " paused ")
                          << s.track.id << " " << s.track.title << " at " << s.progressMs << " ms\n";
                finish(result);
            });
        else if(command == "playlists"_L1)
            api.myPlaylists([](const ApiResult &result, const QList<SpotifyPlaylist> &playlists) {
                for(const SpotifyPlaylist &p : playlists)
                    out() << p.id << "  " << p.name << " (" << p.trackCount << ")\n";
                finish(result);
            });
        else if(command == "items"_L1)
            api.playlistTracks(arg1, printTracks);
        else if(command == "liked"_L1)
            api.likedTracks(printTracks);
        else if(command == "album"_L1)
            api.albumTracks(arg1, printTracks);
        else if(command == "track"_L1)
            api.track(arg1, [](const ApiResult &result, const SpotifyTrack &t) { printTracks(result, { t }); });
        else if(command == "play"_L1)
            api.play(arg1, { arg2 }, 0, finish);
        else if(command == "pause"_L1)
            api.pause(arg1, finish);
        else
        {
            out() << "unknown command\n";
            done(2);
        }
    });
    return app.exec();
}
```

- [ ] **Step 3: Build and verify against the real API (this is the spike)**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: builds; tests PASS.

With Spotify open on the phone (or any device):
- `./build/tools/spotify-cli me` → your display name.
- `./build/tools/spotify-cli devices` → the phone listed with its id.
- `./build/tools/spotify-cli state` → what the phone is playing.
- `./build/tools/spotify-cli playlists` → your playlists with counts, more than 50 if you have them (paging).
- `./build/tools/spotify-cli items <an id from playlists>` → its tracks. **If this fails with HTTP 404, change `/playlists/%1/items` to `/playlists/%1/tracks` in `SpotifyApi::playlistTracks` and rerun.** If it works but prints 0 tracks for a non-empty playlist, print `result.body` once, compare the entry keys with `SpotifyJson::parseTrackItems`, add a row reproducing it to `tst_spotifyjson`, fix, rerun.
- `./build/tools/spotify-cli liked` → liked songs.
- `./build/tools/spotify-cli album 1DFixLWuPkv3KT3TnV35m3` and `./build/tools/spotify-cli track 4uLU6hMCjMI75M1A2tKUQC` → tracks with album names.
- `./build/tools/spotify-cli play <phone id> spotify:track:4uLU6hMCjMI75M1A2tKUQC` → the phone plays it; `./build/tools/spotify-cli pause <phone id>` pauses it.
- `./build/tools/spotify-cli pause nonexistent` → `error: Spotify: the selected device is not available…`.

- [ ] **Step 4: Commit**

```bash
git add src/core tools
git commit -m "Add Spotify Web API client and CLI commands"
```

---

### Task 8: Poller and SpotifyClient

**Files:**
- Create: `src/logic/pollpolicy.h`, `src/logic/pollpolicy.cpp`, `tests/tst_pollpolicy.cpp`, `src/core/spotifypoller.h`, `src/core/spotifypoller.cpp`, `src/core/spotifyclient.h`, `src/core/spotifyclient.cpp`
- Modify: `src/logic/CMakeLists.txt` (add `pollpolicy.cpp`), `tests/CMakeLists.txt` (add `spotify_test(tst_pollpolicy)`), `src/core/CMakeLists.txt` (add `spotifypoller.cpp spotifyclient.cpp`), `tools/spotify-cli.cpp` (add `watch`)

**Interfaces:**
- Consumes: `SpotifyApi`, `SpotifyAuth`, `PlayerState`.
- Produces:
```cpp
int pollIntervalMs(bool engineActive, bool selectedDevicePlaying, int failures);
class SpotifyPoller : public QObject {
    SpotifyPoller(SpotifyApi *api, QObject *parent = nullptr);
    void setEnabled(bool enabled); void setEngineActive(bool active); void setSelectedDevice(const QString &deviceId);
    void pollSoon(); PlayerState lastState() const;
signals: void stateChanged(const PlayerState &state);
};
class SpotifyClient : public QObject {
    static SpotifyClient *instance();
    SpotifyAuth *auth() const; SpotifyApi *api() const; SpotifyPoller *poller() const;
    QString clientId() const; quint16 port() const; void setCredentialsConfig(const QString &clientId, quint16 port);
    QString deviceId() const; QString deviceName() const; void setDevice(const QString &id, const QString &name);
    void setEngineTracks(const QStringList &ids, bool awaiting); QStringList engineTrackIds() const; bool isEngineAwaiting() const;
    void notify(const QString &message);
signals: void deviceChanged(); void notice(const QString &message);
};
```

- [ ] **Step 1: Failing test for the poll interval**

`tests/tst_pollpolicy.cpp`:
```cpp
#include <QtTest>
#include "pollpolicy.h"

class TestPollPolicy : public QObject
{
    Q_OBJECT
private slots:
    void intervals();
};

void TestPollPolicy::intervals()
{
    QCOMPARE(pollIntervalMs(true, false, 0), 2000);
    QCOMPARE(pollIntervalMs(false, true, 0), 2000);
    QCOMPARE(pollIntervalMs(false, false, 0), 10000);
    QCOMPARE(pollIntervalMs(true, true, 1), 2000);
    QCOMPARE(pollIntervalMs(true, true, 3), 8000);
    QCOMPARE(pollIntervalMs(false, false, 10), 30000);
}

QTEST_APPLESS_MAIN(TestPollPolicy)
#include "tst_pollpolicy.moc"
```

`src/logic/pollpolicy.h`:
```cpp
#ifndef POLLPOLICY_H
#define POLLPOLICY_H

int pollIntervalMs(bool engineActive, bool selectedDevicePlaying, int failures);

#endif
```

Temporary `src/logic/pollpolicy.cpp`:
```cpp
#include "pollpolicy.h"

int pollIntervalMs(bool, bool, int)
{
    return 0;
}
```

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R tst_pollpolicy`
Expected: FAIL.

- [ ] **Step 2: Implement the interval**

`src/logic/pollpolicy.cpp`:
```cpp
#include <QtGlobal>
#include "pollpolicy.h"

int pollIntervalMs(bool engineActive, bool selectedDevicePlaying, int failures)
{
    if(failures > 0)
        return qMin(2000 << qMin(failures - 1, 4), 30000);
    return engineActive || selectedDevicePlaying ? 2000 : 10000;
}
```

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: all PASS.

- [ ] **Step 3: Write the poller**

`src/core/spotifypoller.h`:
```cpp
#ifndef SPOTIFYPOLLER_H
#define SPOTIFYPOLLER_H

#include <QObject>
#include <QTimer>
#include "spotifytypes.h"

class SpotifyApi;

class SpotifyPoller : public QObject
{
    Q_OBJECT
public:
    explicit SpotifyPoller(SpotifyApi *api, QObject *parent = nullptr);

    void setEnabled(bool enabled);
    void setEngineActive(bool active);
    void setSelectedDevice(const QString &deviceId);
    void pollSoon();
    PlayerState lastState() const { return m_last; }

signals:
    void stateChanged(const PlayerState &state);

private:
    void poll();
    void scheduleNext();

    SpotifyApi *m_api;
    QTimer m_timer;
    PlayerState m_last;
    QString m_device;
    bool m_enabled = false;
    bool m_engineActive = false;
    bool m_inFlight = false;
    int m_failures = 0;
};

#endif
```

`src/core/spotifypoller.cpp`:
```cpp
#include <QPointer>
#include "pollpolicy.h"
#include "spotifyapi.h"
#include "spotifypoller.h"

SpotifyPoller::SpotifyPoller(SpotifyApi *api, QObject *parent) : QObject(parent), m_api(api)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &SpotifyPoller::poll);
}

void SpotifyPoller::setEnabled(bool enabled)
{
    m_enabled = enabled;
    m_failures = 0;
    if(enabled)
        pollSoon();
    else
        m_timer.stop();
}

void SpotifyPoller::setEngineActive(bool active)
{
    if(m_engineActive == active)
        return;
    m_engineActive = active;
    if(active)
        pollSoon();
}

void SpotifyPoller::setSelectedDevice(const QString &deviceId)
{
    m_device = deviceId;
    pollSoon();
}

void SpotifyPoller::pollSoon()
{
    if(m_enabled && !m_inFlight)
        m_timer.start(300);
}

void SpotifyPoller::poll()
{
    if(!m_enabled || m_inFlight)
        return;
    m_inFlight = true;
    QPointer<SpotifyPoller> self(this);
    m_api->playerState([self](const ApiResult &result, const PlayerState &state) {
        if(!self)
            return;
        self->m_inFlight = false;
        if(result.ok)
        {
            self->m_failures = 0;
            self->m_last = state;
            emit self->stateChanged(state);
        }
        else
            self->m_failures++;
        self->scheduleNext();
    });
}

void SpotifyPoller::scheduleNext()
{
    if(!m_enabled)
        return;
    const bool selectedPlaying = m_last.hasPlayback && m_last.isPlaying && m_last.deviceId == m_device;
    m_timer.start(pollIntervalMs(m_engineActive, selectedPlaying, m_failures));
}
```

- [ ] **Step 4: Write SpotifyClient**

`src/core/spotifyclient.h`:
```cpp
#ifndef SPOTIFYCLIENT_H
#define SPOTIFYCLIENT_H

#include <QHash>
#include <QObject>
#include <QStringList>

class SpotifyApi;
class SpotifyAuth;
class SpotifyPoller;

class SpotifyClient : public QObject
{
    Q_OBJECT
public:
    static SpotifyClient *instance();

    SpotifyAuth *auth() const { return m_auth; }
    SpotifyApi *api() const { return m_api; }
    SpotifyPoller *poller() const { return m_poller; }

    QString clientId() const;
    quint16 port() const;
    void setCredentialsConfig(const QString &clientId, quint16 port);

    QString deviceId() const;
    QString deviceName() const;
    void setDevice(const QString &id, const QString &name);

    void setEngineTracks(const QStringList &ids, bool awaiting);
    QStringList engineTrackIds() const { return m_engineTrackIds; }
    bool isEngineAwaiting() const { return m_engineAwaiting; }

    void notify(const QString &message);

signals:
    void deviceChanged();
    void notice(const QString &message);

private:
    explicit SpotifyClient(QObject *parent);

    SpotifyAuth *m_auth;
    SpotifyApi *m_api;
    SpotifyPoller *m_poller;
    QStringList m_engineTrackIds;
    bool m_engineAwaiting = false;
    QHash<QString, qint64> m_noticeTimes;
};

#endif
```

`src/core/spotifyclient.cpp`:
```cpp
#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QSettings>
#include "spotifyapi.h"
#include "spotifyauth.h"
#include "spotifyclient.h"
#include "spotifypoller.h"

using namespace Qt::StringLiterals;

namespace {

const qint64 kNoticeRepeatMs = 30000;

QString tokenFile()
{
    return QFileInfo(QSettings().fileName()).absolutePath() + u"/spotify/token.json"_s;
}

}

SpotifyClient *SpotifyClient::instance()
{
    static SpotifyClient *client = nullptr;
    if(!client)
        client = new SpotifyClient(qApp);
    return client;
}

SpotifyClient::SpotifyClient(QObject *parent) : QObject(parent)
{
    m_auth = new SpotifyAuth(tokenFile(), this);
    m_auth->setClientId(clientId());
    m_auth->setPort(port());
    m_api = new SpotifyApi(m_auth, this);
    m_poller = new SpotifyPoller(m_api, this);
    m_poller->setSelectedDevice(deviceId());
    m_poller->setEnabled(m_auth->hasCredentials());

    connect(m_auth, &SpotifyAuth::loggedIn, this, [this] { m_poller->setEnabled(true); });
    connect(m_auth, &SpotifyAuth::loggedOut, this, [this](const QString &reason) {
        m_poller->setEnabled(false);
        notify(reason);
    });
}

QString SpotifyClient::clientId() const
{
    return QSettings().value(u"Spotify/client_id"_s).toString();
}

quint16 SpotifyClient::port() const
{
    return quint16(QSettings().value(u"Spotify/port"_s, 8898).toUInt());
}

void SpotifyClient::setCredentialsConfig(const QString &clientId, quint16 port)
{
    QSettings settings;
    settings.setValue(u"Spotify/client_id"_s, clientId);
    settings.setValue(u"Spotify/port"_s, port);
    m_auth->setClientId(clientId);
    m_auth->setPort(port);
}

QString SpotifyClient::deviceId() const
{
    return QSettings().value(u"Spotify/device_id"_s).toString();
}

QString SpotifyClient::deviceName() const
{
    return QSettings().value(u"Spotify/device_name"_s).toString();
}

void SpotifyClient::setDevice(const QString &id, const QString &name)
{
    QSettings settings;
    settings.setValue(u"Spotify/device_id"_s, id);
    settings.setValue(u"Spotify/device_name"_s, name);
    m_poller->setSelectedDevice(id);
    emit deviceChanged();
}

void SpotifyClient::setEngineTracks(const QStringList &ids, bool awaiting)
{
    m_engineTrackIds = ids;
    m_engineAwaiting = awaiting;
    m_poller->setEngineActive(!ids.isEmpty());
}

void SpotifyClient::notify(const QString &message)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if(now - m_noticeTimes.value(message, -kNoticeRepeatMs) < kNoticeRepeatMs)
        return;
    m_noticeTimes.insert(message, now);
    qWarning("%s", qPrintable(message));
    emit notice(message);
}
```

Add `spotifypoller.cpp spotifyclient.cpp` to `qmmp-spotify-core`.

- [ ] **Step 5: Add `watch` to the CLI**

In `tools/spotify-cli.cpp` add `#include "spotifypoller.h"` and, before the final `else` (the "unknown command" branch), insert:
```cpp
        else if(command == "watch"_L1)
        {
            auto *poller = new SpotifyPoller(&api, &app);
            poller->setSelectedDevice(arg1);
            QObject::connect(poller, &SpotifyPoller::stateChanged, &app, [](const PlayerState &s) {
                out() << QTime::currentTime().toString(u"HH:mm:ss"_s) << "  "
                      << (s.hasPlayback ? s.deviceName : u"-"_s) << (s.isPlaying ? " playing " : " paused ")
                      << s.track.title << " " << s.progressMs / 1000 << "s\n";
                out().flush();
            });
            poller->setEnabled(true);
        }
```
and `#include <QTime>`. Extend the usage string with `| watch <device id>`.

- [ ] **Step 6: Build and verify**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: builds; all tests PASS.

Run: `./build/tools/spotify-cli watch <phone id>` while playing on the phone.
Expected: a line every ~2 s with advancing seconds; pause on the phone → lines every ~10 s showing `paused`. Disconnect the network briefly → no lines, then lines resume within 30 s after reconnecting. Stop with Ctrl+C.

- [ ] **Step 7: Commit**

```bash
git add src/logic src/core tests tools
git commit -m "Add player state poller and shared Spotify client"
```

---

### Task 9: Transport plugin and install layout

From here on the plugins need qmmp 2.4.2 installed. Set its prefix (the one you configured qmmp with) once per shell:
```bash
export QMMP_PREFIX=$HOME/opt/qmmp            # adjust
export PKG_CONFIG_PATH=$QMMP_PREFIX/lib64/pkgconfig:$QMMP_PREFIX/lib/pkgconfig
pkg-config --modversion qmmp qmmpui          # must print 2.4.2 twice
```

Why a transport (verified): `FileLoader` drops added URLs whose scheme no decoder or transport claims (`qmmpui/fileloader.cpp:115-129`), and `open.spotify.com` links would otherwise go to the HTTP transport. Transport regexps are checked before protocols (`qmmp/inputsource.cpp:255-287`). At play time our source has no I/O device, so the internal engine rejects it (`qmmp/qmmpaudioengine.cpp:122-138`) and qmmp tries engine plugins.

**Files:**
- Create: `src/transport/CMakeLists.txt`, `src/transport/spotifyinputfactory.h`, `src/transport/spotifyinputfactory.cpp`, `src/transport/spotifyinputsource.h`
- Modify: `CMakeLists.txt` (qmmp detection, core install, transport subdirectory)

**Interfaces:**
- Produces: CMake variables `QMMP_FOUND`, `QMMP_PLUGIN_DIR`, imported target `PkgConfig::QMMP`; the core library installed to `${QMMP_PLUGIN_DIR}` so plugins with `INSTALL_RPATH "$ORIGIN/.."` find it.

- [ ] **Step 1: qmmp detection in the top-level CMakeLists**

Append to `CMakeLists.txt`:
```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(QMMP IMPORTED_TARGET qmmp>=2.4 qmmpui>=2.4)
if(QMMP_FOUND)
    pkg_get_variable(QMMP_PKG_PLUGIN_DIR qmmp plugindir)
    set(QMMP_PLUGIN_DIR "${QMMP_PKG_PLUGIN_DIR}" CACHE PATH "qmmp plugin directory")
    install(TARGETS qmmp-spotify-core LIBRARY DESTINATION ${QMMP_PLUGIN_DIR})
    add_subdirectory(src/transport)
else()
    message(WARNING "qmmp >= 2.4 not found via pkg-config: building only the libraries, CLI and tests")
endif()
```

- [ ] **Step 2: Write the plugin**

`src/transport/CMakeLists.txt`:
```cmake
add_library(spotifytransport MODULE
    spotifyinputfactory.cpp
    spotifyinputfactory.h
    spotifyinputsource.h
)
target_link_libraries(spotifytransport PRIVATE PkgConfig::QMMP Qt6::Widgets)
set_target_properties(spotifytransport PROPERTIES OUTPUT_NAME spotify)
install(TARGETS spotifytransport LIBRARY DESTINATION ${QMMP_PLUGIN_DIR}/Transports)
```

`src/transport/spotifyinputsource.h`:
```cpp
#ifndef SPOTIFYINPUTSOURCE_H
#define SPOTIFYINPUTSOURCE_H

#include <qmmp/inputsource.h>

// Carries a Spotify link to the engine; there is no audio data to read.
class SpotifyInputSource : public InputSource
{
    Q_OBJECT
public:
    explicit SpotifyInputSource(const QString &url, QObject *parent = nullptr) : InputSource(url, parent) {}

    QIODevice *ioDevice() const override { return nullptr; }
    bool initialize() override
    {
        m_ready = true;
        emit ready();
        return true;
    }
    bool isReady() const override { return m_ready; }

private:
    bool m_ready = false;
};

#endif
```

`src/transport/spotifyinputfactory.h`:
```cpp
#ifndef SPOTIFYINPUTFACTORY_H
#define SPOTIFYINPUTFACTORY_H

#include <QObject>
#include <qmmp/inputsourcefactory.h>

class SpotifyInputFactory : public QObject, public InputSourceFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qmmp.qmmp.InputSourceFactoryInterface.1.0")
    Q_INTERFACES(InputSourceFactory)
public:
    InputSourceProperties properties() const override;
    InputSource *create(const QString &url, QObject *parent = nullptr) override;
    QDialog *createSettings(QWidget *parent) override;
    void showAbout(QWidget *parent) override;
    QString translation() const override;
};

#endif
```

`src/transport/spotifyinputfactory.cpp`:
```cpp
#include <QMessageBox>
#include "spotifyinputfactory.h"
#include "spotifyinputsource.h"

using namespace Qt::StringLiterals;

InputSourceProperties SpotifyInputFactory::properties() const
{
    InputSourceProperties properties;
    properties.name = tr("Spotify Links");
    properties.shortName = u"spotify"_s;
    properties.protocols = QStringList { u"spotify"_s };
    properties.regExps = { QRegularExpression(u"^https?://open\\.spotify\\.com/(?:intl-[A-Za-z-]+/)?(?:track|album|playlist)/"_s) };
    properties.hasAbout = true;
    return properties;
}

InputSource *SpotifyInputFactory::create(const QString &url, QObject *parent)
{
    return new SpotifyInputSource(url, parent);
}

QDialog *SpotifyInputFactory::createSettings(QWidget *)
{
    return nullptr;
}

void SpotifyInputFactory::showAbout(QWidget *parent)
{
    QMessageBox::about(parent, tr("About Spotify Links"),
                       tr("Lets qmmp accept spotify:// and open.spotify.com links. Playback is handled by the Spotify engine."));
}

QString SpotifyInputFactory::translation() const
{
    return QString();
}
```

- [ ] **Step 3: Build, install, verify in qmmp**

Run:
```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
cmake --install build
ls "$(pkg-config --variable=plugindir qmmp)"/Transports "$(pkg-config --variable=plugindir qmmp)"/libqmmp-spotify-core.so
```
Expected: tests PASS; `libspotify.so` in `Transports/`; the core library in the plugin dir.

Start `$QMMP_PREFIX/bin/qmmp`. Settings → Plugins → Transports lists "Spotify Links", enabled. Playlist → Add URL `https://open.spotify.com/track/4uLU6hMCjMI75M1A2tKUQC?si=x` and `spotify://album/1DFixLWuPkv3KT3TnV35m3`.
Expected: both entries appear in the playlist (as raw links for now). Without the plugin, the `spotify://` one is silently dropped.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt src/transport
git commit -m "Add transport plugin accepting Spotify links"
```

---

### Task 10: General plugin — settings, login, device picker, notices

**Files:**
- Create: `src/general/CMakeLists.txt`, `src/general/spotifygeneralfactory.{h,cpp}`, `src/general/spotifygeneral.{h,cpp}`, `src/general/settingsdialog.{h,cpp}`, `src/general/devicedialog.{h,cpp}`
- Modify: `CMakeLists.txt` (`add_subdirectory(src/general)` inside `if(QMMP_FOUND)`)

**Interfaces:**
- Consumes: `SpotifyClient` and everything it exposes (Task 8).
- Produces: `class SpotifyGeneral : public QObject` owning the Tools → Spotify menu (`m_menu`) and notices; private slots/methods `chooseDevice()`, `showNotice(const QString &)`, and `bool requireLogin()` (shows a notice and returns false when not logged in). Tasks 12 and 13 add to it.

- [ ] **Step 1: Build file**

`src/general/CMakeLists.txt`:
```cmake
add_library(spotifygeneral MODULE
    spotifygeneralfactory.cpp
    spotifygeneral.cpp
    settingsdialog.cpp
    devicedialog.cpp
)
target_link_libraries(spotifygeneral PRIVATE qmmp-spotify-core PkgConfig::QMMP Qt6::Widgets)
set_target_properties(spotifygeneral PROPERTIES OUTPUT_NAME spotify INSTALL_RPATH "$ORIGIN/..")
install(TARGETS spotifygeneral LIBRARY DESTINATION ${QMMP_PLUGIN_DIR}/General)
```

Add `add_subdirectory(src/general)` after `add_subdirectory(src/transport)` in the top-level `if(QMMP_FOUND)` block.

- [ ] **Step 2: Factory**

`src/general/spotifygeneralfactory.h`:
```cpp
#ifndef SPOTIFYGENERALFACTORY_H
#define SPOTIFYGENERALFACTORY_H

#include <QObject>
#include <qmmpui/generalfactory.h>

class SpotifyGeneralFactory : public QObject, public GeneralFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qmmp.qmmpui.GeneralFactoryInterface.1.0")
    Q_INTERFACES(GeneralFactory)
public:
    GeneralProperties properties() const override;
    QObject *create(QObject *parent) override;
    QDialog *createSettings(QWidget *parent) override;
    void showAbout(QWidget *parent) override;
    QString translation() const override;
};

#endif
```

`src/general/spotifygeneralfactory.cpp`:
```cpp
#include <QMessageBox>
#include "settingsdialog.h"
#include "spotifygeneral.h"
#include "spotifygeneralfactory.h"

using namespace Qt::StringLiterals;

GeneralProperties SpotifyGeneralFactory::properties() const
{
    GeneralProperties properties;
    properties.name = tr("Spotify Plugin");
    properties.shortName = u"spotify"_s;
    properties.hasAbout = true;
    properties.hasSettings = true;
    return properties;
}

QObject *SpotifyGeneralFactory::create(QObject *parent)
{
    return new SpotifyGeneral(parent);
}

QDialog *SpotifyGeneralFactory::createSettings(QWidget *parent)
{
    return new SettingsDialog(parent);
}

void SpotifyGeneralFactory::showAbout(QWidget *parent)
{
    QMessageBox::about(parent, tr("About Spotify Plugin"),
                       tr("Controls Spotify Connect devices from qmmp, loads Spotify playlists and follows what the device plays."));
}

QString SpotifyGeneralFactory::translation() const
{
    return QString();
}
```

- [ ] **Step 3: Settings dialog**

`src/general/settingsdialog.h`:
```cpp
#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

public slots:
    void accept() override;

private:
    void saveConfig();
    void toggleLogin();
    void updateStatus();
    void updateRedirectUri();

    QLineEdit *m_clientId;
    QSpinBox *m_port;
    QLineEdit *m_redirectUri;
    QLabel *m_status;
    QPushButton *m_loginButton;
};

#endif
```

`src/general/settingsdialog.cpp`:
```cpp
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include "settingsdialog.h"
#include "spotifyapi.h"
#include "spotifyauth.h"
#include "spotifyclient.h"

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Spotify Plugin Settings"));
    SpotifyClient *client = SpotifyClient::instance();

    m_clientId = new QLineEdit(client->clientId(), this);
    m_port = new QSpinBox(this);
    m_port->setRange(1024, 65535);
    m_port->setValue(client->port());
    m_redirectUri = new QLineEdit(this);
    m_redirectUri->setReadOnly(true);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_loginButton = new QPushButton(this);

    auto *help = new QLabel(tr("Create an app at <a href=\"https://developer.spotify.com/dashboard\">developer.spotify.com/dashboard</a> "
                               "(Web API), add the redirect URI below to it and paste its client ID here."), this);
    help->setOpenExternalLinks(true);
    help->setWordWrap(true);

    auto *form = new QFormLayout;
    form->addRow(tr("Client ID:"), m_clientId);
    form->addRow(tr("Redirect port:"), m_port);
    form->addRow(tr("Redirect URI:"), m_redirectUri);
    form->addRow(m_status, m_loginButton);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(help);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(m_port, &QSpinBox::valueChanged, this, &SettingsDialog::updateRedirectUri);
    connect(m_loginButton, &QPushButton::clicked, this, &SettingsDialog::toggleLogin);
    connect(client->auth(), &SpotifyAuth::loggedIn, this, &SettingsDialog::updateStatus);
    connect(client->auth(), &SpotifyAuth::loggedOut, this, &SettingsDialog::updateStatus);
    connect(client->auth(), &SpotifyAuth::loginFailed, this, [this](const QString &message) {
        m_status->setText(message);
        m_loginButton->setEnabled(true);
    });

    updateRedirectUri();
    updateStatus();
}

void SettingsDialog::accept()
{
    saveConfig();
    QDialog::accept();
}

void SettingsDialog::saveConfig()
{
    SpotifyClient::instance()->setCredentialsConfig(m_clientId->text().trimmed(), quint16(m_port->value()));
}

void SettingsDialog::toggleLogin()
{
    SpotifyAuth *auth = SpotifyClient::instance()->auth();
    saveConfig();
    if(auth->hasCredentials())
    {
        auth->logout();
        return;
    }
    m_status->setText(tr("Waiting for the browser…"));
    m_loginButton->setEnabled(false);
    auth->login();
}

void SettingsDialog::updateStatus()
{
    SpotifyClient *client = SpotifyClient::instance();
    m_loginButton->setEnabled(true);
    if(!client->auth()->hasCredentials())
    {
        m_status->setText(tr("Not logged in"));
        m_loginButton->setText(tr("Log In"));
        return;
    }
    m_status->setText(tr("Logged in"));
    m_loginButton->setText(tr("Log Out"));
    QPointer<SettingsDialog> self(this);
    client->api()->displayName([self](const ApiResult &result, const QString &name) {
        if(self && result.ok)
            self->m_status->setText(tr("Logged in as %1").arg(name));
    });
}

void SettingsDialog::updateRedirectUri()
{
    m_redirectUri->setText(SpotifyAuth::redirectUri(quint16(m_port->value())));
}
```

- [ ] **Step 4: Device dialog**

`src/general/devicedialog.h`:
```cpp
#ifndef DEVICEDIALOG_H
#define DEVICEDIALOG_H

#include <QDialog>

class QLabel;
class QListWidget;

class DeviceDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DeviceDialog(QWidget *parent = nullptr);

public slots:
    void accept() override;

private:
    void refresh();

    QListWidget *m_list;
    QLabel *m_status;
};

#endif
```

`src/general/devicedialog.cpp`:
```cpp
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include "devicedialog.h"
#include "spotifyapi.h"
#include "spotifyclient.h"

DeviceDialog::DeviceDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Spotify Device"));
    m_list = new QListWidget(this);
    m_status = new QLabel(this);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton *refreshButton = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_list);
    layout->addWidget(m_status);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &DeviceDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &DeviceDialog::reject);
    connect(refreshButton, &QPushButton::clicked, this, &DeviceDialog::refresh);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &DeviceDialog::accept);
    refresh();
}

void DeviceDialog::accept()
{
    QListWidgetItem *item = m_list->currentItem();
    if(item)
        SpotifyClient::instance()->setDevice(item->data(Qt::UserRole).toString(), item->data(Qt::UserRole + 1).toString());
    QDialog::accept();
}

void DeviceDialog::refresh()
{
    m_status->setText(tr("Loading…"));
    QPointer<DeviceDialog> self(this);
    SpotifyClient::instance()->api()->devices([self](const ApiResult &result, const QList<SpotifyDevice> &devices) {
        if(!self)
            return;
        self->m_list->clear();
        const QString selected = SpotifyClient::instance()->deviceId();
        for(const SpotifyDevice &device : devices)
        {
            QString text = QStringLiteral("%1 (%2)").arg(device.name, device.type);
            if(device.isActive)
                text += tr(" — active");
            auto *item = new QListWidgetItem(text, self->m_list);
            item->setData(Qt::UserRole, device.id);
            item->setData(Qt::UserRole + 1, device.name);
            if(device.id == selected)
                self->m_list->setCurrentItem(item);
        }
        if(!result.ok)
            self->m_status->setText(result.message);
        else if(devices.isEmpty())
            self->m_status->setText(tr("No devices. Open Spotify on a device or start Soloist, then refresh."));
        else
            self->m_status->clear();
    });
}
```

- [ ] **Step 5: The general object**

`src/general/spotifygeneral.h`:
```cpp
#ifndef SPOTIFYGENERAL_H
#define SPOTIFYGENERAL_H

#include <QMessageBox>
#include <QObject>
#include <QPointer>

class QMenu;

class SpotifyGeneral : public QObject
{
    Q_OBJECT
public:
    explicit SpotifyGeneral(QObject *parent = nullptr);
    ~SpotifyGeneral();

private:
    bool requireLogin();
    void chooseDevice();
    void showNotice(const QString &message);

    QMenu *m_menu;
    QPointer<QMessageBox> m_notice;
};

#endif
```

`src/general/spotifygeneral.cpp`:
```cpp
#include <QApplication>
#include <QMenu>
#include <qmmpui/uihelper.h>
#include "devicedialog.h"
#include "spotifyauth.h"
#include "spotifyclient.h"
#include "spotifygeneral.h"

SpotifyGeneral::SpotifyGeneral(QObject *parent) : QObject(parent)
{
    SpotifyClient *client = SpotifyClient::instance();
    connect(client, &SpotifyClient::notice, this, &SpotifyGeneral::showNotice);

    m_menu = new QMenu(tr("Spotify"));
    m_menu->addAction(tr("Device…"), this, &SpotifyGeneral::chooseDevice);
    UiHelper::instance()->addAction(m_menu->menuAction(), UiHelper::TOOLS_MENU);
}

SpotifyGeneral::~SpotifyGeneral()
{
    UiHelper::instance()->removeAction(m_menu->menuAction());
    delete m_menu;
}

bool SpotifyGeneral::requireLogin()
{
    if(SpotifyClient::instance()->auth()->hasCredentials())
        return true;
    showNotice(tr("Spotify: log in first (Settings → Plugins → Spotify Plugin → Preferences)"));
    return false;
}

void SpotifyGeneral::chooseDevice()
{
    if(!requireLogin())
        return;
    DeviceDialog dialog(QApplication::activeWindow());
    dialog.exec();
}

void SpotifyGeneral::showNotice(const QString &message)
{
    if(!m_notice)
    {
        m_notice = new QMessageBox(QMessageBox::Information, tr("Spotify"), QString(), QMessageBox::Ok);
        m_notice->setModal(false);
        m_notice->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_notice->setText(message);
    m_notice->show();
    m_notice->raise();
}
```

- [ ] **Step 6: Build, install, verify in qmmp**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure && cmake --install build`
Expected: builds, tests PASS, `General/libspotify.so` installed.

In qmmp: Settings → Plugins → General → enable "Spotify Plugin" → Preferences.
Expected:
- Paste the client ID; the redirect URI shows `http://127.0.0.1:8898/callback`; "Log In" opens the browser; after approval the status shows "Logged in as <name>".
- Reopen the dialog after restarting qmmp → still "Logged in as <name>" (refresh token persisted; `stat -c %a` on the token file next to qmmp's config prints `600`).
- Tools → Spotify → Device… lists the phone (and Soloist if running); choose one; reopen → it is preselected.
- "Log Out" → status "Not logged in"; Device… now shows the "log in first" notice. Log in again.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/general
git commit -m "Add Spotify general plugin with login and device picker"
```

---

### Task 11: Engine plugin

**Files:**
- Create: `src/engine/CMakeLists.txt`, `src/engine/spotifyengine.{h,cpp}`, `src/engine/spotifyenginefactory.{h,cpp}`
- Modify: `CMakeLists.txt` (`add_subdirectory(src/engine)` inside `if(QMMP_FOUND)`)

**Interfaces:**
- Consumes: `EngineLogic`/`EngineSink`/`EngineState` (Task 4), `SpotifyClient`, `SpotifyApi`, `SpotifyPoller` (Tasks 7–8), `SpotifyLink`.
- Produces: the Engines plugin. Publishes `SpotifyClient::setEngineTracks(ids, awaiting)` after every state change so the follower (Task 13) can see what the engine owns.

- [ ] **Step 1: Build file**

`src/engine/CMakeLists.txt`:
```cmake
add_library(spotifyengine MODULE
    spotifyenginefactory.cpp
    spotifyengine.cpp
)
target_link_libraries(spotifyengine PRIVATE qmmp-spotify-core PkgConfig::QMMP Qt6::Widgets)
set_target_properties(spotifyengine PROPERTIES OUTPUT_NAME spotify INSTALL_RPATH "$ORIGIN/..")
install(TARGETS spotifyengine LIBRARY DESTINATION ${QMMP_PLUGIN_DIR}/Engines)
```

- [ ] **Step 2: Engine**

`src/engine/spotifyengine.h`:
```cpp
#ifndef SPOTIFYENGINE_H
#define SPOTIFYENGINE_H

#include <QElapsedTimer>
#include <QTimer>
#include <qmmp/abstractengine.h>
#include "enginelogic.h"
#include "spotifyapi.h"

class SpotifyClient;

class SpotifyEngine : public AbstractEngine, private EngineSink
{
    Q_OBJECT
public:
    explicit SpotifyEngine(QObject *parent = nullptr);
    ~SpotifyEngine();

    bool enqueue(InputSource *source) override;
    bool play() override;
    void seek(qint64 time) override;
    void stop() override;
    void pause() override;

private:
    void apiPlay(const QString &uri, qint64 positionMs) override;
    void apiPause() override;
    void apiResume() override;
    void apiSeek(qint64 positionMs) override;
    void apiQueue(const QString &uri) override;
    void reportState(EngineState state) override;
    void reportTrack(const QString &path, const SpotifyTrack &track) override;
    void reportFinished() override;
    void reportError(const QString &message, bool fatal) override;

    SpotifyApi::Callback notifyOnFailure() const;
    void publishTracks();

    SpotifyClient *m_client;
    QElapsedTimer m_clock;
    EngineLogic m_logic;
    QTimer m_ticker;
};

#endif
```

`src/engine/spotifyengine.cpp`:
```cpp
#include <QPointer>
#include <qmmp/inputsource.h>
#include <qmmp/statehandler.h>
#include <qmmp/trackinfo.h>
#include "spotifyclient.h"
#include "spotifyengine.h"
#include "spotifypoller.h"

using namespace Qt::StringLiterals;

SpotifyEngine::SpotifyEngine(QObject *parent)
    : AbstractEngine(parent), m_client(SpotifyClient::instance()), m_logic(this, [this] { return m_clock.elapsed(); })
{
    m_clock.start();
    m_logic.setSelectedDevice(m_client->deviceId());
    m_logic.updateState(m_client->poller()->lastState());
    connect(m_client->poller(), &SpotifyPoller::stateChanged, this, [this](const PlayerState &state) {
        m_logic.updateState(state);
        publishTracks();
    });
    connect(m_client, &SpotifyClient::deviceChanged, this, [this] { m_logic.setSelectedDevice(m_client->deviceId()); });
    m_ticker.setInterval(250);
    connect(&m_ticker, &QTimer::timeout, this, [this] { StateHandler::instance()->dispatch(m_logic.elapsed(), 0); });
}

SpotifyEngine::~SpotifyEngine()
{
    m_client->setEngineTracks({}, false);
}

bool SpotifyEngine::enqueue(InputSource *source)
{
    if(!m_logic.enqueue(source->path(), source->offset()))
        return false;
    source->deleteLater();
    publishTracks();
    return true;
}

bool SpotifyEngine::play()
{
    m_logic.play();
    publishTracks();
    return true;
}

void SpotifyEngine::seek(qint64 time)
{
    m_logic.seek(time);
}

void SpotifyEngine::stop()
{
    m_logic.stop();
    publishTracks();
}

void SpotifyEngine::pause()
{
    m_logic.pause();
}

void SpotifyEngine::apiPlay(const QString &uri, qint64 positionMs)
{
    QPointer<SpotifyEngine> self(this);
    SpotifyClient *client = m_client;
    m_client->api()->play(m_client->deviceId(), { uri }, positionMs, [self, client](const ApiResult &result) {
        client->poller()->pollSoon();
        if(self && !result.ok)
        {
            self->m_logic.playFailed(result.message, result.deviceProblem);
            self->publishTracks();
        }
    });
}

void SpotifyEngine::apiPause()
{
    m_client->api()->pause(m_client->deviceId(), notifyOnFailure());
}

void SpotifyEngine::apiResume()
{
    m_client->api()->resume(m_client->deviceId(), notifyOnFailure());
}

void SpotifyEngine::apiSeek(qint64 positionMs)
{
    m_client->api()->seek(m_client->deviceId(), positionMs, notifyOnFailure());
}

void SpotifyEngine::apiQueue(const QString &uri)
{
    m_client->api()->queue(m_client->deviceId(), uri, notifyOnFailure());
}

SpotifyApi::Callback SpotifyEngine::notifyOnFailure() const
{
    SpotifyClient *client = m_client;
    return [client](const ApiResult &result) {
        if(!result.ok)
            client->notify(result.message);
        client->poller()->pollSoon();
    };
}

void SpotifyEngine::reportState(EngineState state)
{
    static const Qmmp::State states[] = { Qmmp::Stopped, Qmmp::Buffering, Qmmp::Playing, Qmmp::Paused };
    StateHandler::instance()->dispatch(states[int(state)]);
    if(state == EngineState::Playing || state == EngineState::Paused)
        m_ticker.start();
    else
        m_ticker.stop();
}

void SpotifyEngine::reportTrack(const QString &path, const SpotifyTrack &track)
{
    StateHandler::instance()->dispatch(track.durationMs);
    TrackInfo info(path);
    info.setDuration(track.durationMs);
    info.setValue(Qmmp::TITLE, track.title);
    info.setValue(Qmmp::ARTIST, track.artists.join(u", "_s));
    info.setValue(Qmmp::ALBUM, track.album);
    if(track.trackNumber > 0)
        info.setValue(Qmmp::TRACK, track.trackNumber);
    StateHandler::instance()->dispatch(info);
}

void SpotifyEngine::reportFinished()
{
    StateHandler::instance()->sendFinished();
}

void SpotifyEngine::reportError(const QString &message, bool fatal)
{
    m_client->notify(message);
    m_ticker.stop();
    StateHandler::instance()->dispatch(fatal ? Qmmp::FatalError : Qmmp::NormalError);
}

void SpotifyEngine::publishTracks()
{
    m_client->setEngineTracks(m_logic.trackIds(), m_logic.isAwaitingStart());
}
```

Note: a device problem (no/unavailable device) is reported as `FatalError`, which stops qmmp. Any other play failure (e.g. a track unavailable in your country) is `NormalError`, which makes qmmp skip to the next track.

- [ ] **Step 3: Factory**

`src/engine/spotifyenginefactory.h`:
```cpp
#ifndef SPOTIFYENGINEFACTORY_H
#define SPOTIFYENGINEFACTORY_H

#include <QObject>
#include <qmmp/enginefactory.h>

class SpotifyEngineFactory : public QObject, public EngineFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qmmp.qmmp.EngineFactoryInterface.1.0")
    Q_INTERFACES(EngineFactory)
public:
    bool supports(const QString &source) const override;
    EngineProperties properties() const override;
    AbstractEngine *create(QObject *parent = nullptr) override;
    QList<TrackInfo> createPlayList(const QString &path, TrackInfo::Parts parts, QStringList *ignoredPaths) override;
    MetaDataModel *createMetaDataModel(const QString &path, bool readOnly) override;
    QDialog *createSettings(QWidget *parent) override;
    void showAbout(QWidget *parent) override;
    QString translation() const override;
};

#endif
```

`src/engine/spotifyenginefactory.cpp`:
```cpp
#include <QMessageBox>
#include "spotifyengine.h"
#include "spotifyenginefactory.h"
#include "spotifylink.h"

using namespace Qt::StringLiterals;

bool SpotifyEngineFactory::supports(const QString &source) const
{
    return SpotifyLink::parse(source).kind == SpotifyLink::Track;
}

EngineProperties SpotifyEngineFactory::properties() const
{
    EngineProperties properties;
    properties.name = tr("Spotify Connect Engine");
    properties.shortName = u"spotify"_s;
    properties.protocols = QStringList { u"spotify"_s };
    properties.hasAbout = true;
    return properties;
}

AbstractEngine *SpotifyEngineFactory::create(QObject *parent)
{
    return new SpotifyEngine(parent);
}

QList<TrackInfo> SpotifyEngineFactory::createPlayList(const QString &, TrackInfo::Parts, QStringList *)
{
    return {};
}

MetaDataModel *SpotifyEngineFactory::createMetaDataModel(const QString &, bool)
{
    return nullptr;
}

QDialog *SpotifyEngineFactory::createSettings(QWidget *)
{
    return nullptr;
}

void SpotifyEngineFactory::showAbout(QWidget *parent)
{
    QMessageBox::about(parent, tr("About Spotify Connect Engine"),
                       tr("Plays Spotify tracks on the Spotify Connect device selected in Tools → Spotify → Device…"));
}

QString SpotifyEngineFactory::translation() const
{
    return QString();
}
```

Add `add_subdirectory(src/engine)` in the top-level `if(QMMP_FOUND)` block.

- [ ] **Step 4: Build, install, verify in qmmp**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure && cmake --install build`

In qmmp (device selected in Task 10, Spotify open on it), add URLs `spotify://track/4uLU6hMCjMI75M1A2tKUQC` and `spotify://track/0VjIjW4GlUZAMYd2vXMi3b`, then double-click the first.
Expected:
- The device starts the track within ~1 s; qmmp shows Playing, title/artist, the duration, and the time advancing smoothly.
- qmmp pause → the device pauses; qmmp play → it resumes. Seek in qmmp → the device jumps.
- Pause on the phone → qmmp shows Paused within ~2 s; unpause from qmmp works.
- Near the end the device moves to the second track without a gap, and qmmp's playlist cursor moves to it.
- qmmp stop → the device pauses.
- Select a device id that is offline (e.g. quit Spotify on the phone, don't refresh) and press play → a notice "the selected device is not available…", qmmp stops (does not skip through the playlist).

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/engine
git commit -m "Add Spotify Connect engine plugin"
```

---

### Task 12: Load playlists and liked songs

**Files:**
- Create: `src/general/trackbuilder.{h,cpp}`, `src/general/playlistdialog.{h,cpp}`
- Modify: `src/general/CMakeLists.txt` (add `trackbuilder.cpp playlistdialog.cpp`), `src/general/spotifygeneral.{h,cpp}`

**Interfaces:**
- Consumes: `SpotifyApi::myPlaylists/playlistTracks/likedTracks`, `SpotifyLink::trackPath`.
- Produces: `PlayListTrack *createPlayListTrack(const SpotifyTrack &track); QList<PlayListTrack *> createPlayListTracks(const QList<SpotifyTrack> &tracks);` (used by Task 13).

- [ ] **Step 1: Track builder**

`src/general/trackbuilder.h`:
```cpp
#ifndef TRACKBUILDER_H
#define TRACKBUILDER_H

#include <QList>
#include "spotifytypes.h"

class PlayListTrack;

PlayListTrack *createPlayListTrack(const SpotifyTrack &track);
QList<PlayListTrack *> createPlayListTracks(const QList<SpotifyTrack> &tracks);

#endif
```

`src/general/trackbuilder.cpp`:
```cpp
#include <qmmpui/playlisttrack.h>
#include "spotifylink.h"
#include "trackbuilder.h"

using namespace Qt::StringLiterals;

PlayListTrack *createPlayListTrack(const SpotifyTrack &track)
{
    TrackInfo info(SpotifyLink::trackPath(track.id));
    info.setDuration(track.durationMs);
    info.setValue(Qmmp::TITLE, track.title);
    info.setValue(Qmmp::ARTIST, track.artists.join(u", "_s));
    info.setValue(Qmmp::ALBUM, track.album);
    if(track.trackNumber > 0)
        info.setValue(Qmmp::TRACK, track.trackNumber);
    return new PlayListTrack(info);
}

QList<PlayListTrack *> createPlayListTracks(const QList<SpotifyTrack> &tracks)
{
    QList<PlayListTrack *> result;
    for(const SpotifyTrack &track : tracks)
        result << createPlayListTrack(track);
    return result;
}
```

- [ ] **Step 2: Playlist dialog**

`src/general/playlistdialog.h`:
```cpp
#ifndef PLAYLISTDIALOG_H
#define PLAYLISTDIALOG_H

#include <QDialog>
#include "spotifytypes.h"

class QLabel;
class QListWidget;

class PlayListDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PlayListDialog(QWidget *parent = nullptr);
    SpotifyPlaylist selected() const;

private:
    QListWidget *m_list;
    QLabel *m_status;
    QList<SpotifyPlaylist> m_playlists;
};

#endif
```

`src/general/playlistdialog.cpp`:
```cpp
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QVBoxLayout>
#include "playlistdialog.h"
#include "spotifyapi.h"
#include "spotifyclient.h"

PlayListDialog::PlayListDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Load Spotify Playlist"));
    m_list = new QListWidget(this);
    m_status = new QLabel(tr("Loading…"), this);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_list);
    layout->addWidget(m_status);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &PlayListDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &PlayListDialog::reject);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &PlayListDialog::accept);

    QPointer<PlayListDialog> self(this);
    SpotifyClient::instance()->api()->myPlaylists([self](const ApiResult &result, const QList<SpotifyPlaylist> &playlists) {
        if(!self)
            return;
        self->m_playlists = playlists;
        for(const SpotifyPlaylist &playlist : playlists)
            self->m_list->addItem(tr("%1 (%2 tracks)").arg(playlist.name).arg(playlist.trackCount));
        self->m_status->setText(result.ok ? QString() : result.message);
    });
}

SpotifyPlaylist PlayListDialog::selected() const
{
    const int row = m_list->currentRow();
    return row >= 0 && row < m_playlists.size() ? m_playlists.at(row) : SpotifyPlaylist();
}
```

- [ ] **Step 3: Menu entries and filling qmmp playlists**

In `src/general/spotifygeneral.h` add to the private section:
```cpp
    void loadPlaylist();
    void loadLikedSongs();
    void fillPlayList(const QString &name, const QList<SpotifyTrack> &tracks);
```
and `#include "spotifytypes.h"`.

In `src/general/spotifygeneral.cpp` add includes:
```cpp
#include <qmmpui/playlistmanager.h>
#include <qmmpui/playlistmodel.h>
#include "playlistdialog.h"
#include "spotifyapi.h"
#include "trackbuilder.h"
```
In the constructor, after the "Device…" action:
```cpp
    m_menu->addAction(tr("Load Playlist…"), this, &SpotifyGeneral::loadPlaylist);
    m_menu->addAction(tr("Load Liked Songs"), this, &SpotifyGeneral::loadLikedSongs);
```
and the methods:
```cpp
void SpotifyGeneral::loadPlaylist()
{
    if(!requireLogin())
        return;
    PlayListDialog dialog(QApplication::activeWindow());
    if(dialog.exec() != QDialog::Accepted || !dialog.selected().isValid())
        return;
    const SpotifyPlaylist playlist = dialog.selected();
    QPointer<SpotifyGeneral> self(this);
    SpotifyClient::instance()->api()->playlistTracks(playlist.id, [self, name = playlist.name](const ApiResult &result, const QList<SpotifyTrack> &tracks) {
        if(!self)
            return;
        if(!result.ok)
            SpotifyClient::instance()->notify(result.message);
        else
            self->fillPlayList(name, tracks);
    });
}

void SpotifyGeneral::loadLikedSongs()
{
    if(!requireLogin())
        return;
    QPointer<SpotifyGeneral> self(this);
    SpotifyClient::instance()->api()->likedTracks([self](const ApiResult &result, const QList<SpotifyTrack> &tracks) {
        if(!self)
            return;
        if(!result.ok)
            SpotifyClient::instance()->notify(result.message);
        else
            self->fillPlayList(tr("Spotify: Liked Songs"), tracks);
    });
}

void SpotifyGeneral::fillPlayList(const QString &name, const QList<SpotifyTrack> &tracks)
{
    PlayListManager *manager = PlayListManager::instance();
    PlayListModel *model = nullptr;
    for(PlayListModel *candidate : manager->playLists())
    {
        if(candidate->name() == name)
        {
            model = candidate;
            break;
        }
    }
    if(!model)
        model = manager->createPlayList(name);
    model->clear();
    model->addTracks(createPlayListTracks(tracks));
    manager->selectPlayList(model);
}
```

- [ ] **Step 4: Build, install, verify in qmmp**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure && cmake --install build`
Expected in qmmp:
- Tools → Spotify → Load Playlist… lists your playlists with counts; choosing one opens a qmmp playlist with that name, filled with titled tracks and durations (no local files/episodes). Double-click one → plays on the device.
- Load it again → the same qmmp playlist is refilled, not duplicated.
- Load Liked Songs → "Spotify: Liked Songs" playlist with all liked songs (check the count against the Spotify app).

- [ ] **Step 5: Commit**

```bash
git add src/general
git commit -m "Load Spotify playlists and liked songs into qmmp"
```

---

### Task 13: Follower and link expansion

**Files:**
- Create: `src/general/follower.{h,cpp}`, `src/general/linkexpander.{h,cpp}`
- Modify: `src/general/CMakeLists.txt` (add `follower.cpp linkexpander.cpp`), `src/general/spotifygeneral.cpp` (create both in the constructor)

**Interfaces:**
- Consumes: `decideFollow`, `needsExpansion` (Task 5); `createPlayListTrack(s)` (Task 12); `SpotifyClient::engineTrackIds/isEngineAwaiting/deviceId` (Task 8, published by the engine in Task 11); `SpotifyApi::track/albumTracks/playlistTracks`.
- qmmp facts: `PlayListModel::findTrack(trackIndex)` works in track indices; `indexOf(item)` and `insertTracks(index, …)` work in line indices (tracks and group headers); `PlayListTrack` is a `TrackInfo` (`value(Qmmp::TITLE)`).

- [ ] **Step 1: Follower**

`src/general/follower.h`:
```cpp
#ifndef FOLLOWER_H
#define FOLLOWER_H

#include <QObject>
#include "spotifytypes.h"

// Makes qmmp follow what the selected device plays when the change came from elsewhere (e.g. the phone).
class Follower : public QObject
{
    Q_OBJECT
public:
    explicit Follower(QObject *parent = nullptr);

private:
    void onState(const PlayerState &state);
};

#endif
```

`src/general/follower.cpp`:
```cpp
#include <qmmpui/mediaplayer.h>
#include <qmmpui/playlistmanager.h>
#include <qmmpui/playlistmodel.h>
#include "follower.h"
#include "followerlogic.h"
#include "spotifyclient.h"
#include "spotifypoller.h"
#include "trackbuilder.h"

Follower::Follower(QObject *parent) : QObject(parent)
{
    // queued: the engine handles the same state first and publishes its tracks
    connect(SpotifyClient::instance()->poller(), &SpotifyPoller::stateChanged, this, &Follower::onState, Qt::QueuedConnection);
}

void Follower::onState(const PlayerState &state)
{
    SpotifyClient *client = SpotifyClient::instance();
    PlayListModel *model = PlayListManager::instance()->currentPlayList();
    if(!model)
        return;

    QList<PlayListTrack *> tracks;
    QStringList paths;
    for(int i = 0; i < model->trackCount(); ++i)
    {
        PlayListTrack *track = model->findTrack(i);
        tracks << track;
        paths << (track ? track->path() : QString());
    }
    const int currentIndex = tracks.indexOf(model->currentTrack());

    const FollowDecision decision = decideFollow(state, client->deviceId(), client->engineTrackIds(),
                                                 client->isEngineAwaiting(), paths, currentIndex);
    if(decision.action == FollowDecision::None)
        return;

    PlayListTrack *target = nullptr;
    if(decision.action == FollowDecision::Select)
        target = tracks.at(decision.index);
    else
    {
        target = createPlayListTrack(state.track);
        model->addTrack(target);
    }
    MediaPlayer *player = MediaPlayer::instance();
    player->stop();
    model->setCurrent(target);
    player->play();
}
```

Why this does not restart the song on the device: `stop()` only pauses the device when it is playing the engine's own track (Task 4 `stopLeavesForeignTrackAlone`), and `play()` adopts a track the device is already playing (Task 4 `adoptsTrackAlreadyPlaying`).

- [ ] **Step 2: Link expander**

`src/general/linkexpander.h`:
```cpp
#ifndef LINKEXPANDER_H
#define LINKEXPANDER_H

#include <QObject>
#include <QPointer>
#include "spotifytypes.h"

class PlayListModel;
class PlayListTrack;
struct ApiResult;

// Replaces raw Spotify links added to any playlist with fully tagged tracks.
class LinkExpander : public QObject
{
    Q_OBJECT
public:
    explicit LinkExpander(QObject *parent = nullptr);

private:
    void watch(PlayListModel *model);
    void onTracksAdded(PlayListModel *model, const QList<PlayListTrack *> &tracks);
    void replace(QPointer<PlayListModel> model, PlayListTrack *track, const ApiResult &result,
                 const QList<SpotifyTrack> &tracks);
};

#endif
```

`src/general/linkexpander.cpp`:
```cpp
#include <qmmpui/playlistmanager.h>
#include <qmmpui/playlistmodel.h>
#include "followerlogic.h"
#include "linkexpander.h"
#include "spotifyapi.h"
#include "spotifyclient.h"
#include "spotifylink.h"
#include "trackbuilder.h"

LinkExpander::LinkExpander(QObject *parent) : QObject(parent)
{
    PlayListManager *manager = PlayListManager::instance();
    for(PlayListModel *model : manager->playLists())
        watch(model);
    connect(manager, &PlayListManager::playListAdded, this, [this, manager](int index) {
        watch(manager->playListAt(index));
    });
}

void LinkExpander::watch(PlayListModel *model)
{
    connect(model, &PlayListModel::tracksAdded, this, [this, model](const QList<PlayListTrack *> &tracks) {
        onTracksAdded(model, tracks);
    });
}

void LinkExpander::onTracksAdded(PlayListModel *model, const QList<PlayListTrack *> &tracks)
{
    SpotifyApi *api = SpotifyClient::instance()->api();
    QPointer<PlayListModel> guarded(model);
    for(PlayListTrack *track : tracks)
    {
        if(!needsExpansion(track->path(), track->value(Qmmp::TITLE)))
            continue;
        const SpotifyLink link = SpotifyLink::parse(track->path());
        auto done = [this, guarded, track](const ApiResult &result, const QList<SpotifyTrack> &expanded) {
            replace(guarded, track, result, expanded);
        };
        if(link.kind == SpotifyLink::Track)
            api->track(link.id, [done](const ApiResult &result, const SpotifyTrack &expanded) {
                done(result, expanded.isValid() ? QList<SpotifyTrack> { expanded } : QList<SpotifyTrack>());
            });
        else if(link.kind == SpotifyLink::Album)
            api->albumTracks(link.id, done);
        else
            api->playlistTracks(link.id, done);
    }
}

void LinkExpander::replace(QPointer<PlayListModel> model, PlayListTrack *track, const ApiResult &result,
                           const QList<SpotifyTrack> &tracks)
{
    if(!model)
        return;
    if(!result.ok)
    {
        SpotifyClient::instance()->notify(result.message);
        return;
    }
    const int index = model->indexOf(track);
    if(index < 0)
        return;
    model->insertTracks(index, createPlayListTracks(tracks));
    model->removeTrack(track);
}
```

Note: `indexOf(track)` compares pointers, so a track the user removed while the request was in flight yields `-1` and is skipped.

- [ ] **Step 3: Create both in SpotifyGeneral**

In `src/general/spotifygeneral.cpp` add `#include "follower.h"` and `#include "linkexpander.h"`, and at the end of the constructor:
```cpp
    new Follower(this);
    new LinkExpander(this);
```

- [ ] **Step 4: Build, install, verify in qmmp**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure && cmake --install build`
Expected in qmmp:
- Add URL `https://open.spotify.com/album/1DFixLWuPkv3KT3TnV35m3?si=x` → within ~1 s the raw entry is replaced by the album's tracks, in order, at the same position. Same for a `spotify:playlist:` URI and a track link. Dragging a link from the browser into the playlist does the same.
- Loading a playlist (Task 12) does not trigger any expansion requests (entries are already tagged).
- qmmp playing track A from a playlist; on the phone pick a song that is in the same qmmp playlist → within ~2 s the qmmp cursor moves to it and qmmp keeps playing, the phone's song is not restarted.
- Pick a song on the phone that is not in the playlist → it is appended and becomes current.
- Stop qmmp; start a song on the phone → qmmp attaches within ~10 s and shows it playing.
- Double-click a track in qmmp while the phone plays something else → qmmp's choice wins (no flip-back to the phone's old track).
- Play something on a different device than the selected one → qmmp ignores it.

- [ ] **Step 5: Commit**

```bash
git add src/general
git commit -m "Follow device playback and expand Spotify links in playlists"
```

---

### Task 14: End-to-end check and README

**Files:**
- Create: `README.md`
- Modify: `docs/superpowers/specs/2026-09-22-qmmp-spotify-design.md` only if behaviour deviated from it during implementation (record what and why).

- [ ] **Step 1: Write the README**

`README.md`:
````markdown
# qmmp-spotify

qmmp plugins that control Spotify Connect devices: play, pause, seek and skip on your phone, a speaker,
the desktop app or Spotify Soloist from qmmp; load your Spotify playlists and liked songs into qmmp
playlists; add `spotify:` and `open.spotify.com` links; and qmmp follows what the device plays.

Audio is played by the selected device, not by qmmp. For audio on this machine, run
[Spotify Soloist](https://developer.spotify.com/documentation/soloist) or the desktop app and pick it as the device.

## Requirements

- Spotify Premium.
- Your own Spotify app: create one at https://developer.spotify.com/dashboard (Web API), add the redirect URI
  `http://127.0.0.1:8898/callback`, copy the client ID. New apps are limited to 5 users by Spotify.
- qmmp 2.4, Qt 6 with NetworkAuth (`qt6-qtbase-devel`, `qt6-qtnetworkauth-devel`), CMake ≥ 3.18.

## Build and install

```bash
export PKG_CONFIG_PATH=<qmmp prefix>/lib64/pkgconfig
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
cmake --install build
```

## Use

1. Settings → Plugins → General → enable "Spotify Plugin" → Preferences: paste the client ID, Log In.
2. Tools → Spotify → Device…: pick a device.
3. Tools → Spotify → Load Playlist… / Load Liked Songs, or add a Spotify link via Add URL.

`build/tools/spotify-cli` (with `SPOTIFY_CLIENT_ID` set) runs the same API calls from a shell for debugging.
````

- [ ] **Step 2: Full manual checklist (fresh qmmp start, Soloist running, phone with Spotify)**

- [ ] Login from settings; restart qmmp; still logged in.
- [ ] Device picker shows Soloist and the phone; switch between them and play — audio moves to the chosen one.
- [ ] Play/pause/seek from qmmp and from the phone, both directions reflected within ~2 s.
- [ ] Gapless transition between two Spotify tracks; transition from a Spotify track to a local file and back.
- [ ] Phone song change while qmmp plays (in playlist / not in playlist) and while qmmp is stopped.
- [ ] Links in both forms (track, album, playlist) via Add URL and drag and drop.
- [ ] Load a playlist with > 100 tracks and liked songs; counts match the Spotify app.
- [ ] Corrupt the token file (`echo '{"refresh_token":"broken"}' > <qmmp config dir>/spotify/token.json`), restart qmmp, press play → notice "Spotify session expired, please log in again"; log in again works.
- [ ] Network off for a minute while playing → no error spam; recovers after reconnecting.

Fix anything that fails with superpowers:systematic-debugging before continuing.

- [ ] **Step 3: Commit**

```bash
git add README.md docs
git commit -m "Add README"
```
