# Context Playback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** qmmp stops feeding Spotify one queued track at a time: starting a Spotify track hands Spotify a whole context (a bound playlist) or a run of tracks, Spotify plays on by itself, and qmmp only follows what it plays, with shuffle and repeat mirrored both ways.

**Architecture:** Pure decisions go into `src/logic`: hand-off planning (`handoff`), binding bookkeeping and context URIs (`binding`), follow decisions (`followerlogic`), shuffle/repeat mirroring (`modesync`), and the engine state machine (`enginelogic`). All of them are unit-tested. The qmmp glue stays thin. The general plugin owns the bindings (`BindingStore`), loads contexts (`ContextLoader`), plans the engine's hand-offs through a planner it registers on `SpotifyClient`, moves qmmp's cursor (`Follower`) and mirrors the toggles (`ModeFollower`). The engine sends one start request per hand-off and reports a track finished only when a run is over. All queueing code is removed.

**Tech Stack:** C++17, Qt 6 (Core, Gui, Network, NetworkAuth, Widgets, Test), CMake ≥ 3.18, qmmp 2.4.2 (qmmp + qmmpui via pkg-config).

**Spec:** `docs/superpowers/specs/2026-09-26-context-playback-design.md` (implement exactly this; the older `docs/superpowers/specs/2026-09-22-qmmp-spotify-design.md` still describes everything the new spec does not change).

## Global Constraints

- Qt 6 (dev machine has 6.11.2), C++17, CMake ≥ 3.18; qmmp 2.4.2 from `~/opt/qmmp`. Run `source /home/kappa/work/qmmp-for-spotify/dev-env.sh` in every shell before cmake. That file is git-ignored: never print, add or commit it.
- Build and test: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j && ctest --test-dir build --output-on-failure`. `cmake --install build` (into `~/opt/qmmp`) is allowed. No compiler warnings.
- Do NOT make network requests to Spotify, do not run `spotify-cli` against the API, do not launch qmmp. Steps that need Alex (live API, qmmp GUI, phone) go to "Manual checks pending" in the task report. They are run in one batch at the end (Task 9 checklist).
- Commit directly on `main` (no branches), only the files the task lists. Every commit message ends with a blank line and `Claude-Session: https://claude.ai/code/session_01XwRkSvwXRWC8M6LJ7WrpgE`.
- `qmmp-2.4.2/` and `ncspot-1.4.0/` are reference only, never modified or committed.
- Tests are minimal: verify behaviour, no `// Arrange` style comments, fake only what is checked.
- Canonical playlist path for a track: `spotify://track/<id>` (`?unavailable` appended for a track not playable in the user's country). The marker is ignored when matching tracks.
- Context start: `PUT /me/player/play {context_uri, offset: {uri: <track uri>}}` (+ `position_ms`). Run start: `PUT /me/player/play {uris: [...], offset: {uri: <track uri>}}` (+ `position_ms`). A run is the consecutive Spotify tracks from the started track up to (not including) the next local file or the playlist end, at most **500** tracks per request.
- Liked Songs is the context `spotify:user:<user id>:collection`.
- Long runs: about **2.5 s before the predicted end of the portion's last track**, qmmp hands over the next portion, starting from its first track at position 0. No queueing is used.
- Shuffle/repeat: `PUT /me/player/shuffle?state=` / `PUT /me/player/repeat?state=` (`off` / `context`; qmmp's "repeat track" → `track`). Both return **200 with a non-JSON body**: treat any 2xx as success, don't parse the body. Polled `shuffle_state` / `repeat_state` update qmmp's toggles without sending anything back.
- Names and lines, verbatim: bound playlists are named `Spotify: <name>`; on detach `<name> (edited)`; the Tools → Spotify first line is `Playing as Spotify playlist: <name>` / `Plain list (not linked to Spotify)`. No notices (message boxes) on bind or detach.
- Unchanged and must not regress: the rate-limit gate, the interactive (30 / rolling 10 s) and bulk (20 / rolling 10 s) request budgets, the 5 s timeout for playback commands (15 s for other requests), start coalescing (at most one start per 1 s, latest of a burst trailing), seek debounce 300 ms, abort of a superseded start, focus-based polling (5 s focused+playing, 60 s otherwise, polls ~5 s before and ~1.5 s after a predicted track end), bounded retries (nothing re-sends a request in a loop on failure).

## Review Focus

1. qmmp emits `listChanged(STRUCTURE)` without changing the tracks: it happens when the "group tracks" setting changes, on regrouping, and on a sort that leaves the order unchanged. A bound playlist must stay bound and must not be renamed `(edited)`. Test: Task 5 `tst_binding::sameTracksStayIntact`. The glue compares content and does not trust the flag.
2. Spotify answers the shuffle and repeat PUTs with 200 and an opaque non-JSON body. That is a success: no notice, no parse error. Test: Task 1 `tst_apipolicy::opaqueSuccessBody`.
3. The player state has `"context": null` (a run), a context qmmp cannot load (`spotify:artist:…`, `spotify:show:…`), or the legacy `spotify:user:<id>:playlist:<id>` form. Null and unsupported contexts never trigger a load. The legacy form loads like `spotify:playlist:<id>`. Tests: Task 1 `tst_spotifyjson::playerStateContextAndModes`, Task 5 `tst_binding::parsesContexts`, Task 5 `tst_followerlogic::unsupportedContextSelectsOrAppends`.
4. The device plays a track whose qmmp item carries the `?unavailable` marker, because availability changed since the load. The cursor must move to that item without appending a duplicate. Test: Task 5 `tst_followerlogic::selectsMarkedItem`.
5. A run whose started track is directly followed by a local file sends a one-track run and no portion hand-over. A run of exactly 500 tracks has no second portion. Tests: Task 2 `tst_handoff::runOfOneBeforeLocalFile`, `tst_handoff::exactlyOnePortion`.

---

## File Structure

```
src/logic/
  handoff.{h,cpp}        NEW  HandOffRequest, HandOff (context / run / portions), planHandOff, findStartIndex
  binding.{h,cpp}        NEW  Binding, SpotifyContext (context URI parsing), playlist names, status line, JSON store format
  modesync.{h,cpp}       NEW  RepeatMode mapping, ModeMirror (echo-free two-way mirroring of one setting)
  enginelogic.{h,cpp}    MOD  hand-off starts, follow, finish only when a run is over, portion hand-over; queueing removed
  followerlogic.{h,cpp}  MOD  marker-insensitive matching (findTrackIndex), LoadContext decision
  spotifyjson.{h,cpp}    MOD  context/shuffle/repeat in PlayerState, playBody(), parsePlaylistName()
  spotifytypes.h         MOD  PlayerState gains contextUri, contextType, shuffle, repeat
  pollpolicy.cpp         MOD  comments only (no queue step any more)
  CMakeLists.txt         MOD  new sources
src/core/
  spotifyapi.{h,cpp}     MOD  playContext, playUris, setShuffle, setRepeat, playlistName; play/queue/next removed
  spotifyclient.h        MOD  hand-off planner hook (setHandOffPlanner / planHandOff)
  spotifypoller.cpp      MOD  comments only
src/engine/
  spotifyengine.{h,cpp}  MOD  EngineSink: planHandOff + apiStart; apiPlay/apiQueue/apiNext removed
src/general/
  bindingstore.{h,cpp}   NEW  bindings file, detach + rename on user edits, planner, status line, fill/bind/append
  contextloader.{h,cpp}  NEW  loads a playlist / album / Liked Songs context into its bound qmmp playlist
  modefollower.{h,cpp}   NEW  qmmp shuffle/repeat toggles <-> Spotify
  trackbuilder.{h,cpp}   MOD  playListTracks(), trackPaths() helpers
  follower.{h,cpp}       MOD  select / append / follow into another context
  linkexpander.{h,cpp}   MOD  a pasted album/playlist link that became the whole playlist binds it
  spotifygeneral.{h,cpp} MOD  owns BindingStore/ContextLoader/ModeFollower, status line, loads via ContextLoader
  CMakeLists.txt         MOD  new sources
tools/spotify-cli.cpp    MOD  `play` uses playUris
tests/
  tst_handoff.cpp        NEW
  tst_binding.cpp        NEW
  tst_modesync.cpp       NEW
  tst_enginelogic.cpp    MOD  queue tests removed, hand-off/follow/portion tests added
  tst_followerlogic.cpp  MOD
  tst_spotifyjson.cpp    MOD
  tst_apipolicy.cpp      MOD
  CMakeLists.txt         MOD
docs/                    MOD  design spec, backlog, README (Task 9)
```

Task order keeps the build green: Tasks 1–2 add code without users, Task 3 switches the engine over and deletes the queueing path, Task 4 adds portions, Task 5 adds the pure binding/follow logic, Tasks 6–8 wire the general plugin, and Task 9 updates the docs and holds the manual checklist.

---

### Task 1: Web API and JSON for contexts, shuffle and repeat

**Files:**
- Modify: `src/logic/spotifytypes.h` (struct `PlayerState`)
- Modify: `src/logic/spotifyjson.h`, `src/logic/spotifyjson.cpp`
- Modify: `src/core/spotifyapi.h`, `src/core/spotifyapi.cpp`
- Modify: `tools/spotify-cli.cpp:116`
- Test: `tests/tst_spotifyjson.cpp`, `tests/tst_apipolicy.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `PlayerState::contextUri` (QString, empty for a list of tracks), `PlayerState::contextType` (QString), `PlayerState::shuffle` (bool), `PlayerState::repeat` (QString: `"off"`, `"context"`, `"track"`; `"off"` when missing).
  - `QByteArray SpotifyJson::playBody(const QString &contextUri, const QStringList &uris, const QString &offsetUri, qint64 positionMs)`
  - `QString SpotifyJson::parsePlaylistName(const QByteArray &json)`
  - `ApiRequestHandle SpotifyApi::playContext(const QString &deviceId, const QString &contextUri, const QString &offsetUri, qint64 positionMs, Callback callback)`
  - `ApiRequestHandle SpotifyApi::playUris(const QString &deviceId, const QStringList &uris, const QString &offsetUri, qint64 positionMs, Callback callback)`
  - `void SpotifyApi::setShuffle(const QString &deviceId, bool on, Callback callback)`
  - `void SpotifyApi::setRepeat(const QString &deviceId, const QString &state, Callback callback)`
  - `void SpotifyApi::playlistName(const QString &playlistId, std::function<void(const ApiResult &, const QString &)> callback)`
  - `SpotifyApi::play(...)`, `queue(...)`, `next(...)` remain until Task 3 removes them.

- [ ] **Step 1: Write the failing tests**

In `tests/tst_spotifyjson.cpp`, add `#include <QJsonArray>`, `#include <QJsonDocument>`, `#include <QJsonObject>` after `#include <QtTest>`. Add three slots to `private slots:` (after `playerStateVolume();`):

```cpp
    void playerStateContextAndModes();
    void playBody();
    void playlistName();
```

and their bodies (before `QTEST_APPLESS_MAIN`):

```cpp
void TestSpotifyJson::playerStateContextAndModes()
{
    const PlayerState s = SpotifyJson::parsePlayerState(R"({
        "device":{"id":"d1"},"is_playing":true,"shuffle_state":true,"repeat_state":"context",
        "context":{"type":"collection","uri":"spotify:user:u1:collection"},
        "item":{"type":"track","id":"t1","name":"Song"}})");
    QCOMPARE(s.contextUri, u"spotify:user:u1:collection"_s);
    QCOMPARE(s.contextType, u"collection"_s);
    QVERIFY(s.shuffle);
    QCOMPARE(s.repeat, u"context"_s);

    const PlayerState plain = SpotifyJson::parsePlayerState(R"({"device":{"id":"d1"},"is_playing":true,"context":null})");
    QVERIFY(plain.contextUri.isEmpty());
    QVERIFY(plain.contextType.isEmpty());
    QVERIFY(!plain.shuffle);
    QCOMPARE(plain.repeat, u"off"_s);
}

void TestSpotifyJson::playBody()
{
    const QJsonObject context = QJsonDocument::fromJson(
        SpotifyJson::playBody(u"spotify:playlist:p"_s, {}, u"spotify:track:b"_s, 0)).object();
    QCOMPARE(context.value("context_uri"_L1).toString(), u"spotify:playlist:p"_s);
    QVERIFY(!context.contains("uris"_L1));
    QCOMPARE(context.value("offset"_L1).toObject().value("uri"_L1).toString(), u"spotify:track:b"_s);
    QCOMPARE(context.value("position_ms"_L1).toInteger(), 0);

    const QJsonObject run = QJsonDocument::fromJson(
        SpotifyJson::playBody(QString(), { u"spotify:track:a"_s, u"spotify:track:b"_s }, u"spotify:track:a"_s, 1500)).object();
    QVERIFY(!run.contains("context_uri"_L1));
    QCOMPARE(run.value("uris"_L1).toArray().size(), 2);
    QCOMPARE(run.value("uris"_L1).toArray().at(1).toString(), u"spotify:track:b"_s);
    QCOMPARE(run.value("offset"_L1).toObject().value("uri"_L1).toString(), u"spotify:track:a"_s);
    QCOMPARE(run.value("position_ms"_L1).toInteger(), 1500);
}

void TestSpotifyJson::playlistName()
{
    QCOMPARE(SpotifyJson::parsePlaylistName(R"({"name":"My India"})"), u"My India"_s);
    QVERIFY(SpotifyJson::parsePlaylistName("garbage").isEmpty());
}
```

In `tests/tst_apipolicy.cpp` add the slot `void opaqueSuccessBody();` after `void success();` and its body after `TestApiPolicy::success()`:

```cpp
// shuffle and repeat answer 200 with an opaque id, not JSON
void TestApiPolicy::opaqueSuccessBody()
{
    QCOMPARE(classifyReply(200, QString(), QByteArray(), "39a8c5f0e1d2b3a4", false).action, ApiDecision::Success);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `source dev-env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j 2>&1 | grep -E "error|warning" | head`
Expected: compile errors: `no member named 'contextUri' in 'PlayerState'`, `no member named 'playBody' in namespace 'SpotifyJson'`. (`opaqueSuccessBody` already passes, because `classifyReply` treats any 2xx as success. It pins that behaviour.)

- [ ] **Step 3: Implement**

`src/logic/spotifytypes.h`, in `struct PlayerState` after `bool supportsVolume = true;`:

```cpp
    // the context Spotify plays from ("spotify:playlist:…", "spotify:album:…",
    // "spotify:user:<id>:collection"); empty when it plays a list of tracks
    QString contextUri;
    QString contextType;
    bool shuffle = false;
    // "off", "context" or "track"
    QString repeat;
```

`src/logic/spotifyjson.h`, add to the namespace:

```cpp
QString parsePlaylistName(const QByteArray &json);
// the body of PUT /me/player/play: a context (contextUri) or a list of tracks (uris), from offsetUri
QByteArray playBody(const QString &contextUri, const QStringList &uris, const QString &offsetUri, qint64 positionMs);
```

`src/logic/spotifyjson.cpp`: in `parsePlayerState`, before `return state;`:

```cpp
    const QJsonObject context = object.value("context"_L1).toObject();
    state.contextUri = context.value("uri"_L1).toString();
    state.contextType = context.value("type"_L1).toString();
    state.shuffle = object.value("shuffle_state"_L1).toBool();
    state.repeat = object.value("repeat_state"_L1).toString(u"off"_s);
```

and at the end of the file:

```cpp
QString SpotifyJson::parsePlaylistName(const QByteArray &json)
{
    return toObject(json).value("name"_L1).toString();
}

QByteArray SpotifyJson::playBody(const QString &contextUri, const QStringList &uris, const QString &offsetUri, qint64 positionMs)
{
    QJsonObject body;
    if(!contextUri.isEmpty())
        body.insert(u"context_uri"_s, contextUri);
    else
        body.insert(u"uris"_s, QJsonArray::fromStringList(uris));
    if(!offsetUri.isEmpty())
        body.insert(u"offset"_s, QJsonObject { { u"uri"_s, offsetUri } });
    body.insert(u"position_ms"_s, positionMs);
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}
```

`src/core/spotifyapi.h`, public section after `ApiRequestHandle play(...)`:

```cpp
    // PUT /me/player/play from a context or a list of tracks, each from offsetUri; a start that
    // is superseded is cancelled through the handle
    ApiRequestHandle playContext(const QString &deviceId, const QString &contextUri, const QString &offsetUri,
                                 qint64 positionMs, Callback callback);
    ApiRequestHandle playUris(const QString &deviceId, const QStringList &uris, const QString &offsetUri,
                              qint64 positionMs, Callback callback);
    // any 2xx is success: Spotify answers with an opaque id, not JSON
    void setShuffle(const QString &deviceId, bool on, Callback callback);
    // state: "off", "context" or "track"
    void setRepeat(const QString &deviceId, const QString &state, Callback callback);
```

and after `void albumTracks(...)`:

```cpp
    void playlistName(const QString &playlistId, std::function<void(const ApiResult &, const QString &)> callback);
```

private section, after `void clearBlock();`:

```cpp
    ApiRequestHandle startPlayback(const QString &deviceId, const QByteArray &body, Callback callback);
```

Also update the comment above `play(...)` in the header to read: `// playback commands (play … setVolume, shuffle, repeat) time out after 5 s, other requests after 15 s`.

`src/core/spotifyapi.cpp`, after `SpotifyApi::play(...)`:

```cpp
ApiRequestHandle SpotifyApi::startPlayback(const QString &deviceId, const QByteArray &body, Callback callback)
{
    const ApiRequestHandle handle = std::make_shared<ApiRequest>();
    request("PUT", endpoint(u"/me/player/play"_s, deviceQuery(deviceId)), body, callback, Kind::Command, handle);
    return handle;
}

ApiRequestHandle SpotifyApi::playContext(const QString &deviceId, const QString &contextUri, const QString &offsetUri,
                                         qint64 positionMs, Callback callback)
{
    return startPlayback(deviceId, SpotifyJson::playBody(contextUri, {}, offsetUri, positionMs), std::move(callback));
}

ApiRequestHandle SpotifyApi::playUris(const QString &deviceId, const QStringList &uris, const QString &offsetUri,
                                      qint64 positionMs, Callback callback)
{
    return startPlayback(deviceId, SpotifyJson::playBody(QString(), uris, offsetUri, positionMs), std::move(callback));
}

void SpotifyApi::setShuffle(const QString &deviceId, bool on, Callback callback)
{
    QUrlQuery query = deviceQuery(deviceId);
    query.addQueryItem(u"state"_s, on ? u"true"_s : u"false"_s);
    request("PUT", endpoint(u"/me/player/shuffle"_s, query), QByteArray(), callback, Kind::Command);
}

void SpotifyApi::setRepeat(const QString &deviceId, const QString &state, Callback callback)
{
    QUrlQuery query = deviceQuery(deviceId);
    query.addQueryItem(u"state"_s, state);
    request("PUT", endpoint(u"/me/player/repeat"_s, query), QByteArray(), callback, Kind::Command);
}
```

and after `SpotifyApi::albumTracks(...)`:

```cpp
// part of a context load: a bulk request like the pages that follow it
void SpotifyApi::playlistName(const QString &playlistId, std::function<void(const ApiResult &, const QString &)> callback)
{
    QUrlQuery query;
    query.addQueryItem(u"fields"_s, u"name"_s);
    request("GET", endpoint(u"/playlists/"_s + playlistId, query), QByteArray(), [callback](const ApiResult &result) {
        callback(result, result.ok ? SpotifyJson::parsePlaylistName(result.body) : QString());
    }, Kind::Bulk);
}
```

`tools/spotify-cli.cpp` line 116: replace `api.play(arg1, { arg2 }, 0, finish);` with:

```cpp
            api.playUris(arg1, { arg2 }, arg2, 0, finish);
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: all suites pass, no warnings.

- [ ] **Step 5: Commit**

```bash
git add src/logic/spotifytypes.h src/logic/spotifyjson.h src/logic/spotifyjson.cpp src/core/spotifyapi.h src/core/spotifyapi.cpp tools/spotify-cli.cpp tests/tst_spotifyjson.cpp tests/tst_apipolicy.cpp
git commit -F - <<'EOF'
Web API for context playback, shuffle and repeat

Claude-Session: https://claude.ai/code/session_01XwRkSvwXRWC8M6LJ7WrpgE
EOF
```

---

### Task 2: Hand-off planning (pure)

**Files:**
- Create: `src/logic/handoff.h`, `src/logic/handoff.cpp`
- Modify: `src/logic/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/tst_handoff.cpp`

**Interfaces:**
- Consumes: `SpotifyLink::parse` (existing).
- Produces (used by Tasks 3, 4, 6):
  - `struct HandOffRequest { QString contextUri; QStringList uris; QString offsetUri; qint64 positionMs = 0; }`
  - `class HandOff` with `static constexpr int kPortionSize = 500;`, `static HandOff context(const QString &contextUri, const QString &trackId)`, `static HandOff run(const QStringList &trackIds)`, `bool isEmpty() const`, `bool isContext() const`, `bool isRun() const`, `HandOffRequest start(qint64 positionMs) const`, `bool matches(const QString &contextUri, const QString &trackId) const`, `bool leftRun(const QString &trackId) const`, `QString portionEndId() const`, `HandOffRequest takeNextPortion()`
  - `int findStartIndex(const QStringList &paths, int currentIndex, const QString &path)`
  - `HandOff planHandOff(const QStringList &paths, int startIndex, const QString &contextUri, int boundCount)`

- [ ] **Step 1: Write the failing test**

`tests/tst_handoff.cpp`:

```cpp
#include <QtTest>
#include "handoff.h"

using namespace Qt::StringLiterals;

namespace {

const QStringList kMixed = {
    u"spotify://track/a"_s, u"/music/x.mp3"_s, u"spotify://track/b"_s, u"spotify://track/c?unavailable"_s,
    u"spotify://track/d"_s, u"/music/y.mp3"_s, u"spotify://track/e"_s,
};

QStringList ids(int from, int count)
{
    QStringList result;
    for(int i = from; i < from + count; ++i)
        result << u"t%1"_s.arg(i);
    return result;
}

QStringList uris(int from, int count)
{
    QStringList result;
    for(const QString &id : ids(from, count))
        result << u"spotify:track:"_s + id;
    return result;
}

}

class TestHandOff : public QObject
{
    Q_OBJECT
private slots:
    void contextStartsAtTrack();
    void runStopsAtLocalFile();
    void runEndsAtPlaylistEnd();
    void runOfOneBeforeLocalFile();
    void appendedItemsOfBoundPlaylistPlayAsRun();
    void nothingForLocalFileOrBadIndex();
    void longRunInPortions();
    void exactlyOnePortion();
    void leftRun();
    void matches();
    void findsStartIndex();
};

void TestHandOff::contextStartsAtTrack()
{
    const HandOff handOff = planHandOff({ u"spotify://track/a"_s, u"spotify://track/b"_s }, 1, u"spotify:playlist:p"_s, 2);
    QVERIFY(handOff.isContext());
    const HandOffRequest request = handOff.start(4000);
    QCOMPARE(request.contextUri, u"spotify:playlist:p"_s);
    QVERIFY(request.uris.isEmpty());
    QCOMPARE(request.offsetUri, u"spotify:track:b"_s);
    QCOMPARE(request.positionMs, qint64(4000));
    QVERIFY(handOff.portionEndId().isEmpty());
}

void TestHandOff::runStopsAtLocalFile()
{
    const HandOff handOff = planHandOff(kMixed, 2, QString(), 0);
    QVERIFY(handOff.isRun());
    const HandOffRequest request = handOff.start(1500);
    QVERIFY(request.contextUri.isEmpty());
    QCOMPARE(request.uris, (QStringList { u"spotify:track:b"_s, u"spotify:track:c"_s, u"spotify:track:d"_s }));
    QCOMPARE(request.offsetUri, u"spotify:track:b"_s);
    QCOMPARE(request.positionMs, qint64(1500));
}

void TestHandOff::runEndsAtPlaylistEnd()
{
    QCOMPARE(planHandOff(kMixed, 6, QString(), 0).start(0).uris, QStringList { u"spotify:track:e"_s });
}

void TestHandOff::runOfOneBeforeLocalFile()
{
    const HandOff handOff = planHandOff(kMixed, 0, QString(), 0);
    QCOMPARE(handOff.start(0).uris, QStringList { u"spotify:track:a"_s });
    QVERIFY(handOff.portionEndId().isEmpty());
}

void TestHandOff::appendedItemsOfBoundPlaylistPlayAsRun()
{
    const QStringList paths = { u"spotify://track/a"_s, u"spotify://track/b"_s, u"spotify://track/n"_s };
    QVERIFY(planHandOff(paths, 1, u"spotify:album:z"_s, 2).isContext());
    const HandOff appended = planHandOff(paths, 2, u"spotify:album:z"_s, 2);
    QVERIFY(appended.isRun());
    QCOMPARE(appended.start(0).uris, QStringList { u"spotify:track:n"_s });
}

void TestHandOff::nothingForLocalFileOrBadIndex()
{
    QVERIFY(planHandOff(kMixed, 1, QString(), 0).isEmpty());
    QVERIFY(planHandOff(kMixed, -1, QString(), 0).isEmpty());
    QVERIFY(planHandOff(kMixed, 7, QString(), 0).isEmpty());
}

void TestHandOff::longRunInPortions()
{
    HandOff handOff = HandOff::run(ids(0, 1201));
    QCOMPARE(handOff.start(0).uris, uris(0, 500));
    QCOMPARE(handOff.portionEndId(), u"t499"_s);

    const HandOffRequest second = handOff.takeNextPortion();
    QCOMPARE(second.uris, uris(500, 500));
    QCOMPARE(second.offsetUri, u"spotify:track:t500"_s);
    QCOMPARE(second.positionMs, qint64(0));
    QCOMPARE(handOff.portionEndId(), u"t999"_s);

    QCOMPARE(handOff.takeNextPortion().uris, uris(1000, 201));
    QVERIFY(handOff.portionEndId().isEmpty());
    QVERIFY(handOff.takeNextPortion().uris.isEmpty());
}

void TestHandOff::exactlyOnePortion()
{
    const HandOff handOff = HandOff::run(ids(0, HandOff::kPortionSize));
    QCOMPARE(handOff.start(0).uris.size(), 500);
    QVERIFY(handOff.portionEndId().isEmpty());
}

void TestHandOff::leftRun()
{
    const HandOff run = HandOff::run({ u"a"_s, u"b"_s });
    QVERIFY(!run.leftRun(u"b"_s));
    QVERIFY(run.leftRun(u"x"_s));
    QVERIFY(run.leftRun(QString()));
    QVERIFY(!HandOff::context(u"spotify:playlist:p"_s, u"a"_s).leftRun(u"x"_s));
    QVERIFY(!HandOff().leftRun(u"x"_s));
}

void TestHandOff::matches()
{
    const HandOff context = HandOff::context(u"spotify:playlist:p"_s, u"a"_s);
    QVERIFY(context.matches(u"spotify:playlist:p"_s, u"z"_s));
    QVERIFY(!context.matches(u"spotify:album:q"_s, u"a"_s));
    QVERIFY(!context.matches(QString(), u"a"_s));
    const HandOff run = HandOff::run({ u"a"_s, u"b"_s });
    QVERIFY(run.matches(QString(), u"b"_s));
    QVERIFY(!run.matches(QString(), u"x"_s));
    QVERIFY(!run.matches(u"spotify:album:q"_s, u"b"_s));
    QVERIFY(!HandOff().matches(QString(), u"a"_s));
}

void TestHandOff::findsStartIndex()
{
    const QStringList paths = { u"spotify://track/a"_s, u"spotify://track/b"_s, u"spotify://track/a"_s };
    QCOMPARE(findStartIndex(paths, 2, u"spotify://track/a"_s), 2);
    QCOMPARE(findStartIndex(paths, 0, u"spotify://track/b"_s), 1);
    QCOMPARE(findStartIndex(paths, 1, u"spotify://track/a"_s), 2);
    QCOMPARE(findStartIndex(paths, 2, u"spotify://track/b"_s), 1);
    QCOMPARE(findStartIndex(paths, -1, u"spotify://track/b"_s), 1);
    QCOMPARE(findStartIndex(paths, 0, u"spotify://track/z"_s), -1);
}

QTEST_APPLESS_MAIN(TestHandOff)
#include "tst_handoff.moc"
```

Add `spotify_test(tst_handoff)` to `tests/CMakeLists.txt`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build -j 2>&1 | grep -E "error" | head -3`
Expected: `fatal error: handoff.h: No such file or directory`.

- [ ] **Step 3: Implement**

`src/logic/handoff.h`:

```cpp
#ifndef HANDOFF_H
#define HANDOFF_H

#include <QString>
#include <QStringList>

// one PUT /me/player/play: a context (contextUri) or a list of tracks (uris), from offsetUri
struct HandOffRequest
{
    QString contextUri;
    QStringList uris;
    QString offsetUri;
    qint64 positionMs = 0;
};

// What qmmp hands to Spotify when it starts a track: the playlist's context, or a run of the
// playlist's consecutive Spotify tracks. Spotify plays it on by itself; qmmp only follows.
class HandOff
{
public:
    // tracks per request of a run; a longer run is handed over in portions
    static constexpr int kPortionSize = 500;

    static HandOff context(const QString &contextUri, const QString &trackId);
    // trackIds: the run from the started track to its end (a local file or the playlist end)
    static HandOff run(const QStringList &trackIds);

    bool isEmpty() const { return m_kind == Kind::None; }
    bool isContext() const { return m_kind == Kind::Context; }
    bool isRun() const { return m_kind == Kind::Run; }
    // the request that starts it, positionMs into the started track
    HandOffRequest start(qint64 positionMs) const;
    // the device, playing trackId from contextUri (empty: a list of tracks), plays this hand-off
    bool matches(const QString &contextUri, const QString &trackId) const;
    // a track of a run ended and the device plays trackId (empty: nothing): true when that is
    // not part of the run, so the run is over; never true for a context
    bool leftRun(const QString &trackId) const;
    // the last track of the portion being played when another portion follows, else empty
    QString portionEndId() const;
    // the next portion, from its first track; it becomes the portion being played
    HandOffRequest takeNextPortion();

private:
    enum class Kind { None, Context, Run };

    HandOffRequest portionRequest(int portion) const;

    Kind m_kind = Kind::None;
    QString m_contextUri;
    // a context: the started track
    QString m_trackId;
    // a run: all its tracks, all portions
    QStringList m_trackIds;
    int m_portion = 0;
};

// the playlist item with path: the current one, else the next one after it, else the first; -1 if none
int findStartIndex(const QStringList &paths, int currentIndex, const QString &path);
// the hand-off that starts item startIndex of a playlist with these item paths. boundCount > 0:
// the playlist is bound to contextUri and its first boundCount items are the context's tracks
// (items qmmp appended later play as a run). Empty when the item is not a Spotify track.
HandOff planHandOff(const QStringList &paths, int startIndex, const QString &contextUri, int boundCount);

#endif
```

`src/logic/handoff.cpp`:

```cpp
#include "handoff.h"
#include "spotifylink.h"

using namespace Qt::StringLiterals;

namespace {

QString uriOf(const QString &trackId)
{
    return u"spotify:track:"_s + trackId;
}

// the unavailable marker is ignored: Spotify skips such a track inside a run
QString trackIdOf(const QString &path)
{
    const SpotifyLink link = SpotifyLink::parse(path);
    return link.kind == SpotifyLink::Track ? link.id : QString();
}

}

HandOff HandOff::context(const QString &contextUri, const QString &trackId)
{
    HandOff handOff;
    handOff.m_kind = Kind::Context;
    handOff.m_contextUri = contextUri;
    handOff.m_trackId = trackId;
    return handOff;
}

HandOff HandOff::run(const QStringList &trackIds)
{
    HandOff handOff;
    if(trackIds.isEmpty())
        return handOff;
    handOff.m_kind = Kind::Run;
    handOff.m_trackIds = trackIds;
    return handOff;
}

HandOffRequest HandOff::start(qint64 positionMs) const
{
    HandOffRequest request;
    if(m_kind == Kind::Context)
    {
        request.contextUri = m_contextUri;
        request.offsetUri = uriOf(m_trackId);
    }
    else if(m_kind == Kind::Run)
        request = portionRequest(m_portion);
    request.positionMs = positionMs;
    return request;
}

bool HandOff::matches(const QString &contextUri, const QString &trackId) const
{
    switch(m_kind)
    {
    case Kind::Context:
        return contextUri == m_contextUri;
    case Kind::Run:
        return contextUri.isEmpty() && m_trackIds.contains(trackId);
    case Kind::None:
        break;
    }
    return false;
}

bool HandOff::leftRun(const QString &trackId) const
{
    return m_kind == Kind::Run && (trackId.isEmpty() || !m_trackIds.contains(trackId));
}

QString HandOff::portionEndId() const
{
    const int end = (m_portion + 1) * kPortionSize;
    if(m_kind != Kind::Run || end >= m_trackIds.size())
        return QString();
    return m_trackIds.at(end - 1);
}

HandOffRequest HandOff::takeNextPortion()
{
    if(portionEndId().isEmpty())
        return HandOffRequest();
    return portionRequest(++m_portion);
}

HandOffRequest HandOff::portionRequest(int portion) const
{
    HandOffRequest request;
    for(const QString &id : m_trackIds.mid(portion * kPortionSize, kPortionSize))
        request.uris << uriOf(id);
    request.offsetUri = request.uris.value(0);
    return request;
}

int findStartIndex(const QStringList &paths, int currentIndex, const QString &path)
{
    if(currentIndex >= 0 && currentIndex < paths.size() && paths.at(currentIndex) == path)
        return currentIndex;
    const int next = paths.indexOf(path, qMax(0, currentIndex + 1));
    return next >= 0 ? next : paths.indexOf(path);
}

HandOff planHandOff(const QStringList &paths, int startIndex, const QString &contextUri, int boundCount)
{
    if(startIndex < 0 || startIndex >= paths.size())
        return HandOff();
    const QString startId = trackIdOf(paths.at(startIndex));
    if(startId.isEmpty())
        return HandOff();
    if(!contextUri.isEmpty() && startIndex < boundCount)
        return HandOff::context(contextUri, startId);
    QStringList run;
    for(int i = startIndex; i < paths.size(); ++i)
    {
        const QString id = trackIdOf(paths.at(i));
        if(id.isEmpty())
            break;
        run << id;
    }
    return HandOff::run(run);
}
```

Add `handoff.cpp` to `add_library(spotifylogic STATIC ...)` in `src/logic/CMakeLists.txt`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build -R tst_handoff --output-on-failure`
Expected: `tst_handoff` passes.

- [ ] **Step 5: Commit**

```bash
git add src/logic/handoff.h src/logic/handoff.cpp src/logic/CMakeLists.txt tests/tst_handoff.cpp tests/CMakeLists.txt
git commit -F - <<'EOF'
Plan hand-offs: a bound playlist's context or a run of tracks

Claude-Session: https://claude.ai/code/session_01XwRkSvwXRWC8M6LJ7WrpgE
EOF
```

---

### Task 3: Engine hands off and follows; queueing removed

The engine starts a hand-off (one request) instead of a single track. While the device plays inside the hand-off, it neither sends requests nor reports finished. It reports finished only when the device leaves a run, or when the device stops and rewinds the run's last track. Removed: `apiQueue`, `apiNext`, `queueFailed`, the queue state (`Queue`, `m_nextQueue`), `skipOnDevice`/`m_skippingToQueued`, `maybeQueueNext`, `kQueueLeadMs`, the "device did not advance" skip, `tick()` (Task 4 brings it back for portions), `activeGeneration()`, and `SpotifyApi::play/queue/next`.

**Where the B7–B10 invariants hold after the removal:**

- *Rate-limit gate, budgets, command timeout:* every start goes through `SpotifyApi::playContext`/`playUris` → `request(..., Kind::Command, handle)`, the same gate, interactive budget and 5 s timeout as the old `play`. Nothing bypasses `request()`.
- *Start coalescing:* every hand-off start goes through `sendStart` → `flushStart` with the unchanged 1 s gap. `skimmingStaysUnderBudget` and `heldNextKeyStaysUnderBudget` stay green.
- *Superseded start aborted:* `cancelStartRequest()` is still called from `play/stop/pause/fail/flushPendingPause`. The glue keeps the handle of every `apiStart` in `m_startRequest`.
- *Bounded retries / no loops:* the engine sends a start only for a qmmp `play()` (user, follower, qmmp skip), or once per `finish()`. `finish()` replaces `m_current` and sets awaiting, so later polls cannot repeat it (`qmmpNextStartsOnceWhenRunEnds`). Following sends nothing, because the follower's stop+play adopts (`followsHandOffWithoutRequests`). The queue-once rule is gone along with the queue.
- *Polling:* unchanged. The post-end poll is what shows the device on the next track.
- *Unavailable marker:* a marked start still fails at once without a request (`unavailableTrackFailsWithoutRequest`, `unavailableNextIsSkippedAtTrackEnd`).

**Files:**
- Modify: `src/logic/enginelogic.h`, `src/logic/enginelogic.cpp` (full replacement below)
- Modify: `src/engine/spotifyengine.h`, `src/engine/spotifyengine.cpp`
- Modify: `src/core/spotifyclient.h`
- Modify: `src/core/spotifyapi.h`, `src/core/spotifyapi.cpp` (remove `play`, `queue`, `next`)
- Modify: `src/logic/pollpolicy.cpp:13-17`, `src/core/spotifypoller.cpp:57,72` (comments only)
- Test: `tests/tst_enginelogic.cpp`

**Interfaces:**
- Consumes: `HandOff`, `HandOffRequest` (Task 2); `SpotifyApi::playContext/playUris` (Task 1).
- Produces:
  - `EngineSink::planHandOff(const QString &path) -> HandOff` and `EngineSink::apiStart(const HandOffRequest &request, quint64 generation)` replace `apiPlay`, `apiQueue` and `apiNext`.
  - `EngineLogic::trackIds()` returns only the current track.
  - `SpotifyClient::setHandOffPlanner(HandOffPlanner planner)` and `HandOff SpotifyClient::planHandOff(const QString &path) const`, with `using HandOffPlanner = std::function<HandOff(const QString &path)>;`. Without a planner the result is an empty `HandOff`, and the engine then hands over just the started track. Task 6 registers the real planner.

- [ ] **Step 1: Update the tests (they fail to compile until Step 3)**

In `tests/tst_enginelogic.cpp`:

1. Add `#include <QHash>` after `#include <QtTest>`, and replace the `FakeSink` class with:

```cpp
class FakeSink : public EngineSink
{
public:
    QStringList calls;
    quint64 lastGeneration = 0;
    QString lastError;
    // the delay of the last scheduleStartFlush(); -1: none
    qint64 startFlushMs = -1;
    int cancels = 0;
    // hand-offs by path; a missing one is empty (the engine then hands over just the track)
    QHash<QString, HandOff> plans;

    HandOff planHandOff(const QString &path) override { return plans.value(path); }
    void apiStart(const HandOffRequest &request, quint64 generation) override
    {
        if(request.contextUri.isEmpty())
            calls << u"play %1 %2"_s.arg(request.uris.join(u","_s)).arg(request.positionMs);
        else
            calls << u"play %1 at %2 %3"_s.arg(request.contextUri, request.offsetUri).arg(request.positionMs);
        lastGeneration = generation;
    }
    void apiPause() override { calls << u"pause"_s; }
    void apiResume() override { calls << u"resume"_s; }
    void apiSeek(qint64 positionMs) override { calls << u"seek %1"_s.arg(positionMs); }
    void suppressFollow(const QString &trackId) override { calls << u"suppress "_s + trackId; }
    void reportState(EngineState state) override
    {
        static const QStringList names = { u"Stopped"_s, u"Buffering"_s, u"Playing"_s, u"Paused"_s };
        calls << u"state "_s + names.at(int(state));
    }
    void reportTrack(const QString &path, const SpotifyTrack &track) override { calls << u"track %1 %2"_s.arg(path, track.title); }
    void reportFinished() override { calls << u"finished"_s; }
    void reportError(const QString &message, bool fatal) override
    {
        calls << (fatal ? u"fatal"_s : u"error"_s);
        lastError = message;
    }
    void reportNotice(const QString &message) override { calls << u"notice "_s + message; }
    void scheduleStartFlush(qint64 delayMs) override { startFlushMs = delayMs; }
    void cancelStartRequest() override { cancels++; }
    void pollAfter(qint64 delayMs) override { calls << u"poll after %1"_s.arg(delayMs); }
};
```

(A single-track run prints exactly the old `play spotify:track:<id> <ms>` string, so the existing expectations stay valid.)

2. Replace `deviceState` with:

```cpp
PlayerState deviceState(const QString &device, const QString &trackId, qint64 progressMs, bool isPlaying = true,
                        const QString &contextUri = QString())
{
    PlayerState state;
    state.hasPlayback = true;
    state.deviceId = device;
    state.isPlaying = isPlaying;
    state.progressMs = progressMs;
    state.contextUri = contextUri;
    state.track.id = trackId;
    state.track.title = u"Title "_s + trackId;
    state.track.durationMs = 200000;
    return state;
}
```

3. Change the helper declaration to `void startPlaying(const QString &trackId, const QString &contextUri = QString());` and replace its body. Tracks now start through a real hand-off, so the engine knows the run or context. Previously it adopted the track and never planned one.

```cpp
// qmmp starts the track (a hand-off) and the device confirms it; the next start is not held
// back by the start gap
void TestEngineLogic::startPlaying(const QString &trackId, const QString &contextUri)
{
    QVERIFY(m_logic->enqueue(u"spotify://track/"_s + trackId));
    m_logic->play();
    m_now += EngineLogic::kStartGapMs;
    m_logic->updateState(deviceState(u"dev"_s, trackId, 0, true, contextUri));
    QCOMPARE(m_logic->state(), EngineState::Playing);
    m_sink.calls.clear();
}
```

4. In `init()`, add `m_sink.plans.clear();` after `m_sink.calls.clear();`.

5. Delete these slots (declaration and body): `queuesNextAndSwitchesGaplessly`, `startsNextWhenDeviceDidNotAdvance`, `fallsBackToPlayWhenQueueFailed`, `failedQueueIsNotResent`, `fallbackAtTrackEndRunsOnce`, `enqueueDoesNotQueueImmediately`, `tickQueuesOnceAtLeadTime`, `tickDoesNotQueueWhilePaused`, `updateStateQueuesInsideWindow`, `switchingBeforeLeadTimeNeverQueuesAbandonedNext`, `failedSkipToQueuedTrackStops`, `skipToQueuedTrackTimesOut`.

6. In `unavailableNextIsSkippedAtTrackEnd`, delete the line `m_logic->tick();`.

7. Add slot declarations (after `pauseIgnoresStalePoll();`):

```cpp
    void startsContextAtTrack();
    void startsRunFromTrack();
    void followsHandOffWithoutRequests();
    void contextEndIsFollowed();
    void runFinishesAfterItsLastTrack();
    void qmmpNextStartsOnceWhenRunEnds();
    void adoptedPlaybackIsNeverFinished();
    void adoptDropsHandOffTheDeviceLeft();
```

and bodies:

```cpp
void TestEngineLogic::startsContextAtTrack()
{
    m_sink.plans.insert(u"spotify://track/b"_s, HandOff::context(u"spotify:playlist:p"_s, u"b"_s));
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s, 4000));
    m_logic->play();
    QCOMPARE(m_sink.calls, (QStringList { u"state Buffering"_s, u"play spotify:playlist:p at spotify:track:b 4000"_s }));
    QVERIFY(m_logic->isAwaitingStart());
}

void TestEngineLogic::startsRunFromTrack()
{
    m_sink.plans.insert(u"spotify://track/a"_s, HandOff::run({ u"a"_s, u"b"_s, u"c"_s }));
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    QCOMPARE(m_sink.calls, (QStringList { u"state Buffering"_s, u"play spotify:track:a,spotify:track:b,spotify:track:c 0"_s }));
}

// the device moved on inside the hand-off: no request, no finish; the follower's stop and play
// adopt the track the device plays
void TestEngineLogic::followsHandOffWithoutRequests()
{
    m_sink.plans.insert(u"spotify://track/a"_s, HandOff::context(u"spotify:playlist:p"_s, u"a"_s));
    startPlaying(u"a"_s, u"spotify:playlist:p"_s);
    m_now += 198500;
    m_logic->updateState(deviceState(u"dev"_s, u"b"_s, 300, true, u"spotify:playlist:p"_s));
    QVERIFY(m_sink.calls.isEmpty());
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_logic->play();
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls, (QStringList { u"state Stopped"_s, u"state Playing"_s, u"track spotify://track/b Title b"_s }));
}

// Spotify ends a context its own way (Autoplay, repeat, or it stops): qmmp neither moves on nor
// sends anything; a stopped device shows as paused
void TestEngineLogic::contextEndIsFollowed()
{
    m_sink.plans.insert(u"spotify://track/a"_s, HandOff::context(u"spotify:playlist:p"_s, u"a"_s));
    startPlaying(u"a"_s, u"spotify:playlist:p"_s);
    m_now += 198500;
    m_logic->updateState(deviceState(u"dev"_s, u"r"_s, 1000));
    QVERIFY(m_sink.calls.isEmpty());
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 0, false, u"spotify:playlist:p"_s));
    QCOMPARE(m_sink.calls, QStringList { u"state Paused"_s });
}

void TestEngineLogic::runFinishesAfterItsLastTrack()
{
    m_sink.plans.insert(u"spotify://track/a"_s, HandOff::run({ u"a"_s, u"b"_s }));
    startPlaying(u"a"_s);
    m_now += 198500;
    m_logic->updateState(deviceState(u"dev"_s, u"b"_s, 300));
    QVERIFY(m_sink.calls.isEmpty());
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_logic->play();
    m_sink.calls.clear();
    m_now += 198500;
    m_logic->updateState(deviceState(u"dev"_s, u"r"_s, 1000));
    QCOMPARE(m_sink.calls, (QStringList { u"suppress r"_s, u"finished"_s, u"state Stopped"_s }));
}

// qmmp's next item is announced but never sent ahead; when a run ends and qmmp's next is a
// Spotify track (qmmp's shuffle picked it), it starts once
void TestEngineLogic::qmmpNextStartsOnceWhenRunEnds()
{
    startPlaying(u"a"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/c"_s));
    QCOMPARE(m_logic->trackIds(), QStringList { u"a"_s });
    QVERIFY(m_sink.calls.isEmpty());
    m_now += 198500;
    for(int i = 0; i < 10; i++)
    {
        m_now += 250;
        m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 0, false));
    }
    QCOMPARE(m_sink.calls.mid(0, 4), (QStringList { u"finished"_s, u"state Stopped"_s, u"state Buffering"_s,
                                                   u"play spotify:track:c 0"_s }));
    QCOMPARE(m_sink.calls.count(u"play spotify:track:c 0"_s), 1);
}

// playback qmmp did not hand over (attached at startup) is Spotify's: qmmp never finishes it
void TestEngineLogic::adoptedPlaybackIsNeverFinished()
{
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 0));
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    m_sink.calls.clear();
    m_now += 198500;
    m_logic->updateState(deviceState(u"dev"_s, u"r"_s, 1000));
    QVERIFY(m_sink.calls.isEmpty());
}

// the follower moved qmmp to a track of another context: the old run no longer applies
void TestEngineLogic::adoptDropsHandOffTheDeviceLeft()
{
    m_sink.plans.insert(u"spotify://track/a"_s, HandOff::run({ u"a"_s, u"b"_s }));
    startPlaying(u"a"_s);
    m_logic->updateState(deviceState(u"dev"_s, u"x"_s, 300, true, u"spotify:album:z"_s));
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/x"_s));
    m_logic->play();
    m_sink.calls.clear();
    m_now += 198500;
    m_logic->updateState(deviceState(u"dev"_s, u"y"_s, 1000, true, u"spotify:album:z"_s));
    QVERIFY(m_sink.calls.isEmpty());
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build -j 2>&1 | grep -E "error" | head -5`
Expected: compile errors in `tst_enginelogic.cpp`: `'HandOff' was not declared`, `marked 'override', but does not override` (planHandOff/apiStart).

- [ ] **Step 3: Implement the engine logic**

Replace `src/logic/enginelogic.h` with:

```cpp
#ifndef ENGINELOGIC_H
#define ENGINELOGIC_H

#include <functional>
#include <QString>
#include <QStringList>
#include "handoff.h"
#include "spotifytypes.h"

enum class EngineState { Stopped, Buffering, Playing, Paused };

class EngineSink
{
public:
    virtual ~EngineSink() = default;
    // what starting the playlist item at path hands to Spotify; empty: just that track
    virtual HandOff planHandOff(const QString &path) = 0;
    virtual void apiStart(const HandOffRequest &request, quint64 generation) = 0;
    virtual void apiPause() = 0;
    virtual void apiResume() = 0;
    virtual void apiSeek(qint64 positionMs) = 0;
    // the follower must not pull qmmp back to this track
    virtual void suppressFollow(const QString &trackId) = 0;
    virtual void reportState(EngineState state) = 0;
    virtual void reportTrack(const QString &path, const SpotifyTrack &track) = 0;
    virtual void reportFinished() = 0;
    // an empty message fails quietly (an unavailable track skipped, a logout)
    virtual void reportError(const QString &message, bool fatal) = 0;
    virtual void reportNotice(const QString &message) = 0;
    // a coalesced start is due in delayMs: call flushStart() then
    virtual void scheduleStartFlush(qint64 delayMs) = 0;
    // the last start request is superseded: if it has not been answered, it must never reach
    // the device (a request stuck offline)
    virtual void cancelStartRequest() = 0;
    // a poll in delayMs, also after polling stopped for "nothing active"
    virtual void pollAfter(qint64 delayMs) = 0;
};

// Starts a hand-off (a context or a run of tracks) on the device and mirrors what it plays.
// Spotify plays on by itself: qmmp hears of a track end only when a run is over; inside the
// hand-off the follower moves qmmp's cursor and the engine adopts the track without a request.
class EngineLogic
{
public:
    using Clock = std::function<qint64()>;

    static constexpr qint64 kStartTimeoutMs = 10000;
    // seeks are sent this long after the last one of a burst (a held key, a scrolled slider)
    static constexpr qint64 kSeekDebounceMs = 300;
    // hand-offs go out at most once per this long; the latest of a burst (a held Next key)
    // follows when it ends
    static constexpr qint64 kStartGapMs = 1000;

    EngineLogic(EngineSink *sink, Clock clock);

    void setSelectedDevice(const QString &deviceId);
    bool enqueue(const QString &path, qint64 offsetMs = 0);
    void play();
    void pause();
    void seek(qint64 positionMs);
    // sends the pending seek once kSeekDebounceMs passed since the last seek()
    void flushSeek();
    // sends the coalesced start once kStartGapMs passed since the last one was sent
    void flushStart();
    void stop();
    void flushPendingPause();
    void updateState(const PlayerState &state);
    void playFailed(quint64 generation, const QString &message, bool systemic);
    void checkStartTimeout();
    // logged out: qmmp stops without a request and without an error message
    void sessionEnded();

    EngineState state() const { return m_state; }
    qint64 elapsed() const;
    // the track qmmp plays (none or one): the follower leaves it alone
    QStringList trackIds() const;
    bool isAwaitingStart() const { return m_awaiting; }

private:
    struct Entry
    {
        QString path;
        QString id;
        qint64 offsetMs = 0;
        // not playable in the user's country: failed at once, without a request
        bool unavailable = false;

        bool isEmpty() const { return id.isEmpty(); }
    };

    void setState(EngineState state);
    void apply(const PlayerState &state);
    void startOnDevice(qint64 positionMs);
    void fail(const QString &message, bool systemic);
    void switchToNext();
    void finish();
    void deviceStopped();
    void sendStart(const HandOffRequest &request);
    bool nearEnd() const;
    bool inCommandGrace() const;
    bool deviceOnCurrentTrack() const;

    EngineSink *m_sink;
    Clock m_clock;
    QString m_device;
    QString m_deviceName;
    PlayerState m_last;
    Entry m_current;
    // qmmp's next item, announced ~7 s before the end; never sent ahead. It starts only when a
    // run ends while qmmp's next is a Spotify track (qmmp's shuffle picked it, or a portion
    // hand-over was lost)
    Entry m_next;
    // what the device was given; empty for playback qmmp only adopted (Spotify's own)
    HandOff m_handOff;
    bool m_awaiting = false;
    bool m_trackReported = false;
    EngineState m_state = EngineState::Stopped;
    SpotifyTrack m_track;
    qint64 m_progressMs = 0;
    qint64 m_progressAt = 0;
    qint64 m_commandAt = -1;
    // the last seek() of a burst, sent by flushSeek(); -1: none
    qint64 m_pendingSeekMs = -1;
    qint64 m_seekAt = 0;
    // the coalesced start
    bool m_startPending = false;
    HandOffRequest m_pendingStart;
    qint64 m_startSentAt = -1;
    bool m_pendingPause = false;
    // the pause play() cancelled, restored if that play fails
    bool m_cancelledPause = false;
    // start timeouts since the device last started one of our tracks: the second one stops qmmp
    int m_startTimeouts = 0;
    QString m_stoppedId;
    quint64 m_generation = 0;
    quint64 m_activeGeneration = 0;
    qint64 m_playSentAt = 0;
};

#endif
```

Replace `src/logic/enginelogic.cpp` with:

```cpp
#include "enginelogic.h"
#include "spotifylink.h"

using namespace Qt::StringLiterals;

namespace {

// polls that arrive this soon after a pause/resume/seek may predate it
const qint64 kCommandGraceMs = 2500;
// how close to the end a track must be for "stopped" or "moved on" to mean "finished"
const qint64 kEndWindowMs = 3000;
// the device rewinds to 0 when the last track it was given ends
const qint64 kRewoundMs = 1500;

}

EngineLogic::EngineLogic(EngineSink *sink, Clock clock) : m_sink(sink), m_clock(std::move(clock))
{}

void EngineLogic::setSelectedDevice(const QString &deviceId)
{
    if(deviceId != m_device)
        m_deviceName.clear();
    m_device = deviceId;
}

// the first source is the track to play; a later one is qmmp's next item (see m_next)
bool EngineLogic::enqueue(const QString &path, qint64 offsetMs)
{
    const SpotifyLink link = SpotifyLink::parse(path);
    if(link.kind != SpotifyLink::Track)
        return false;

    const Entry entry { path, link.id, qMax<qint64>(0, offsetMs), link.unavailable };
    if(m_current.isEmpty())
        m_current = entry;
    else
        m_next = entry;
    return true;
}

void EngineLogic::play()
{
    m_sink->cancelStartRequest();
    // a play in the same call stack as stop() (restart, Next) supersedes the pause stop() deferred
    m_cancelledPause = m_pendingPause;
    m_pendingPause = false;
    // a seek or a start not yet sent for the previous track is moot
    m_pendingSeekMs = -1;
    m_startPending = false;
    const bool restart = !m_current.isEmpty() && m_current.id == m_stoppedId;
    m_stoppedId.clear();
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
    m_commandAt = -1;
    m_activeGeneration = 0;
    if(!restart && m_current.offsetMs == 0 && deviceOnCurrentTrack() && m_last.isPlaying)
    {
        // the device got there by itself (the follower moves qmmp after it): the hand-off
        // stays only while the device still plays it
        if(!m_handOff.matches(m_last.contextUri, m_current.id))
            m_handOff = HandOff();
        apply(m_last);
        return;
    }
    m_progressMs = m_current.offsetMs;
    m_progressAt = m_clock();
    setState(EngineState::Buffering);
    startOnDevice(m_current.offsetMs);
}

void EngineLogic::pause()
{
    if(m_state == EngineState::Playing)
    {
        m_progressMs = elapsed();
        m_progressAt = m_clock();
        m_commandAt = m_clock();
        m_sink->cancelStartRequest();
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
    m_pendingSeekMs = positionMs;
    m_seekAt = m_clock();
}

void EngineLogic::flushSeek()
{
    if(m_pendingSeekMs < 0 || m_clock() - m_seekAt < kSeekDebounceMs)
        return;
    const qint64 positionMs = m_pendingSeekMs;
    m_pendingSeekMs = -1;
    // polls from before the seek reached the device show the old position
    m_commandAt = m_clock();
    m_sink->apiSeek(positionMs);
}

void EngineLogic::flushStart()
{
    if(!m_startPending)
        return;
    const qint64 wait = m_startSentAt < 0 ? 0 : m_startSentAt + kStartGapMs - m_clock();
    if(wait > 0)
    {
        // early: the pending start must not be lost
        m_sink->scheduleStartFlush(wait);
        return;
    }
    m_startPending = false;
    m_startSentAt = m_clock();
    // the start timeout counts from the request actually sent
    m_playSentAt = m_clock();
    m_sink->apiStart(m_pendingStart, m_activeGeneration);
}

void EngineLogic::stop()
{
    // SoundCore::play() stops again after MediaPlayer::stop(); that second stop must not wipe
    // the first one's restart candidate and pending pause
    if(m_current.isEmpty())
        return;
    m_sink->cancelStartRequest();
    // m_state is our belief; m_last may predate a resume we just sent. A paused device is left alone (403).
    const bool active = m_awaiting || (m_state == EngineState::Playing && deviceOnCurrentTrack());
    m_pendingPause = active;
    m_stoppedId = m_current.id;
    m_activeGeneration = 0;
    m_current = Entry();
    m_pendingSeekMs = -1;
    m_startPending = false;
    m_next = Entry();
    m_awaiting = false;
    m_trackReported = false;
    m_track = SpotifyTrack();
    m_commandAt = -1;
    // m_handOff stays: the follower's stop and play of the track the device moved to adopt it
    setState(EngineState::Stopped);
}

void EngineLogic::updateState(const PlayerState &state)
{
    m_last = state;
    const bool onDevice = state.hasPlayback && state.deviceId == m_device;
    if(onDevice && !state.deviceName.isEmpty())
        m_deviceName = state.deviceName;
    if(m_current.isEmpty() || m_state == EngineState::Stopped)
        return;

    const bool ending = !m_awaiting && m_state == EngineState::Playing && nearEnd();
    if(onDevice && state.track.id == m_current.id)
    {
        // a run is over when the device stopped and rewound its last track; a context that
        // stopped stays on its last track, paused
        if(ending && !state.isPlaying && state.progressMs < kRewoundMs && m_handOff.isRun())
            finish();
        else
            apply(state);
    }
    // the device moved on: inside the hand-off (and after a context) the follower moves qmmp's
    // cursor; only a device that left a run makes qmmp move on
    else if(ending && m_handOff.leftRun(onDevice ? state.track.id : QString()))
    {
        finish();
    }
    else if(!onDevice && !m_awaiting && m_state == EngineState::Playing)
    {
        if(!inCommandGrace())
            deviceStopped();
        // polling stops on "nothing active": look again when the poll can be trusted
        else if(!state.hasPlayback)
            m_sink->pollAfter(m_commandAt + kCommandGraceMs - m_clock());
    }
}

void EngineLogic::flushPendingPause()
{
    m_stoppedId.clear();
    if(!m_pendingPause)
        return;
    m_pendingPause = false;
    m_sink->cancelStartRequest();
    m_sink->apiPause();
}

void EngineLogic::playFailed(quint64 generation, const QString &message, bool systemic)
{
    if(generation == 0 || generation != m_activeGeneration)
        return;
    fail(message, systemic);
}

void EngineLogic::checkStartTimeout()
{
    if(!m_awaiting || m_activeGeneration == 0 || m_clock() - m_playSentAt < kStartTimeoutMs)
        return;
    // one silent track is skipped; a device that starts none of two in a row is the problem
    const bool systemic = ++m_startTimeouts >= 2;
    if(systemic)
        m_startTimeouts = 0;
    fail(u"Spotify: the device did not start playing"_s, systemic);
}

// the device keeps playing: a pause could not be sent without a token anyway
void EngineLogic::sessionEnded()
{
    m_pendingPause = false;
    m_cancelledPause = false;
    m_stoppedId.clear();
    if(!m_current.isEmpty())
        fail(QString(), true);
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
    return m_current.isEmpty() ? QStringList() : QStringList { m_current.id };
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
    m_cancelledPause = false;
    m_startTimeouts = 0;
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
}

void EngineLogic::startOnDevice(qint64 positionMs)
{
    // qmmp skips it like a track Spotify refused, without the message
    if(m_current.unavailable)
    {
        fail(QString(), false);
        return;
    }
    m_handOff = m_sink->planHandOff(m_current.path);
    if(m_handOff.isEmpty())
        m_handOff = HandOff::run({ m_current.id });
    m_awaiting = true;
    m_activeGeneration = ++m_generation;
    sendStart(m_handOff.start(positionMs));
}

// the first start of a burst goes out at once; later ones within kStartGapMs replace each other
// and the latest follows when the gap ends (a held Next key sends about one start per second)
void EngineLogic::sendStart(const HandOffRequest &request)
{
    m_pendingStart = request;
    m_startPending = true;
    flushStart();
}

// a systemic failure (FatalError) stops qmmp; a track-specific one (NormalError) makes qmmp
// skip to the next track, and qmmp itself stops after a whole playlist of failures. The
// pause the failed start cancelled comes back: the next play cancels it again, qmmp's stop
// sends it, so the previous track does not keep playing on the device.
void EngineLogic::fail(const QString &message, bool systemic)
{
    m_sink->cancelStartRequest();
    m_pendingPause = m_cancelledPause;
    m_cancelledPause = false;
    m_current = Entry();
    m_pendingSeekMs = -1;
    m_startPending = false;
    m_next = Entry();
    m_handOff = HandOff();
    m_awaiting = false;
    m_trackReported = false;
    m_activeGeneration = 0;
    m_state = EngineState::Stopped;
    m_sink->reportError(message, systemic);
}

// qmmp moves on to its next item (MediaPlayer's playNext finds it already given to the engine)
void EngineLogic::switchToNext()
{
    m_sink->reportFinished();
    m_sink->reportState(EngineState::Stopped);
    m_current = m_next;
    m_pendingSeekMs = -1;
    m_startPending = false;
    m_next = Entry();
    m_trackReported = false;
    m_track = SpotifyTrack();
    m_progressMs = 0;
    m_progressAt = m_clock();
    m_commandAt = -1;
    m_state = EngineState::Buffering;
    m_sink->reportState(EngineState::Buffering);
    startOnDevice(0);
}

// a run is over
void EngineLogic::finish()
{
    if(!m_next.isEmpty())
    {
        // an unavailable next track is skipped: if qmmp finds nothing playable after it, its
        // stop pauses the device
        if(m_next.unavailable)
            m_cancelledPause = m_last.hasPlayback && m_last.deviceId == m_device;
        switchToNext();
        return;
    }
    // qmmp moves on to a non-Spotify item or its playlist ended: the device must not keep
    // playing (Spotify's Autoplay may already have started a recommendation). The pause is
    // deferred like stop()'s: if qmmp plays a Spotify track next, that play() cancels it.
    if(m_last.hasPlayback && m_last.deviceId == m_device)
    {
        m_pendingPause = true;
        m_sink->suppressFollow(m_last.track.id);
    }
    m_current = Entry();
    m_handOff = HandOff();
    m_pendingSeekMs = -1;
    m_startPending = false;
    m_awaiting = false;
    m_trackReported = false;
    m_sink->reportFinished();
    setState(EngineState::Stopped);
}

// the device was closed or playback moved elsewhere: stop the clock and say so once;
// a later poll of our track playing on the device resumes following through apply()
void EngineLogic::deviceStopped()
{
    m_progressMs = elapsed();
    m_progressAt = m_clock();
    setState(EngineState::Paused);
    m_sink->reportNotice(u"Spotify: playback stopped on %1"_s.arg(m_deviceName.isEmpty() ? u"the device"_s : m_deviceName));
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

- [ ] **Step 4: Glue: client planner hook, engine sink, API removal, comments**

`src/core/spotifyclient.h`: add `#include <functional>` and `#include "handoff.h"`. In the public section after `QString suppressedTrack() const ...`:

```cpp
    using HandOffPlanner = std::function<HandOff(const QString &path)>;
    // the general plugin plans hand-offs from the playing playlist; without it a start hands
    // over just its track
    void setHandOffPlanner(HandOffPlanner planner) { m_planner = std::move(planner); }
    HandOff planHandOff(const QString &path) const { return m_planner ? m_planner(path) : HandOff(); }
```

and the member `HandOffPlanner m_planner;` after `QString m_suppressedTrack;`.

`src/engine/spotifyengine.h`: replace the three declarations `apiPlay`, `apiQueue`, `apiNext` with:

```cpp
    HandOff planHandOff(const QString &path) override;
    void apiStart(const HandOffRequest &request, quint64 generation) override;
```

and change the member comment to `// the last hand-off start sent`.

`src/engine/spotifyengine.cpp`:

- In the ticker lambda, remove `m_logic.tick();` (keep the elapsed dispatch):

```cpp
    connect(&m_ticker, &QTimer::timeout, this, [this] { StateHandler::instance()->dispatch(m_logic.elapsed(), 0); });
```

- Delete `SpotifyEngine::apiPlay`, `SpotifyEngine::apiQueue`, `SpotifyEngine::apiNext`. Add:

```cpp
HandOff SpotifyEngine::planHandOff(const QString &path)
{
    return m_client->planHandOff(path);
}

void SpotifyEngine::apiStart(const HandOffRequest &request, quint64 generation)
{
    QPointer<SpotifyEngine> self(this);
    SpotifyClient *client = m_client;
    const SpotifyApi::Callback done = [self, client, generation](const ApiResult &result) {
        client->poller()->pollSoon();
        if(self && !result.ok)
        {
            // a track not available in the user's country is skipped without a message
            const bool restricted = isRestrictedTrackReply(result.status, result.message);
            self->m_logic.playFailed(generation, restricted ? QString() : result.message,
                                     isSystemicPlayFailure(result.status, result.deviceProblem, result.reason));
            self->publishTracks();
        }
    };
    const QString device = m_client->deviceId();
    if(request.contextUri.isEmpty())
        m_startRequest = m_client->api()->playUris(device, request.uris, request.offsetUri, request.positionMs, done);
    else
        m_startRequest = m_client->api()->playContext(device, request.contextUri, request.offsetUri, request.positionMs, done);
    armStartTimeout();
}
```

- Update the comment above `armStartTimeout()` to: `// the device may land on something else than our track; without the timeout qmmp would wait for it for good, polling every 2 s`.

`src/core/spotifyapi.h` / `.cpp`: delete `play(...)`, `queue(...)` and `next(...)` (declarations and definitions). Update the header comment above the playback commands to: `// playback commands (playContext … setVolume, shuffle, repeat) time out after 5 s, other requests after 15 s`.

`src/logic/pollpolicy.cpp` comments:

```cpp
// the extra poll before a track ends refreshes the engine's clock ahead of the end (and of a
// long run's portion hand-over 2.5 s before it)
const qint64 kBeforeEndMs = 5000;
// the extra poll after a track ends shows the device on its next track: the follower moves
// qmmp's cursor, or the engine sees a run end
const qint64 kAfterEndMs = 1500;
```

`src/core/spotifypoller.cpp`: comment at line 57 → `// automatic requests (start replies, track ends) do not restart a lost connection`; line 72 → `// failing commands (a start while offline) must not shorten the backoff`.

- [ ] **Step 5: Run the tests and a clean build**

Run: `cmake --build build -j --clean-first 2>&1 | grep -E "warning|error"; ctest --test-dir build --output-on-failure`
Expected: no warnings or errors; all suites pass, including `skimmingStaysUnderBudget`, `heldNextKeyStaysUnderBudget`, `supersededStartIsCancelled` and `unavailableNextIsSkippedAtTrackEnd`.
Then verify that no queueing remains: `grep -rn -e "apiQueue" -e "apiNext" -e "queueFailed" -e "/me/player/queue" -e "/me/player/next" src tools tests` → no output.

- [ ] **Step 6: Install and commit**

Run: `cmake --install build` (for Alex's batch of manual checks).

```bash
git add src/logic/enginelogic.h src/logic/enginelogic.cpp src/engine/spotifyengine.h src/engine/spotifyengine.cpp src/core/spotifyclient.h src/core/spotifyapi.h src/core/spotifyapi.cpp src/logic/pollpolicy.cpp src/core/spotifypoller.cpp tests/tst_enginelogic.cpp
git commit -F - <<'EOF'
Hand off contexts and runs, follow the device, drop queueing

Claude-Session: https://claude.ai/code/session_01XwRkSvwXRWC8M6LJ7WrpgE
EOF
```

Manual checks pending (for Task 9's batch): mixed playlist run → local file → next run.

---

### Task 4: Long runs in portions

**Files:**
- Modify: `src/logic/enginelogic.h`, `src/logic/enginelogic.cpp`
- Modify: `src/engine/spotifyengine.cpp` (ticker)
- Test: `tests/tst_enginelogic.cpp`

**Interfaces:**
- Consumes: `HandOff::portionEndId()`, `HandOff::takeNextPortion()` (Task 2).
- Produces: `static constexpr qint64 EngineLogic::kPortionLeadMs = 2500;`, `void EngineLogic::tick();` (called every 250 ms by the engine's ticker).

**Bounded:** `takeNextPortion()` advances before the request is sent, so each portion goes out at most once. There is no retry. If the hand-over is lost, the device stops and rewinds the portion's last track. `finish()` then starts qmmp's next item, which is the next portion's first track, once. A failed hand-over goes through `playFailed` → `fail()`: systemic stops qmmp, a track-specific failure makes qmmp skip.

- [ ] **Step 1: Write the failing tests**

In `tests/tst_enginelogic.cpp`, add to the anonymous namespace:

```cpp
QStringList manyIds(int count)
{
    QStringList ids;
    for(int i = 0; i < count; ++i)
        ids << u"t%1"_s.arg(i);
    return ids;
}
```

a private helper declaration `void playLastTrackOfPortion();` (next to `startPlaying`), slots:

```cpp
    void handsOverNextPortionBeforeEnd();
    void lostPortionHandOverStartsQmmpNext();
    void failedPortionHandOverSkips();
    void noPortionHandOverForContextShortRunOrPause();
```

and bodies:

```cpp
// a 501-track run: qmmp followed the device to the first portion's last track
void TestEngineLogic::playLastTrackOfPortion()
{
    m_sink.plans.insert(u"spotify://track/t0"_s, HandOff::run(manyIds(501)));
    startPlaying(u"t0"_s);
    m_logic->updateState(deviceState(u"dev"_s, u"t499"_s, 0));
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/t499"_s));
    m_logic->play();
    m_sink.calls.clear();
}

void TestEngineLogic::handsOverNextPortionBeforeEnd()
{
    playLastTrackOfPortion();
    m_now += 200000 - EngineLogic::kPortionLeadMs - 1;
    m_logic->tick();
    QVERIFY(m_sink.calls.isEmpty());
    m_now += 1;
    m_logic->tick();
    m_logic->tick();
    QCOMPARE(m_sink.calls, QStringList { u"play spotify:track:t500 0"_s });
    m_logic->updateState(deviceState(u"dev"_s, u"t500"_s, 300));
    QCOMPARE(m_sink.calls.size(), 1);
}

void TestEngineLogic::lostPortionHandOverStartsQmmpNext()
{
    playLastTrackOfPortion();
    QVERIFY(m_logic->enqueue(u"spotify://track/t500"_s));
    m_now += 200000 - EngineLogic::kPortionLeadMs;
    m_logic->tick();
    m_now += 3000;
    m_logic->updateState(deviceState(u"dev"_s, u"t499"_s, 0, false));
    QCOMPARE(m_sink.calls, (QStringList { u"play spotify:track:t500 0"_s, u"finished"_s, u"state Stopped"_s,
                                          u"state Buffering"_s, u"play spotify:track:t500 0"_s }));
}

void TestEngineLogic::failedPortionHandOverSkips()
{
    playLastTrackOfPortion();
    m_now += 200000 - EngineLogic::kPortionLeadMs;
    m_logic->tick();
    m_logic->playFailed(m_sink.lastGeneration, u"Spotify: gone"_s, false);
    QCOMPARE(m_sink.calls.last(), u"error"_s);
    QVERIFY(m_logic->trackIds().isEmpty());
}

void TestEngineLogic::noPortionHandOverForContextShortRunOrPause()
{
    startPlaying(u"a"_s);
    m_now += 199000;
    m_logic->tick();
    QVERIFY(m_sink.calls.isEmpty());

    cleanup();
    init();
    m_sink.plans.insert(u"spotify://track/a"_s, HandOff::context(u"spotify:playlist:p"_s, u"a"_s));
    startPlaying(u"a"_s, u"spotify:playlist:p"_s);
    m_now += 199000;
    m_logic->tick();
    QVERIFY(m_sink.calls.isEmpty());

    cleanup();
    init();
    playLastTrackOfPortion();
    m_logic->pause();
    m_now += 199000;
    m_logic->tick();
    QCOMPARE(m_sink.calls, (QStringList { u"pause"_s, u"state Paused"_s }));
}
```

(`cleanup(); init();` inside the test gives each case a fresh engine and sink.)

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build -j 2>&1 | grep error | head -3`
Expected: `no member named 'tick' in 'EngineLogic'` / `no member named 'kPortionLeadMs'`.

- [ ] **Step 3: Implement**

`src/logic/enginelogic.h`: after `kStartGapMs`:

```cpp
    // the next portion of a long run goes out this long before the portion's last track ends
    // (cutting its last seconds, often silent, beats a gap)
    static constexpr qint64 kPortionLeadMs = 2500;
```

public, after `checkStartTimeout();`:

```cpp
    // hands over the next portion of a long run in time
    void tick();
```

private, after `sendStart(...)`: `void maybeSwitchPortion();`

`src/logic/enginelogic.cpp`: at the end of `updateState` (after the if/else chain) add:

```cpp
    // a poll may refresh the clock inside the lead window
    maybeSwitchPortion();
```

and add:

```cpp
void EngineLogic::tick()
{
    maybeSwitchPortion();
}

// the device plays the last track of a run's portion: the next portion goes out from its first
// track, once; the device then moves into it and the follower follows. A lost hand-over ends
// the run at this track and qmmp's next item (that portion's first track) starts instead.
void EngineLogic::maybeSwitchPortion()
{
    if(m_state != EngineState::Playing || m_awaiting || m_current.id != m_handOff.portionEndId())
        return;
    if(m_track.durationMs <= 0 || m_track.durationMs - elapsed() > kPortionLeadMs)
        return;
    m_sink->cancelStartRequest();
    m_activeGeneration = ++m_generation;
    m_startSentAt = m_clock();
    m_sink->apiStart(m_handOff.takeNextPortion(), m_activeGeneration);
}
```

`src/engine/spotifyengine.cpp`, ticker lambda:

```cpp
    connect(&m_ticker, &QTimer::timeout, this, [this] {
        m_logic.tick();
        StateHandler::instance()->dispatch(m_logic.elapsed(), 0);
    });
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: all pass, no warnings.

- [ ] **Step 5: Commit**

```bash
git add src/logic/enginelogic.h src/logic/enginelogic.cpp src/engine/spotifyengine.cpp tests/tst_enginelogic.cpp
git commit -F - <<'EOF'
Hand over long runs in portions of 500, 2.5 s before a portion ends

Claude-Session: https://claude.ai/code/session_01XwRkSvwXRWC8M6LJ7WrpgE
EOF
```

---

### Task 5: Bindings and follow decisions (pure)

**Files:**
- Create: `src/logic/binding.h`, `src/logic/binding.cpp`
- Modify: `src/logic/followerlogic.h`, `src/logic/followerlogic.cpp`, `src/logic/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/tst_binding.cpp`, `tests/tst_followerlogic.cpp`

**Interfaces:**
- Consumes: `SpotifyLink` (existing).
- Produces (used by Tasks 6, 7):
  - `struct Binding { QString contextUri; QString name; QStringList trackIds; QStringList appendedIds; bool isValid() const; bool isIntact(const QStringList &playlistTrackIds) const; }`
  - `struct SpotifyContext { enum Kind { None, Playlist, Album, Collection }; Kind kind; QString id; bool isValid() const; static SpotifyContext parse(const QString &uri); }`
  - `QString likedSongsContextUri(const QString &userId)`, `QString boundPlaylistName(const QString &name)`, `QString editedPlaylistName(const QString &name)`, `QStringList playlistTrackIds(const QStringList &paths)`, `QString bindingStatusLine(const Binding *binding)`, `QByteArray bindingsToJson(const QHash<QString, Binding> &bindings)`, `QHash<QString, Binding> bindingsFromJson(const QByteArray &json)`
  - `FollowDecision::LoadContext`. `decideFollow(..., int currentIndex, const QString &playlistContextUri = QString())`: the added last parameter has a default, so the current follower glue compiles unchanged until Task 7. Until then that glue treats `LoadContext` as an append.
  - `int findTrackIndex(const QStringList &playlistPaths, const QString &trackId, int currentIndex)`

- [ ] **Step 1: Write the failing tests**

`tests/tst_binding.cpp`:

```cpp
#include <QtTest>
#include "binding.h"

using namespace Qt::StringLiterals;

namespace {

Binding liked()
{
    Binding binding;
    binding.contextUri = u"spotify:user:u1:collection"_s;
    binding.name = u"Liked Songs"_s;
    binding.trackIds = { u"a"_s, u"b"_s };
    return binding;
}

}

class TestBinding : public QObject
{
    Q_OBJECT
private slots:
    void parsesContexts_data();
    void parsesContexts();
    void names();
    void trackIdsIgnoreMarker();
    void sameTracksStayIntact();
    void editsBreakIt();
    void appendedTracksKeepIt();
    void statusLine();
    void jsonRoundTrip();
    void badJsonGivesNothing();
};

void TestBinding::parsesContexts_data()
{
    QTest::addColumn<QString>("uri");
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("id");
    QTest::newRow("playlist") << u"spotify:playlist:37i9dQZF1DXcBWIGoYBM5M"_s << int(SpotifyContext::Playlist) << u"37i9dQZF1DXcBWIGoYBM5M"_s;
    QTest::newRow("legacy playlist") << u"spotify:user:alex:playlist:37i9dQZF1DXcBWIGoYBM5M"_s << int(SpotifyContext::Playlist) << u"37i9dQZF1DXcBWIGoYBM5M"_s;
    QTest::newRow("album") << u"spotify:album:1DFixLWuPkv3KT3TnV35m3"_s << int(SpotifyContext::Album) << u"1DFixLWuPkv3KT3TnV35m3"_s;
    QTest::newRow("liked songs") << u"spotify:user:u1:collection"_s << int(SpotifyContext::Collection) << QString();
    QTest::newRow("artist") << u"spotify:artist:0OdUWJ0sBjDrqHygGUXeCF"_s << int(SpotifyContext::None) << QString();
    QTest::newRow("show") << u"spotify:show:5CfCWKI5pZ28U0uOzXkDHe"_s << int(SpotifyContext::None) << QString();
    QTest::newRow("none") << QString() << int(SpotifyContext::None) << QString();
}

void TestBinding::parsesContexts()
{
    QFETCH(QString, uri);
    QFETCH(int, kind);
    QFETCH(QString, id);
    const SpotifyContext context = SpotifyContext::parse(uri);
    QCOMPARE(int(context.kind), kind);
    QCOMPARE(context.id, id);
    QCOMPARE(context.isValid(), kind != int(SpotifyContext::None));
}

void TestBinding::names()
{
    QCOMPARE(likedSongsContextUri(u"u1"_s), u"spotify:user:u1:collection"_s);
    QCOMPARE(boundPlaylistName(u"My India"_s), u"Spotify: My India"_s);
    QCOMPARE(editedPlaylistName(u"My India"_s), u"My India (edited)"_s);
}

void TestBinding::trackIdsIgnoreMarker()
{
    QCOMPARE(playlistTrackIds({ u"spotify://track/a"_s, u"spotify://track/b?unavailable"_s, u"/music/x.mp3"_s }),
             (QStringList { u"a"_s, u"b"_s, QString() }));
}

// qmmp regroups a playlist with a STRUCTURE change that leaves its tracks as they are
void TestBinding::sameTracksStayIntact()
{
    QVERIFY(liked().isIntact({ u"a"_s, u"b"_s }));
}

void TestBinding::editsBreakIt()
{
    QVERIFY(!liked().isIntact({ u"b"_s, u"a"_s }));
    QVERIFY(!liked().isIntact({ u"a"_s }));
    QVERIFY(!liked().isIntact({ u"a"_s, u"b"_s, QString() }));
    QVERIFY(!Binding().isIntact({}));
}

void TestBinding::appendedTracksKeepIt()
{
    Binding binding = liked();
    binding.appendedIds << u"n"_s;
    QVERIFY(binding.isIntact({ u"a"_s, u"b"_s, u"n"_s }));
    QVERIFY(!binding.isIntact({ u"a"_s, u"b"_s }));
}

void TestBinding::statusLine()
{
    const Binding binding = liked();
    QCOMPARE(bindingStatusLine(&binding), u"Playing as Spotify playlist: Liked Songs"_s);
    QCOMPARE(bindingStatusLine(nullptr), u"Plain list (not linked to Spotify)"_s);
}

void TestBinding::jsonRoundTrip()
{
    Binding binding = liked();
    binding.appendedIds << u"n"_s;
    const QHash<QString, Binding> read = bindingsFromJson(bindingsToJson({ { u"Spotify: Liked Songs"_s, binding } }));
    QCOMPARE(read.size(), 1);
    const Binding back = read.value(u"Spotify: Liked Songs"_s);
    QCOMPARE(back.contextUri, binding.contextUri);
    QCOMPARE(back.name, binding.name);
    QCOMPARE(back.trackIds, binding.trackIds);
    QCOMPARE(back.appendedIds, binding.appendedIds);
}

void TestBinding::badJsonGivesNothing()
{
    QVERIFY(bindingsFromJson("garbage").isEmpty());
    QVERIFY(bindingsFromJson(R"({"Spotify: X":{"name":"X","tracks":["a"]}})").isEmpty());
}

QTEST_APPLESS_MAIN(TestBinding)
#include "tst_binding.moc"
```

Add `spotify_test(tst_binding)` to `tests/CMakeLists.txt`.

In `tests/tst_followerlogic.cpp` add slots:

```cpp
    void selectsMarkedItem();
    void loadsOtherContext();
    void sameContextSelectsOrAppends();
    void unsupportedContextSelectsOrAppends();
    void filtersComeBeforeContexts();
```

and bodies (the anonymous-namespace helper `playing()` gets a context overload):

```cpp
PlayerState playingFrom(const QString &contextUri, const QString &trackId)
{
    PlayerState state = playing(u"dev"_s, trackId);
    state.contextUri = contextUri;
    return state;
}
```

(put `playingFrom` in the anonymous namespace after `playing`), then:

```cpp
// availability changed after the load: the device plays a track qmmp marked unavailable
void TestFollowerLogic::selectsMarkedItem()
{
    const QStringList playlist = { u"spotify://track/a"_s, u"spotify://track/m?unavailable"_s };
    const FollowDecision d = decideFollow(playing(u"dev"_s, u"m"_s), u"dev"_s, {}, QString(), false, playlist, 0);
    QCOMPARE(d.action, FollowDecision::Select);
    QCOMPARE(d.index, 1);
}

void TestFollowerLogic::loadsOtherContext()
{
    QCOMPARE(decideFollow(playingFrom(u"spotify:album:z"_s, u"a"_s), u"dev"_s, {}, QString(), false, kPlaylist, 0,
                          QString()).action, FollowDecision::LoadContext);
    QCOMPARE(decideFollow(playingFrom(u"spotify:album:z"_s, u"a"_s), u"dev"_s, {}, QString(), false, kPlaylist, 0,
                          u"spotify:playlist:p"_s).action, FollowDecision::LoadContext);
}

// the playlist's own context: a track qmmp's copy lacks (the context changed since) is appended
void TestFollowerLogic::sameContextSelectsOrAppends()
{
    const FollowDecision found = decideFollow(playingFrom(u"spotify:playlist:p"_s, u"x"_s), u"dev"_s, {}, QString(), false,
                                              kPlaylist, 0, u"spotify:playlist:p"_s);
    QCOMPARE(found.action, FollowDecision::Select);
    QCOMPARE(found.index, 1);
    QCOMPARE(decideFollow(playingFrom(u"spotify:playlist:p"_s, u"new"_s), u"dev"_s, {}, QString(), false, kPlaylist, 0,
                          u"spotify:playlist:p"_s).action, FollowDecision::Append);
}

void TestFollowerLogic::unsupportedContextSelectsOrAppends()
{
    QCOMPARE(decideFollow(playingFrom(u"spotify:artist:q"_s, u"x"_s), u"dev"_s, {}, QString(), false, kPlaylist, 0).action,
             FollowDecision::Select);
    QCOMPARE(decideFollow(playingFrom(u"spotify:show:q"_s, u"new"_s), u"dev"_s, {}, QString(), false, kPlaylist, 0).action,
             FollowDecision::Append);
}

void TestFollowerLogic::filtersComeBeforeContexts()
{
    const PlayerState album = playingFrom(u"spotify:album:z"_s, u"a"_s);
    QCOMPARE(decideFollow(album, u"dev"_s, {}, QString(), true, kPlaylist, 0).action, FollowDecision::None);
    QCOMPARE(decideFollow(album, u"dev"_s, { u"a"_s }, QString(), false, kPlaylist, 0).action, FollowDecision::None);
    QCOMPARE(decideFollow(album, u"dev"_s, {}, u"a"_s, false, kPlaylist, 0).action, FollowDecision::None);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake -S . -B build && cmake --build build -j 2>&1 | grep error | head -3`
Expected: `binding.h: No such file or directory`; `no member named 'LoadContext'`.

- [ ] **Step 3: Implement**

`src/logic/binding.h`:

```cpp
#ifndef BINDING_H
#define BINDING_H

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

// A qmmp playlist loaded from a Spotify context and not edited since plays as that context.
struct Binding
{
    QString contextUri;
    // the context's name; the playlist is named boundPlaylistName(name)
    QString name;
    // the context's tracks at load time
    QStringList trackIds;
    // tracks qmmp appended while following (they play as a run)
    QStringList appendedIds;

    bool isValid() const { return !contextUri.isEmpty(); }
    // the playlist holds exactly the loaded tracks, then the appended ones
    bool isIntact(const QStringList &playlistTrackIds) const;
};

// a context qmmp can load into a playlist
struct SpotifyContext
{
    enum Kind { None, Playlist, Album, Collection };

    Kind kind = None;
    // the playlist or album ID
    QString id;

    bool isValid() const { return kind != None; }
    // "spotify:playlist:<id>" (also "spotify:user:<user>:playlist:<id>"), "spotify:album:<id>",
    // "spotify:user:<user>:collection"; anything else (artists, shows, radio) is None
    static SpotifyContext parse(const QString &uri);
};

QString likedSongsContextUri(const QString &userId);
QString boundPlaylistName(const QString &name);
QString editedPlaylistName(const QString &name);
// each item's track ID, the unavailable marker ignored; empty for anything but a Spotify track
QStringList playlistTrackIds(const QStringList &paths);
// the Tools → Spotify status line; binding: the playlist's intact binding, or null
QString bindingStatusLine(const Binding *binding);
// bindings by qmmp playlist name, as stored in <qmmp config dir>/spotify/bindings.json
QByteArray bindingsToJson(const QHash<QString, Binding> &bindings);
QHash<QString, Binding> bindingsFromJson(const QByteArray &json);

#endif
```

`src/logic/binding.cpp`:

```cpp
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVariant>
#include "binding.h"
#include "spotifylink.h"

using namespace Qt::StringLiterals;

bool Binding::isIntact(const QStringList &playlistTrackIds) const
{
    return isValid() && playlistTrackIds == trackIds + appendedIds;
}

SpotifyContext SpotifyContext::parse(const QString &uri)
{
    static const QRegularExpression collection(u"^spotify:user:[^:]+:collection$"_s);
    static const QRegularExpression item(u"^spotify:(?:user:[^:]+:)?(playlist|album):([A-Za-z0-9]+)$"_s);
    if(collection.match(uri).hasMatch())
        return SpotifyContext { Collection, QString() };
    const QRegularExpressionMatch match = item.match(uri);
    if(!match.hasMatch())
        return SpotifyContext();
    return SpotifyContext { match.captured(1) == "playlist"_L1 ? Playlist : Album, match.captured(2) };
}

QString likedSongsContextUri(const QString &userId)
{
    return u"spotify:user:%1:collection"_s.arg(userId);
}

QString boundPlaylistName(const QString &name)
{
    return u"Spotify: "_s + name;
}

QString editedPlaylistName(const QString &name)
{
    return name + u" (edited)"_s;
}

QStringList playlistTrackIds(const QStringList &paths)
{
    QStringList ids;
    for(const QString &path : paths)
    {
        const SpotifyLink link = SpotifyLink::parse(path);
        ids << (link.kind == SpotifyLink::Track ? link.id : QString());
    }
    return ids;
}

QString bindingStatusLine(const Binding *binding)
{
    if(!binding)
        return u"Plain list (not linked to Spotify)"_s;
    return u"Playing as Spotify playlist: %1"_s.arg(binding->name);
}

QByteArray bindingsToJson(const QHash<QString, Binding> &bindings)
{
    QJsonObject root;
    for(auto it = bindings.cbegin(); it != bindings.cend(); ++it)
    {
        root.insert(it.key(), QJsonObject {
            { u"context"_s, it->contextUri },
            { u"name"_s, it->name },
            { u"tracks"_s, QJsonArray::fromStringList(it->trackIds) },
            { u"appended"_s, QJsonArray::fromStringList(it->appendedIds) },
        });
    }
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QHash<QString, Binding> bindingsFromJson(const QByteArray &json)
{
    QHash<QString, Binding> bindings;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    for(auto it = root.constBegin(); it != root.constEnd(); ++it)
    {
        const QJsonObject object = it.value().toObject();
        Binding binding;
        binding.contextUri = object.value("context"_L1).toString();
        binding.name = object.value("name"_L1).toString();
        binding.trackIds = object.value("tracks"_L1).toVariant().toStringList();
        binding.appendedIds = object.value("appended"_L1).toVariant().toStringList();
        if(binding.isValid())
            bindings.insert(it.key(), binding);
    }
    return bindings;
}
```

Add `binding.cpp` to `src/logic/CMakeLists.txt`.

`src/logic/followerlogic.h`: change `FollowDecision` and `decideFollow`, add `findTrackIndex`:

```cpp
struct FollowDecision
{
    // LoadContext: the device plays from a context (state.contextUri) other than the playlist's
    enum Action { None, Select, Append, LoadContext };

    Action action = None;
    int index = -1;
};

// playlistContextUri: the context the playlist is intactly bound to, else empty
FollowDecision decideFollow(const PlayerState &state, const QString &selectedDevice, const QStringList &engineTrackIds,
                            const QString &suppressedTrackId, bool engineAwaiting, const QStringList &playlistPaths, int currentIndex,
                            const QString &playlistContextUri = QString());
// the item holding the track, the unavailable marker ignored: the next one after currentIndex,
// else the first; -1 if none
int findTrackIndex(const QStringList &playlistPaths, const QString &trackId, int currentIndex);
```

`src/logic/followerlogic.cpp`: add `#include "binding.h"` and replace `decideFollow`:

```cpp
FollowDecision decideFollow(const PlayerState &state, const QString &selectedDevice, const QStringList &engineTrackIds,
                            const QString &suppressedTrackId, bool engineAwaiting, const QStringList &playlistPaths, int currentIndex,
                            const QString &playlistContextUri)
{
    if(engineAwaiting || selectedDevice.isEmpty() || !state.hasPlayback || !state.isPlaying
            || state.deviceId != selectedDevice || !state.track.isValid() || engineTrackIds.contains(state.track.id)
            || state.track.id == suppressedTrackId)
        return FollowDecision();

    if(SpotifyContext::parse(state.contextUri).isValid() && state.contextUri != playlistContextUri)
        return FollowDecision { FollowDecision::LoadContext, -1 };
    const int index = findTrackIndex(playlistPaths, state.track.id, currentIndex);
    if(index >= 0)
        return FollowDecision { FollowDecision::Select, index };
    return FollowDecision { FollowDecision::Append, -1 };
}

int findTrackIndex(const QStringList &playlistPaths, const QString &trackId, int currentIndex)
{
    const QStringList ids = playlistTrackIds(playlistPaths);
    const int next = ids.indexOf(trackId, qMax(0, currentIndex + 1));
    return next >= 0 ? next : ids.indexOf(trackId);
}
```

(`spotifylink.h` stays included for `needsExpansion`/`expansionFailureMessage`.)

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: all pass, including the old follower tests.

- [ ] **Step 5: Commit**

```bash
git add src/logic/binding.h src/logic/binding.cpp src/logic/followerlogic.h src/logic/followerlogic.cpp src/logic/CMakeLists.txt tests/tst_binding.cpp tests/tst_followerlogic.cpp tests/CMakeLists.txt
git commit -F - <<'EOF'
Bindings, context URIs and context-aware follow decisions

Claude-Session: https://claude.ai/code/session_01XwRkSvwXRWC8M6LJ7WrpgE
EOF
```

---

### Task 6: Binding store, context loading and indication (general plugin)

This task binds playlists on Load Playlist, Load Liked Songs and a pasted album or playlist link. It persists the bindings, and when the user edits a bound playlist it detaches it and renames it `<name> (edited)`. It registers the hand-off planner with `SpotifyClient` and shows the first line of the Tools → Spotify menu. Nothing here is unit-testable beyond Task 5. Verification is the build, the full test suite and the manual checks.

**Files:**
- Create: `src/general/bindingstore.h`, `src/general/bindingstore.cpp`, `src/general/contextloader.h`, `src/general/contextloader.cpp`
- Modify: `src/general/trackbuilder.h`, `src/general/trackbuilder.cpp`
- Modify: `src/general/spotifygeneral.h`, `src/general/spotifygeneral.cpp`
- Modify: `src/general/linkexpander.h`, `src/general/linkexpander.cpp`
- Modify: `src/general/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 5 (`Binding`, `SpotifyContext`, names, `playlistTrackIds`, `bindingStatusLine`, JSON), Task 2 (`planHandOff`, `findStartIndex`), Task 3 (`SpotifyClient::setHandOffPlanner`), Task 1 (`SpotifyApi::playlistName`), and the existing `removesFailedLink`, `loadFailureMessage` and `createPlayListTracks`.
- Produces (used by Task 7):
  - `QList<PlayListTrack *> playListTracks(PlayListModel *model)`, `QStringList trackPaths(const QList<PlayListTrack *> &tracks)` (trackbuilder)
  - `class BindingStore : QObject` with `const Binding *intactBinding(PlayListModel *model) const`, `PlayListModel *boundPlayList(const QString &contextUri) const`, `PlayListModel *fill(const QString &contextUri, const QString &name, const QList<SpotifyTrack> &tracks)`, `void bind(PlayListModel *model, const QString &contextUri, const QString &name)`, `PlayListTrack *append(PlayListModel *model, const SpotifyTrack &track)`, `QString statusLine() const`
  - `class ContextLoader : QObject` with `using Done = std::function<void(PlayListModel *model, const ApiResult &result)>;`, `void load(const QString &contextUri, const QString &name, Done done)`, `bool isLoading(const QString &contextUri) const`, `bool canLoad(const QString &contextUri) const`
  - `SpotifyGeneral` members `BindingStore *m_bindings`, `ContextLoader *m_loader` (Task 7 passes them to `Follower`, Task 8 adds `ModeFollower`)

- [ ] **Step 1: Track helpers**

`src/general/trackbuilder.h`, add after the existing declarations (and `class PlayListModel;` next to `class PlayListTrack;`):

```cpp
// the playlist's tracks in qmmp's track order (not its display lines)
QList<PlayListTrack *> playListTracks(PlayListModel *model);
QStringList trackPaths(const QList<PlayListTrack *> &tracks);
```

`src/general/trackbuilder.cpp`, add `#include <qmmpui/playlistmodel.h>` and:

```cpp
QList<PlayListTrack *> playListTracks(PlayListModel *model)
{
    QList<PlayListTrack *> tracks;
    for(int i = 0; i < model->trackCount(); ++i)
        tracks << model->findTrack(i);
    return tracks;
}

QStringList trackPaths(const QList<PlayListTrack *> &tracks)
{
    QStringList paths;
    for(PlayListTrack *track : tracks)
        paths << (track ? track->path() : QString());
    return paths;
}
```

- [ ] **Step 2: BindingStore**

`src/general/bindingstore.h`:

```cpp
#ifndef BINDINGSTORE_H
#define BINDINGSTORE_H

#include <QHash>
#include <QObject>
#include "binding.h"
#include "handoff.h"
#include "spotifytypes.h"

class PlayListModel;
class PlayListTrack;

// Which qmmp playlists are bound to a Spotify context, kept across restarts in
// <qmmp config dir>/spotify/bindings.json by playlist name. A bound playlist the user edits is
// detached and renamed "<name> (edited)", without a notice. Plans the engine's hand-offs.
class BindingStore : public QObject
{
    Q_OBJECT
public:
    explicit BindingStore(QObject *parent = nullptr);
    ~BindingStore();

    // the playlist's binding while it holds exactly the bound tracks, else null; valid until
    // the next call that binds, appends or detaches
    const Binding *intactBinding(PlayListModel *model) const;
    // an open playlist intactly bound to contextUri, or null
    PlayListModel *boundPlayList(const QString &contextUri) const;
    // fills "Spotify: <name>" (that playlist, else "<name> (edited)" renamed back, else a new
    // one) with the context's tracks and binds it
    PlayListModel *fill(const QString &contextUri, const QString &name, const QList<SpotifyTrack> &tracks);
    // binds a playlist that holds exactly a pasted context's tracks, renamed "Spotify: <name>"
    void bind(PlayListModel *model, const QString &contextUri, const QString &name);
    // appends a track the device plays; an intactly bound playlist stays bound
    PlayListTrack *append(PlayListModel *model, const SpotifyTrack &track);
    // Tools → Spotify's first line: the playing playlist, or the selected one when stopped
    QString statusLine() const;

private:
    void watch(PlayListModel *model);
    void checkEdited(PlayListModel *model);
    void save() const;
    HandOff plan(const QString &path) const;

    QHash<QString, Binding> m_bindings;
    // > 0 while this plugin changes a playlist: not the user's edit
    int m_ownChanges = 0;
};

#endif
```

`src/general/bindingstore.cpp`:

```cpp
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <qmmp/soundcore.h>
#include <qmmpui/playlistmanager.h>
#include <qmmpui/playlistmodel.h>
#include "bindingstore.h"
#include "spotifyauth.h"
#include "spotifyclient.h"
#include "trackbuilder.h"

using namespace Qt::StringLiterals;

namespace {

QString bindingsFile()
{
    return SpotifyClient::instance()->auth()->dataDir() + u"/bindings.json"_s;
}

QStringList trackIdsOf(PlayListModel *model)
{
    return playlistTrackIds(trackPaths(playListTracks(model)));
}

PlayListModel *findPlayList(const QString &name)
{
    for(PlayListModel *model : PlayListManager::instance()->playLists())
    {
        if(model->name() == name)
            return model;
    }
    return nullptr;
}

}

BindingStore::BindingStore(QObject *parent) : QObject(parent)
{
    QFile file(bindingsFile());
    if(file.open(QIODevice::ReadOnly))
        m_bindings = bindingsFromJson(file.readAll());
    PlayListManager *manager = PlayListManager::instance();
    for(PlayListModel *model : manager->playLists())
    {
        watch(model);
        // edited while the plugin was off
        checkEdited(model);
    }
    connect(manager, &PlayListManager::playListAdded, this, [this, manager](int index) { watch(manager->playListAt(index)); });
    SpotifyClient::instance()->setHandOffPlanner([this](const QString &path) { return plan(path); });
}

// qmmp deletes the plugin before it creates it again (settings accepted)
BindingStore::~BindingStore()
{
    SpotifyClient::instance()->setHandOffPlanner(nullptr);
}

const Binding *BindingStore::intactBinding(PlayListModel *model) const
{
    const auto it = m_bindings.constFind(model->name());
    if(it == m_bindings.constEnd() || !it->isIntact(trackIdsOf(model)))
        return nullptr;
    return &it.value();
}

PlayListModel *BindingStore::boundPlayList(const QString &contextUri) const
{
    for(PlayListModel *model : PlayListManager::instance()->playLists())
    {
        const Binding *binding = intactBinding(model);
        if(binding && binding->contextUri == contextUri)
            return model;
    }
    return nullptr;
}

PlayListModel *BindingStore::fill(const QString &contextUri, const QString &name, const QList<SpotifyTrack> &tracks)
{
    PlayListModel *model = findPlayList(boundPlaylistName(name));
    if(!model)
        model = findPlayList(editedPlaylistName(name));
    if(!model)
        model = PlayListManager::instance()->createPlayList(boundPlaylistName(name));
    ++m_ownChanges;
    model->setName(boundPlaylistName(name));
    model->clear();
    model->addTracks(createPlayListTracks(tracks));
    --m_ownChanges;
    Binding binding;
    binding.contextUri = contextUri;
    binding.name = name;
    binding.trackIds = trackIdsOf(model);
    m_bindings.insert(model->name(), binding);
    save();
    return model;
}

void BindingStore::bind(PlayListModel *model, const QString &contextUri, const QString &name)
{
    Binding binding;
    binding.contextUri = contextUri;
    binding.name = name;
    binding.trackIds = trackIdsOf(model);
    model->setName(boundPlaylistName(name));
    m_bindings.insert(model->name(), binding);
    save();
}

PlayListTrack *BindingStore::append(PlayListModel *model, const SpotifyTrack &track)
{
    const bool bound = intactBinding(model);
    PlayListTrack *added = createPlayListTrack(track);
    ++m_ownChanges;
    model->addTrack(added);
    --m_ownChanges;
    if(bound)
    {
        m_bindings[model->name()].appendedIds << track.id;
        save();
    }
    return added;
}

QString BindingStore::statusLine() const
{
    PlayListManager *manager = PlayListManager::instance();
    const bool playing = SoundCore::instance()->state() != Qmmp::Stopped;
    PlayListModel *model = playing ? manager->currentPlayList() : manager->selectedPlayList();
    return bindingStatusLine(model ? intactBinding(model) : nullptr);
}

void BindingStore::watch(PlayListModel *model)
{
    connect(model, &PlayListModel::listChanged, this, [this, model](int flags) {
        if(flags & PlayListModel::STRUCTURE)
            checkEdited(model);
    });
}

// qmmp also signals STRUCTURE when it only regroups: the tracks decide, not the flag
void BindingStore::checkEdited(PlayListModel *model)
{
    if(m_ownChanges > 0)
        return;
    const auto it = m_bindings.constFind(model->name());
    if(it == m_bindings.constEnd() || it->isIntact(trackIdsOf(model)))
        return;
    const QString name = it->name;
    m_bindings.erase(it);
    save();
    model->setName(editedPlaylistName(name));
}

void BindingStore::save() const
{
    QDir().mkpath(SpotifyClient::instance()->auth()->dataDir());
    QSaveFile file(bindingsFile());
    if(!file.open(QIODevice::WriteOnly))
    {
        qWarning("Spotify: cannot write %s", qPrintable(file.fileName()));
        return;
    }
    file.write(bindingsToJson(m_bindings));
    file.commit();
}

// called by the engine while qmmp starts the item: it is the playing playlist's current one
HandOff BindingStore::plan(const QString &path) const
{
    PlayListManager *manager = PlayListManager::instance();
    PlayListModel *model = manager->currentPlayList();
    if(!model)
        return HandOff();
    const QList<PlayListTrack *> tracks = playListTracks(model);
    const QStringList paths = trackPaths(tracks);
    const int index = findStartIndex(paths, int(tracks.indexOf(model->currentTrack())), path);
    const Binding *binding = intactBinding(model);
    return planHandOff(paths, index, binding ? binding->contextUri : QString(), binding ? int(binding->trackIds.size()) : 0);
}
```

- [ ] **Step 3: ContextLoader**

`src/general/contextloader.h`:

```cpp
#ifndef CONTEXTLOADER_H
#define CONTEXTLOADER_H

#include <functional>
#include <QObject>
#include <QSet>
#include "spotifytypes.h"

class BindingStore;
class PlayListModel;
struct ApiResult;

// Loads a Spotify context (a playlist, an album, Liked Songs) into its bound qmmp playlist.
class ContextLoader : public QObject
{
    Q_OBJECT
public:
    // the filled playlist, or null when the load failed
    using Done = std::function<void(PlayListModel *model, const ApiResult &result)>;

    explicit ContextLoader(BindingStore *bindings, QObject *parent = nullptr);

    // name: the context's name when known (a playlist picked in the dialog), else it is loaded
    void load(const QString &contextUri, const QString &name, Done done);
    bool isLoading(const QString &contextUri) const { return m_loading.contains(contextUri); }
    // a playlist, album or Liked Songs that has not been refused before (another user's
    // playlist: Development Mode apps get 403/404), so a follower does not retry it per track
    bool canLoad(const QString &contextUri) const;

private:
    void finish(const QString &contextUri, const QString &name, const ApiResult &result, const QList<SpotifyTrack> &tracks,
                const Done &done);

    BindingStore *m_bindings;
    QSet<QString> m_loading;
    QSet<QString> m_refused;
};

#endif
```

`src/general/contextloader.cpp`:

```cpp
#include <QPointer>
#include <QTimer>
#include "binding.h"
#include "bindingstore.h"
#include "contextloader.h"
#include "followerlogic.h"
#include "spotifyapi.h"
#include "spotifyclient.h"

using namespace Qt::StringLiterals;

ContextLoader::ContextLoader(BindingStore *bindings, QObject *parent) : QObject(parent), m_bindings(bindings)
{}

bool ContextLoader::canLoad(const QString &contextUri) const
{
    return SpotifyContext::parse(contextUri).isValid() && !m_refused.contains(contextUri);
}

void ContextLoader::load(const QString &contextUri, const QString &name, Done done)
{
    const SpotifyContext context = SpotifyContext::parse(contextUri);
    SpotifyApi *api = SpotifyClient::instance()->api();
    QPointer<ContextLoader> self(this);
    // the plugin may be re-created (its settings accepted) while the pages load
    auto finish = [self, contextUri, done](const QString &loadedName, const ApiResult &result, const QList<SpotifyTrack> &tracks) {
        if(self)
            self->finish(contextUri, loadedName, result, tracks, done);
    };
    m_loading.insert(contextUri);
    switch(context.kind)
    {
    case SpotifyContext::Collection:
        api->likedTracks([finish, name](const ApiResult &result, const QList<SpotifyTrack> &tracks) {
            finish(name.isEmpty() ? u"Liked Songs"_s : name, result, tracks);
        });
        break;
    case SpotifyContext::Album:
        api->albumTracks(context.id, [finish, name](const ApiResult &result, const QList<SpotifyTrack> &tracks) {
            finish(tracks.isEmpty() ? name : tracks.first().album, result, tracks);
        });
        break;
    case SpotifyContext::Playlist:
        if(!name.isEmpty())
        {
            api->playlistTracks(context.id, [finish, name](const ApiResult &result, const QList<SpotifyTrack> &tracks) {
                finish(name, result, tracks);
            });
            break;
        }
        api->playlistName(context.id, [api, finish, id = context.id](const ApiResult &result, const QString &loadedName) {
            if(!result.ok)
            {
                finish(QString(), result, {});
                return;
            }
            api->playlistTracks(id, [finish, loadedName](const ApiResult &result, const QList<SpotifyTrack> &tracks) {
                finish(loadedName, result, tracks);
            });
        });
        break;
    case SpotifyContext::None:
        // like a failed request: on the next event-loop turn
        QTimer::singleShot(0, this, [finish] { finish(QString(), ApiResult(), {}); });
        break;
    }
}

void ContextLoader::finish(const QString &contextUri, const QString &name, const ApiResult &result,
                           const QList<SpotifyTrack> &tracks, const Done &done)
{
    m_loading.remove(contextUri);
    if(!result.ok)
    {
        if(removesFailedLink(result.status))
            m_refused.insert(contextUri);
        done(nullptr, result);
        return;
    }
    done(m_bindings->fill(contextUri, name, tracks), result);
}
```

- [ ] **Step 4: SpotifyGeneral: bound loads and the status line**

`src/general/spotifygeneral.h`: add forward declarations `class QAction;`, `class BindingStore;`, `class ContextLoader;`. Remove the `fillPlayList` declaration. Add these members after `QMenu *m_menu;`:

```cpp
    // the first, inactive menu line: the state of the playing (or selected) playlist
    QAction *m_status;
    BindingStore *m_bindings;
    ContextLoader *m_loader;
```

`src/general/spotifygeneral.cpp`:
- includes: add `#include <QAction>`, `#include "binding.h"`, `#include "bindingstore.h"`, `#include "contextloader.h"`, `#include "spotifylink.h"`; remove `#include "trackbuilder.h"` if nothing else uses it.
- add after the includes:

```cpp
namespace {

// a rate-limit failure names the load, so the block's notice suppression does not hide it
void showLoaded(PlayListModel *model, const QString &name, const ApiResult &result)
{
    if(model)
        PlayListManager::instance()->selectPlayList(model);
    else
        SpotifyClient::instance()->notify(loadFailureMessage(name, result.message));
}

}
```

- constructor: create the store and loader before the menu, and put the status line first:

```cpp
    m_bindings = new BindingStore(this);
    m_loader = new ContextLoader(m_bindings, this);

    m_menu = new QMenu(tr("Spotify"));
    m_status = m_menu->addAction(QString());
    m_status->setEnabled(false);
    connect(m_menu, &QMenu::aboutToShow, this, [this] { m_status->setText(m_bindings->statusLine()); });
    m_menu->addAction(tr("Device…"), this, &SpotifyGeneral::chooseDevice);
    m_menu->addAction(tr("Load Playlist…"), this, &SpotifyGeneral::loadPlaylist);
    m_menu->addAction(tr("Load Liked Songs"), this, &SpotifyGeneral::loadLikedSongs);
    UiHelper::instance()->addAction(m_menu->menuAction(), UiHelper::TOOLS_MENU);

    new Follower(this);
    new LinkExpander(m_bindings, this);
```

- replace `loadPlaylist`, `loadLikedSongs`, and delete `fillPlayList`:

```cpp
void SpotifyGeneral::loadPlaylist()
{
    if(!requireLogin())
        return;
    PlayListDialog dialog(QApplication::activeWindow());
    if(dialog.exec() != QDialog::Accepted || !dialog.selected().isValid())
        return;
    const SpotifyPlaylist playlist = dialog.selected();
    const QString uri = SpotifyLink { SpotifyLink::Playlist, playlist.id }.uri();
    m_loader->load(uri, playlist.name, [name = playlist.name](PlayListModel *model, const ApiResult &result) {
        showLoaded(model, name, result);
    });
}

// Liked Songs is the context spotify:user:<user id>:collection
void SpotifyGeneral::loadLikedSongs()
{
    if(!requireLogin())
        return;
    const QString name = tr("Liked Songs");
    QPointer<SpotifyGeneral> self(this);
    SpotifyClient::instance()->api()->userId([self, name](const ApiResult &result, const QString &userId) {
        if(!self)
            return;
        if(!result.ok)
        {
            SpotifyClient::instance()->notify(loadFailureMessage(name, result.message));
            return;
        }
        self->m_loader->load(likedSongsContextUri(userId), name, [name](PlayListModel *model, const ApiResult &result) {
            showLoaded(model, name, result);
        });
    });
}
```

- [ ] **Step 5: LinkExpander binds a pasted album or playlist link**

`src/general/linkexpander.h`: add `#include "bindingstore.h"` and `struct SpotifyLink;`. Change the constructor to `explicit LinkExpander(BindingStore *bindings, QObject *parent = nullptr);`. Extend `replace` with a trailing `QPointer<BindingStore> bindings` parameter. Add:

```cpp
    // a pasted album or playlist link whose tracks became the whole playlist binds it
    static void bindPasted(QPointer<PlayListModel> model, const SpotifyLink &link, const QList<SpotifyTrack> &tracks,
                           QPointer<BindingStore> bindings);

    QPointer<BindingStore> m_bindings;
```

`src/general/linkexpander.cpp`:
- add `#include "binding.h"`
- constructor: `LinkExpander::LinkExpander(BindingStore *bindings, QObject *parent) : QObject(parent), m_bindings(bindings)`
- in `onTracksAdded`, capture the store: `QPointer<BindingStore> bindings = m_bindings;` next to `QPointer<PlayListModel> guarded(model);`, and in the `done` lambda capture `bindings` and call `replace(guarded, track, rawPath, result, expanded, bindings);`
- `replace` gets the parameter `QPointer<BindingStore> bindings`; after `model->removeTrack(live);` add:

```cpp
    bindPasted(model, SpotifyLink::parse(rawPath), tracks, bindings);
```

- add:

```cpp
void LinkExpander::bindPasted(QPointer<PlayListModel> model, const SpotifyLink &link, const QList<SpotifyTrack> &tracks,
                              QPointer<BindingStore> bindings)
{
    if(link.kind == SpotifyLink::Track || tracks.isEmpty() || !bindings)
        return;
    QStringList ids;
    for(const SpotifyTrack &track : tracks)
        ids << track.id;
    const auto holdsOnly = [model, ids] { return model && playlistTrackIds(trackPaths(playListTracks(model))) == ids; };
    if(!holdsOnly())
        return;
    const QString uri = link.uri();
    if(link.kind == SpotifyLink::Album)
    {
        bindings->bind(model, uri, tracks.first().album);
        return;
    }
    SpotifyClient::instance()->api()->playlistName(link.id, [model, bindings, uri, holdsOnly](const ApiResult &result, const QString &name) {
        if(result.ok && bindings && holdsOnly())
            bindings->bind(model, uri, name);
    });
}
```

`src/general/CMakeLists.txt`: add `bindingstore.cpp` and `contextloader.cpp` to `add_library(spotifygeneral MODULE ...)`.

- [ ] **Step 6: Build, test, install**

Run: `cmake -S . -B build && cmake --build build -j --clean-first 2>&1 | grep -E "warning|error"; ctest --test-dir build --output-on-failure && cmake --install build`
Expected: no warnings or errors; all suites pass; the plugins are installed.

- [ ] **Step 7: Commit**

```bash
git add src/general/bindingstore.h src/general/bindingstore.cpp src/general/contextloader.h src/general/contextloader.cpp src/general/trackbuilder.h src/general/trackbuilder.cpp src/general/spotifygeneral.h src/general/spotifygeneral.cpp src/general/linkexpander.h src/general/linkexpander.cpp src/general/CMakeLists.txt
git commit -F - <<'EOF'
Bind loaded playlists to their Spotify context; detach and rename on edit

Claude-Session: https://claude.ai/code/session_01XwRkSvwXRWC8M6LJ7WrpgE
EOF
```

Manual checks pending (Task 9 batch):
- Load Liked Songs → playlist `Spotify: Liked Songs`; menu line `Playing as Spotify playlist: Liked Songs`; double-click track 10 → api.log shows `PUT /me/player/play` once and Spotify shows "Liked Songs".
- Delete one track from it → renamed `Liked Songs (edited)`, menu line `Plain list (not linked to Spotify)`. Load Liked Songs again → `Spotify: Liked Songs`, bound. Restart qmmp → still bound.
- Toggle qmmp's "group tracks" on a bound playlist → it stays bound.
- In a new empty playlist, paste an album link → renamed `Spotify: <album>`, bound.

---

### Task 7: Follower: cursor, other contexts, appends

**Files:**
- Modify: `src/general/follower.h`, `src/general/follower.cpp` (full replacement below)
- Modify: `src/general/spotifygeneral.cpp` (Follower construction)

**Interfaces:**
- Consumes: `decideFollow` with `playlistContextUri`, `findTrackIndex`, `FollowDecision::LoadContext` (Task 5); `BindingStore::intactBinding/boundPlayList/append`, `ContextLoader::load/isLoading/canLoad`, `playListTracks`, `trackPaths` (Task 6).
- Produces: `Follower(BindingStore *bindings, ContextLoader *loader, QObject *parent = nullptr)`.

**No request loops:** following never sends a playback request. `playTrack` is a qmmp stop + setCurrent + play, and the engine adopts the track the device already plays (Task 3 `followsHandOffWithoutRequests`). A context load happens at most once per follow trigger. Triggers are edge-based (`isFollowTrigger`: a track change or playback starting), a context already loading is not loaded again, and a context Spotify refused (400/403/404) is never retried in this session (`ContextLoader::canLoad`). The requests are bulk ones, paced by the bulk budget.

- [ ] **Step 1: Replace the follower**

`src/general/follower.h`:

```cpp
#ifndef FOLLOWER_H
#define FOLLOWER_H

#include <QObject>
#include "spotifytypes.h"

class BindingStore;
class ContextLoader;
class PlayListModel;

// Makes qmmp follow what the selected device plays: inside a hand-off it moves qmmp's cursor;
// a track from another context is followed in that context's own playlist; anything else is
// appended to the playing playlist.
class Follower : public QObject
{
    Q_OBJECT
public:
    Follower(BindingStore *bindings, ContextLoader *loader, QObject *parent = nullptr);

private:
    void onState(const PlayerState &state);
    void followContext(const PlayerState &state, PlayListModel *current);

    BindingStore *m_bindings;
    ContextLoader *m_loader;
    PlayerState m_previous;
    bool m_hasBaseline = false;
};

#endif
```

`src/general/follower.cpp`:

```cpp
#include <QPointer>
#include <qmmpui/mediaplayer.h>
#include <qmmpui/playlistmanager.h>
#include <qmmpui/playlistmodel.h>
#include "bindingstore.h"
#include "contextloader.h"
#include "follower.h"
#include "followerlogic.h"
#include "spotifyclient.h"
#include "spotifypoller.h"
#include "trackbuilder.h"

namespace {

// qmmp moves to the track the device already plays: the engine adopts it without a request
void playTrack(PlayListModel *model, PlayListTrack *track)
{
    PlayListManager *manager = PlayListManager::instance();
    MediaPlayer *player = MediaPlayer::instance();
    player->stop();
    if(manager->currentPlayList() != model)
    {
        manager->activatePlayList(model);
        manager->selectPlayList(model);
    }
    model->setCurrent(track);
    player->play();
}

}

Follower::Follower(BindingStore *bindings, ContextLoader *loader, QObject *parent)
    : QObject(parent), m_bindings(bindings), m_loader(loader)
{
    // queued: the engine handles the same state first and publishes its tracks
    connect(SpotifyClient::instance()->poller(), &SpotifyPoller::stateChanged, this, &Follower::onState, Qt::QueuedConnection);
}

void Follower::onState(const PlayerState &state)
{
    SpotifyClient *client = SpotifyClient::instance();

    if(!m_hasBaseline)
    {
        m_hasBaseline = true;
        // at qmmp startup the first poll is compared with an empty state, so qmmp attaches to
        // what the device is already playing; a follower re-created later (the plugin's
        // settings were accepted) only takes a baseline
        if(!client->takeStartupAttach())
        {
            m_previous = state;
            return;
        }
    }
    const PlayerState previous = m_previous;
    m_previous = state;
    if(!isFollowTrigger(previous, state, client->deviceId()))
        return;

    PlayListModel *model = PlayListManager::instance()->currentPlayList();
    if(!model)
        return;
    const QList<PlayListTrack *> tracks = playListTracks(model);
    const int currentIndex = int(tracks.indexOf(model->currentTrack()));
    const Binding *binding = m_bindings->intactBinding(model);
    // a context qmmp cannot load is followed like the playlist's own: select or append
    const QString context = m_loader->canLoad(state.contextUri) ? (binding ? binding->contextUri : QString()) : state.contextUri;
    const FollowDecision decision = decideFollow(state, client->deviceId(), client->engineTrackIds(), client->suppressedTrack(),
                                                 client->isEngineAwaiting(), trackPaths(tracks), currentIndex, context);
    switch(decision.action)
    {
    case FollowDecision::None:
        break;
    case FollowDecision::Select:
        playTrack(model, tracks.at(decision.index));
        break;
    case FollowDecision::Append:
        playTrack(model, m_bindings->append(model, state.track));
        break;
    case FollowDecision::LoadContext:
        followContext(state, model);
        break;
    }
}

// the device plays from a context the playlist is not bound to: qmmp follows it in that
// context's own playlist, loaded unless an intact one already holds the track
void Follower::followContext(const PlayerState &state, PlayListModel *current)
{
    if(PlayListModel *bound = m_bindings->boundPlayList(state.contextUri))
    {
        const QList<PlayListTrack *> tracks = playListTracks(bound);
        const int index = findTrackIndex(trackPaths(tracks), state.track.id, -1);
        if(index >= 0)
        {
            playTrack(bound, tracks.at(index));
            return;
        }
    }
    if(m_loader->isLoading(state.contextUri))
        return;
    QPointer<PlayListModel> fallback(current);
    QPointer<BindingStore> bindings(m_bindings);
    const SpotifyTrack track = state.track;
    m_loader->load(state.contextUri, QString(), [fallback, bindings, track](PlayListModel *model, const ApiResult &) {
        SpotifyClient *client = SpotifyClient::instance();
        // the device may have moved on, or qmmp started a track, while the context loaded
        const PlayerState now = client->poller()->lastState();
        if(client->isEngineAwaiting() || !now.isPlaying || now.deviceId != client->deviceId())
            return;
        if(!model)
        {
            // not loadable (another user's playlist, offline): the track joins the playing playlist
            if(fallback && bindings && now.track.id == track.id)
                playTrack(fallback, bindings->append(fallback, track));
            return;
        }
        const QList<PlayListTrack *> tracks = playListTracks(model);
        const int index = findTrackIndex(trackPaths(tracks), now.track.id, -1);
        if(index >= 0)
            playTrack(model, tracks.at(index));
    });
}
```

`src/general/spotifygeneral.cpp`: replace `new Follower(this);` with `new Follower(m_bindings, m_loader, this);`.

- [ ] **Step 2: Build, test, install**

Run: `cmake --build build -j --clean-first 2>&1 | grep -E "warning|error"; ctest --test-dir build --output-on-failure && cmake --install build`
Expected: no warnings or errors; all suites pass.

- [ ] **Step 3: Commit**

```bash
git add src/general/follower.h src/general/follower.cpp src/general/spotifygeneral.cpp
git commit -F - <<'EOF'
Follow the device: move the cursor, load other contexts, append the rest

Claude-Session: https://claude.ai/code/session_01XwRkSvwXRWC8M6LJ7WrpgE
EOF
```

Manual checks pending (Task 9 batch): natural advance moves the cursor, both focused and unfocused. Starting an album on the phone loads `Spotify: <album>` and follows it. Autoplay after a context end is followed.

---

### Task 8: Shuffle and repeat mirrored both ways

**Files:**
- Create: `src/logic/modesync.h`, `src/logic/modesync.cpp`, `src/general/modefollower.h`, `src/general/modefollower.cpp`
- Modify: `src/logic/CMakeLists.txt`, `src/general/CMakeLists.txt`, `src/general/spotifygeneral.cpp`, `tests/CMakeLists.txt`
- Test: `tests/tst_modesync.cpp`

**Interfaces:**
- Consumes: `PlayerState::shuffle/repeat` and `SpotifyApi::setShuffle/setRepeat` (Task 1).
- Produces:
  - `enum class RepeatMode { Off, Context, Track };`
  - `RepeatMode parseRepeatState(const QString &state)`, `QString repeatStateName(RepeatMode mode)`
  - `struct QmmpRepeat { bool list = false; bool track = false; }`, `RepeatMode repeatModeOf(const QmmpRepeat &toggles)`, `QmmpRepeat qmmpRepeatFor(RepeatMode mode, const QmmpRepeat &current)`
  - `class ModeMirror { static constexpr qint64 kEchoGraceMs = 5000; explicit ModeMirror(Clock); bool qmmpChanged(int value); bool polled(int value); }`
  - `class ModeFollower : QObject { explicit ModeFollower(QObject *parent = nullptr); }`

**No loops:** a polled value is applied to qmmp with `m_applying` set, so the toggle signals it causes are not sent back. `ModeMirror::qmmpChanged` also returns false for the value Spotify is already known to have. A request goes out only when the user toggles in qmmp. There is one request per toggle and no retry. A failed request is reverted by the next poll once `kEchoGraceMs` has passed.

- [ ] **Step 1: Write the failing test**

`tests/tst_modesync.cpp`:

```cpp
#include <QtTest>
#include "modesync.h"

using namespace Qt::StringLiterals;

class TestModeSync : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void repeatStates();
    void repeatFromQmmp();
    void repeatForQmmp();
    void pollSetsQmmp();
    void ownValueIsNotSentBack();
    void qmmpChangeIsSent();
    void stalePollDoesNotUndoChange();
    void failedChangeRevertsAfterGrace();
    void toggleBackWhilePending();

private:
    qint64 m_now = 0;
};

void TestModeSync::init()
{
    m_now = 1000;
}

void TestModeSync::repeatStates()
{
    QCOMPARE(parseRepeatState(u"track"_s), RepeatMode::Track);
    QCOMPARE(parseRepeatState(u"context"_s), RepeatMode::Context);
    QCOMPARE(parseRepeatState(u"off"_s), RepeatMode::Off);
    QCOMPARE(parseRepeatState(QString()), RepeatMode::Off);
    QCOMPARE(repeatStateName(RepeatMode::Track), u"track"_s);
    QCOMPARE(repeatStateName(RepeatMode::Context), u"context"_s);
    QCOMPARE(repeatStateName(RepeatMode::Off), u"off"_s);
}

void TestModeSync::repeatFromQmmp()
{
    QCOMPARE(repeatModeOf(QmmpRepeat { false, false }), RepeatMode::Off);
    QCOMPARE(repeatModeOf(QmmpRepeat { true, false }), RepeatMode::Context);
    QCOMPARE(repeatModeOf(QmmpRepeat { false, true }), RepeatMode::Track);
    QCOMPARE(repeatModeOf(QmmpRepeat { true, true }), RepeatMode::Track);
}

void TestModeSync::repeatForQmmp()
{
    const QmmpRepeat off = qmmpRepeatFor(RepeatMode::Off, QmmpRepeat { true, true });
    QVERIFY(!off.list && !off.track);
    const QmmpRepeat context = qmmpRepeatFor(RepeatMode::Context, QmmpRepeat { false, true });
    QVERIFY(context.list && !context.track);
    const QmmpRepeat keepsList = qmmpRepeatFor(RepeatMode::Track, QmmpRepeat { true, false });
    QVERIFY(keepsList.list && keepsList.track);
    const QmmpRepeat trackOnly = qmmpRepeatFor(RepeatMode::Track, QmmpRepeat { false, false });
    QVERIFY(!trackOnly.list && trackOnly.track);
}

void TestModeSync::pollSetsQmmp()
{
    ModeMirror mirror([this] { return m_now; });
    QVERIFY(mirror.polled(1));
}

void TestModeSync::ownValueIsNotSentBack()
{
    ModeMirror mirror([this] { return m_now; });
    QVERIFY(mirror.polled(1));
    QVERIFY(!mirror.qmmpChanged(1));
}

void TestModeSync::qmmpChangeIsSent()
{
    ModeMirror mirror([this] { return m_now; });
    QVERIFY(mirror.qmmpChanged(1));
    QVERIFY(mirror.polled(0) == false);
    QVERIFY(mirror.polled(1));
    QVERIFY(!mirror.qmmpChanged(1));
    QVERIFY(mirror.qmmpChanged(0));
}

// a poll sent before qmmp's change reached Spotify shows the old value
void TestModeSync::stalePollDoesNotUndoChange()
{
    ModeMirror mirror([this] { return m_now; });
    QVERIFY(mirror.polled(0));
    QVERIFY(mirror.qmmpChanged(1));
    m_now += 1000;
    QVERIFY(!mirror.polled(0));
    QVERIFY(mirror.polled(1));
    m_now += 60000;
    QVERIFY(mirror.polled(0));
}

void TestModeSync::failedChangeRevertsAfterGrace()
{
    ModeMirror mirror([this] { return m_now; });
    QVERIFY(mirror.polled(0));
    QVERIFY(mirror.qmmpChanged(1));
    m_now += ModeMirror::kEchoGraceMs;
    QVERIFY(mirror.polled(0));
    QVERIFY(!mirror.qmmpChanged(0));
}

void TestModeSync::toggleBackWhilePending()
{
    ModeMirror mirror([this] { return m_now; });
    QVERIFY(mirror.polled(0));
    QVERIFY(mirror.qmmpChanged(1));
    QVERIFY(mirror.qmmpChanged(0));
    m_now += 500;
    QVERIFY(!mirror.polled(1));
    QVERIFY(mirror.polled(0));
}

QTEST_APPLESS_MAIN(TestModeSync)
#include "tst_modesync.moc"
```

Add `spotify_test(tst_modesync)` to `tests/CMakeLists.txt`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake -S . -B build && cmake --build build -j 2>&1 | grep error | head -3`
Expected: `modesync.h: No such file or directory`.

- [ ] **Step 3: Implement the pure part**

`src/logic/modesync.h`:

```cpp
#ifndef MODESYNC_H
#define MODESYNC_H

#include <functional>
#include <QString>

enum class RepeatMode { Off, Context, Track };

// Spotify's repeat_state: "off", "context", "track"; anything else is off
RepeatMode parseRepeatState(const QString &state);
QString repeatStateName(RepeatMode mode);

// qmmp's two repeat toggles
struct QmmpRepeat
{
    // "Repeat playlist"
    bool list = false;
    // "Repeat track"; it wins over list
    bool track = false;
};

RepeatMode repeatModeOf(const QmmpRepeat &toggles);
// the toggles that show mode; "track" keeps qmmp's list toggle as it is
QmmpRepeat qmmpRepeatFor(RepeatMode mode, const QmmpRepeat &current);

// Mirrors one Spotify setting (shuffle, repeat) and qmmp's toggle for it, both ways: a poll's
// value is not sent back, and a poll from before qmmp's change does not undo it.
class ModeMirror
{
public:
    using Clock = std::function<qint64()>;

    // polls this soon after qmmp's change may predate it; later, Spotify's value wins (the
    // change failed or was overridden elsewhere)
    static constexpr qint64 kEchoGraceMs = 5000;

    explicit ModeMirror(Clock clock);

    // qmmp's toggle changed to value: true when it must be sent to Spotify
    bool qmmpChanged(int value);
    // a poll of the selected device shows value: true when qmmp's toggle must be set to it
    bool polled(int value);

private:
    Clock m_clock;
    bool m_known = false;
    int m_spotify = 0;
    bool m_pending = false;
    int m_pendingValue = 0;
    qint64 m_pendingAt = 0;
};

#endif
```

`src/logic/modesync.cpp`:

```cpp
#include "modesync.h"

using namespace Qt::StringLiterals;

RepeatMode parseRepeatState(const QString &state)
{
    if(state == "track"_L1)
        return RepeatMode::Track;
    if(state == "context"_L1)
        return RepeatMode::Context;
    return RepeatMode::Off;
}

QString repeatStateName(RepeatMode mode)
{
    switch(mode)
    {
    case RepeatMode::Track:
        return u"track"_s;
    case RepeatMode::Context:
        return u"context"_s;
    case RepeatMode::Off:
        break;
    }
    return u"off"_s;
}

RepeatMode repeatModeOf(const QmmpRepeat &toggles)
{
    if(toggles.track)
        return RepeatMode::Track;
    return toggles.list ? RepeatMode::Context : RepeatMode::Off;
}

QmmpRepeat qmmpRepeatFor(RepeatMode mode, const QmmpRepeat &current)
{
    switch(mode)
    {
    case RepeatMode::Track:
        return QmmpRepeat { current.list, true };
    case RepeatMode::Context:
        return QmmpRepeat { true, false };
    case RepeatMode::Off:
        break;
    }
    return QmmpRepeat();
}

ModeMirror::ModeMirror(Clock clock) : m_clock(std::move(clock))
{}

bool ModeMirror::qmmpChanged(int value)
{
    const bool alreadyThere = m_pending ? value == m_pendingValue : m_known && value == m_spotify;
    if(alreadyThere)
        return false;
    m_pending = true;
    m_pendingValue = value;
    m_pendingAt = m_clock();
    return true;
}

bool ModeMirror::polled(int value)
{
    if(m_pending)
    {
        if(value != m_pendingValue && m_clock() - m_pendingAt < kEchoGraceMs)
            return false;
        m_pending = false;
    }
    m_known = true;
    m_spotify = value;
    return true;
}
```

Add `modesync.cpp` to `src/logic/CMakeLists.txt`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build -R tst_modesync --output-on-failure`
Expected: PASS.

- [ ] **Step 5: The glue**

`src/general/modefollower.h`:

```cpp
#ifndef MODEFOLLOWER_H
#define MODEFOLLOWER_H

#include <QElapsedTimer>
#include <QObject>
#include "modesync.h"
#include "spotifytypes.h"

// qmmp's Shuffle and Repeat toggles mirror Spotify's, both ways.
class ModeFollower : public QObject
{
    Q_OBJECT
public:
    explicit ModeFollower(QObject *parent = nullptr);

private:
    void onState(const PlayerState &state);
    void sendShuffle(bool on);
    void sendRepeat();

    QElapsedTimer m_clock;
    ModeMirror m_shuffle;
    ModeMirror m_repeat;
    // qmmp's toggles are being set from a poll: their signals are not sent back
    bool m_applying = false;
};

#endif
```

`src/general/modefollower.cpp`:

```cpp
#include <qmmpui/qmmpuisettings.h>
#include "modefollower.h"
#include "spotifyapi.h"
#include "spotifyclient.h"
#include "spotifypoller.h"

namespace {

// a failure is shown; either way a poll soon shows what Spotify has
SpotifyApi::Callback confirmation()
{
    SpotifyClient *client = SpotifyClient::instance();
    return [client](const ApiResult &result) {
        if(!result.ok)
            client->notify(result.message);
        client->poller()->pollSoon();
    };
}

}

ModeFollower::ModeFollower(QObject *parent)
    : QObject(parent), m_shuffle([this] { return m_clock.elapsed(); }), m_repeat([this] { return m_clock.elapsed(); })
{
    m_clock.start();
    QmmpUiSettings *settings = QmmpUiSettings::instance();
    connect(SpotifyClient::instance()->poller(), &SpotifyPoller::stateChanged, this, &ModeFollower::onState);
    connect(settings, &QmmpUiSettings::shuffleChanged, this, &ModeFollower::sendShuffle);
    connect(settings, &QmmpUiSettings::repeatableListChanged, this, [this] { sendRepeat(); });
    connect(settings, &QmmpUiSettings::repeatableTrackChanged, this, [this] { sendRepeat(); });
}

// only polls of the selected device's playback carry its settings
void ModeFollower::onState(const PlayerState &state)
{
    if(!state.hasPlayback || state.deviceId != SpotifyClient::instance()->deviceId())
        return;
    QmmpUiSettings *settings = QmmpUiSettings::instance();
    const RepeatMode repeat = parseRepeatState(state.repeat);
    m_applying = true;
    if(m_shuffle.polled(state.shuffle))
        settings->setShuffle(state.shuffle);
    if(m_repeat.polled(int(repeat)))
    {
        const QmmpRepeat toggles = qmmpRepeatFor(repeat, QmmpRepeat { settings->isRepeatableList(), settings->isRepeatableTrack() });
        settings->setRepeatableList(toggles.list);
        settings->setRepeatableTrack(toggles.track);
    }
    m_applying = false;
}

void ModeFollower::sendShuffle(bool on)
{
    SpotifyClient *client = SpotifyClient::instance();
    if(m_applying || client->deviceId().isEmpty() || !m_shuffle.qmmpChanged(on))
        return;
    client->api()->setShuffle(client->deviceId(), on, confirmation());
}

void ModeFollower::sendRepeat()
{
    SpotifyClient *client = SpotifyClient::instance();
    QmmpUiSettings *settings = QmmpUiSettings::instance();
    const RepeatMode mode = repeatModeOf(QmmpRepeat { settings->isRepeatableList(), settings->isRepeatableTrack() });
    if(m_applying || client->deviceId().isEmpty() || !m_repeat.qmmpChanged(int(mode)))
        return;
    client->api()->setRepeat(client->deviceId(), repeatStateName(mode), confirmation());
}
```

`src/general/CMakeLists.txt`: add `modefollower.cpp`. `src/general/spotifygeneral.cpp`: add `#include "modefollower.h"` and `new ModeFollower(this);` after `new LinkExpander(m_bindings, this);`.

- [ ] **Step 6: Build, test, install**

Run: `cmake -S . -B build && cmake --build build -j --clean-first 2>&1 | grep -E "warning|error"; ctest --test-dir build --output-on-failure && cmake --install build`
Expected: no warnings or errors; all suites pass.

- [ ] **Step 7: Commit**

```bash
git add src/logic/modesync.h src/logic/modesync.cpp src/logic/CMakeLists.txt src/general/modefollower.h src/general/modefollower.cpp src/general/CMakeLists.txt src/general/spotifygeneral.cpp tests/tst_modesync.cpp tests/CMakeLists.txt
git commit -F - <<'EOF'
Mirror shuffle and repeat between qmmp and Spotify

Claude-Session: https://claude.ai/code/session_01XwRkSvwXRWC8M6LJ7WrpgE
EOF
```

Manual checks pending (Task 9 batch): shuffle and repeat toggled in qmmp and on the phone are followed on both sides.

---

### Task 9: Docs and the manual verification batch

**Files:**
- Modify: `docs/superpowers/specs/2026-09-22-qmmp-spotify-design.md`, `docs/backlog.md`, `README.md`
- On disk only (git-ignored, not committed): `.superpowers/sdd/2026-09-22-qmmp-spotify/global-constraints.md`

- [ ] **Step 1: Update the design spec** (`docs/superpowers/specs/2026-09-22-qmmp-spotify-design.md`)

- `SpotifyApi` list: replace the `play(deviceId, uris, positionMs)` line with `playContext(deviceId, contextUri, offsetUri, positionMs)` / `playUris(deviceId, uris, offsetUri, positionMs)` — `PUT /me/player/play`. Replace `queue(uri)` / `POST /me/player/queue` in the `pause, resume, seek` line with `setShuffle(on)`, `setRepeat(state)`: `PUT /me/player/shuffle`, `PUT /me/player/repeat` (any 2xx is success; the body is an opaque id). Add `playlistName(id)`: `GET /playlists/{id}?fields=name`. In the timeout bullet, replace "(play, pause, resume, seek, queue, next, volume)" with "(starts, pause, resume, seek, shuffle, repeat, volume)", and replace "`play` and `next` return a handle" with "starts return a handle".
- `### Engine`: replace the `enqueue`, `play()` and "Gapless" bullets with:
  - `enqueue(source)`: the first source is the track to play. A later one is qmmp's next item. It is kept but never sent ahead, and it starts only when a run ends while qmmp's next is a Spotify track.
  - `play()`: if the device already plays this track, **adopt** it with no request (this is how the follower moves qmmp). Otherwise send one hand-off, planned by the general plugin from the playing playlist: the bound playlist's context (`context_uri` + `offset`), or a run of consecutive Spotify tracks (`uris` + `offset`, ≤ 500). See `2026-09-26-context-playback-design.md`.
  - Spotify advances on its own. The engine reports a track finished only when a run is over: the device left the run, or stopped on its rewound last track. Long runs hand over the next portion 2.5 s before the portion's last track ends. Nothing is queued.
- Error table, row "Network down": replace "The engine sends the next track's queue call once; at the track end it makes one fallback attempt (skip or play), which stops qmmp with a notice when it fails." with "A start is sent once per hand-off (and once per portion of a long run); a failed one stops qmmp with a notice (systemic) or skips (track-specific)." Replace "A play or Next" with "A start".
- "Testing": replace "queue-ahead timing, switch on queued track" with "hand-off planning, following without requests, finish only when a run is over, portions".
- "Changes during implementation": append one bullet: `- Context playback (2026-09-26, docs/superpowers/specs/2026-09-26-context-playback-design.md) replaced queueing: the bullets above about queuing late, skipping to the queued track and the "device did not advance" fallback are superseded.`

- [ ] **Step 2: Backlog, README, constraints**

`docs/backlog.md`: delete the section "Play Spotify-sourced playlists as Spotify contexts (wanted, needs a design round)". Delete "A poll can end the wait while a coalesced start is still pending". It is about the removed skip-to-queued start, and a pending start of the same track is harmless.

`README.md`, after step 3 of "Use", add:

```markdown
Playlists loaded from Spotify (and a pasted album or playlist link that makes up a whole playlist) are
named "Spotify: <name>" and play as that Spotify playlist: Spotify picks the next track and qmmp follows.
Editing such a playlist in qmmp renames it "<name> (edited)" and it then plays as a plain list; load it
again to link it back. Tools → Spotify shows which applies. qmmp's Shuffle and Repeat follow Spotify's.
```

`.superpowers/sdd/2026-09-22-qmmp-spotify/global-constraints.md` (on disk, git-ignored): replace the Polling line with the policy in `b9-report.md` §4 and B10 §3 (focused+playing 5 s, else 60 s, awaiting 2 s ≤ 20 s, pre/post-end polls, a 204 stops unless awaiting). In the Retries line, replace "the engine sends each next-track queue call at most once and makes one track-end fallback attempt" with "the engine sends one start per hand-off, one per portion of a long run, and one when a run ends into qmmp's next Spotify item; following sends nothing".

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-09-22-qmmp-spotify-design.md docs/backlog.md README.md
git commit -F - <<'EOF'
Docs: context playback replaces queueing

Claude-Session: https://claude.ai/code/session_01XwRkSvwXRWC8M6LJ7WrpgE
EOF
```

- [ ] **Step 4: Manual verification checklist (Alex, live, one batch; `api.log` next to `token.json` shows every request)**

From the spec:
- [ ] Load Liked Songs, double-click track 10: it plays as a context (Spotify shows "Liked Songs"), and a natural advance moves qmmp's cursor, both focused and unfocused. api.log: one `PUT /me/player/play`, then only polls. No `/queue`, no `/next`.
- [ ] **Next with shuffle on**, several times in Liked Songs: something random plays each time, the cursor matches the device, Spotify stays in shuffle, no surprises.
- [ ] Toggle shuffle/repeat in qmmp and on the phone: both sides follow (qmmp's "repeat track" ↔ Spotify's repeat-one). No toggle flips back.
- [ ] Edit a bound playlist: it is renamed `… (edited)`, the menu line changes, and it plays as runs.
- [ ] Mixed playlist: Spotify run → local file (device paused) → next Spotify run.
- [ ] Start an album on the phone: qmmp loads it as `Spotify: <album>` and follows.
- [ ] Let a context end with Autoplay on: qmmp follows. Record which `context` Autoplay tracks report (run `spotify-cli get "/me/player"` then). This is an open question of the spec.
- [ ] A run over 500 tracks crossing a portion boundary (e.g. an edited Liked Songs): the last ~2.5 s of the portion's last track are cut, and the next portion starts cleanly.

Also:
- [ ] Record whether `offset` is honoured with shuffle on: the double-clicked track plays first. This is the spec's other open question.
- [ ] Restart qmmp: bound playlists stay bound (menu line).
- [ ] Toggle qmmp's "group tracks" on a bound playlist: it stays bound.
- [ ] Paste an album link into a new empty playlist: it is renamed `Spotify: <album>` and bound.
- [ ] A phone-started playlist of another user (not readable by the app): its tracks are appended to qmmp's playing playlist, with no repeated failing requests in api.log.
- [ ] Rapid Next / held Next: at most ~1 start per second, no `budget` lines in api.log.
