# qmmp-spotify — design

Date: 2026-09-22
Status: draft, awaiting review

## Goal

Use qmmp as a Spotify player and remote control:

- pick a Spotify Connect device and control playback on it from qmmp;
- load my Spotify playlists and liked songs into qmmp playlists;
- add Spotify links (`spotify://…`, `https://open.spotify.com/…`) to qmmp playlists;
- qmmp follows what happens on the device, including changes made from the phone.

Local playback is not done by the plugin. To hear Spotify on this machine, run
Spotify's official headless Connect client **Soloist** (or the desktop app); it is
just another device in the picker.

## Decisions and why

| Decision | Reason |
|---|---|
| Web API only, no librespot, no Rust | librespot is unofficial and fragile (audio key errors for newer accounts, streaming client-ID requirements). Soloist gives official local playback, and controlling it through the Web API needs no special code. |
| Each user registers their own Spotify app; nothing hardcoded | New apps are in Development Mode (max 5 users, owner needs Premium, since Feb 2026); Extended Quota is only for organisations with 250k+ MAU. ncspot/spt work for everyone only because their apps are grandfathered. |
| Not a Connect target itself | Not needed. |
| No search, no playlist editing | Not needed. |
| Soloist's WebSocket API is not used | Web API control is uniform for every device; latency of a few hundred ms is acceptable. |

References: [Feb 2026 migration guide](https://developer.spotify.com/documentation/web-api/tutorials/february-2026-migration-guide),
[quota modes](https://developer.spotify.com/documentation/web-api/concepts/quota-modes),
[Soloist](https://developer.spotify.com/documentation/soloist). ncspot 1.4.0 (in this directory) is
the reference for a clean open-source client.

## Architecture

One out-of-tree CMake project producing a shared library and three qmmp plugins.
Qt 6 (Widgets, Network, NetworkAuth). Targets qmmp 2.4.2 (latest), built from
source by the user; our project finds it via pkg-config (`qmmp`, `qmmpui`, e.g. with
`PKG_CONFIG_PATH` pointing at its install prefix; the `plugindir` variable gives the
plugin install location). The distro's older qmmp is not a target.

| Unit | Kind | Responsibility |
|---|---|---|
| `libspotifycore` | shared library | OAuth + token storage, Web API client, selected device, the player-state poller, link parsing and expansion. Singleton shared by all plugins. |
| `spotify` transport | `Transports/` (`InputSourceFactory`) | Makes qmmp accept Spotify links. Claims protocol `spotify` and regexp `^https?://open\.spotify\.com/`. At play time returns an input source with no I/O device that is immediately ready, so the internal engine rejects it and qmmp falls through to our engine. |
| `spotify` engine | `Engines/` (`EngineFactory` + `AbstractEngine`) | Plays `spotify://track/<id>` (and raw `open.spotify.com/track/…`) on the selected device; mirrors device state into qmmp. |
| `spotify` general | `General/` (`GeneralFactory`) | Tools menu, settings dialog, the follower, link expansion in playlists. |

### Why these plugin types (verified in qmmp source)

- `FileLoader` drops added URLs whose scheme is not claimed by a decoder or transport
  (`qmmpui/fileloader.cpp:115-129`); engines do not count. Hence the transport.
- URL expansion via `createPlayList` is only called for decoders; transport-matched URLs
  become a bare `TrackInfo(path)` (`qmmp/metadatamanager.cpp:153-170`). Hence expansion
  happens in the general plugin after the tracks are added.
- `AbstractEngine::create` tries the internal engine first and plugins only if it
  rejects the source (`qmmp/abstractengine.cpp:99-129`). The internal engine rejects a
  source with no decoder and no I/O device (`qmmp/qmmpaudioengine.cpp:122-138`).
- The engine learns the next track ahead of time through
  `StateHandler::sendNextTrackRequest()` → `MediaPlayer::updateNextUrl()` → `enqueue()`
  (`qmmpui/mediaplayer.cpp:94-124`).

## Components

### libspotifycore

- `SpotifyAuth` — authorization code flow with PKCE (`QOAuth2AuthorizationCodeFlow`,
  `QOAuthHttpServerReplyHandler` on `127.0.0.1:<port>`, path `/callback`). No client
  secret. Scopes: `user-read-playback-state user-modify-playback-state
  user-read-currently-playing playlist-read-private playlist-read-collaborative
  user-library-read`. Refresh token stored in `<qmmp config dir>/spotify/token.json`,
  mode 0600. Access token in memory, refreshed before expiry.
- `SpotifyApi` — async calls returning parsed value types:
  - `devices()` — `GET /me/player/devices`
  - `playerState()` — `GET /me/player`
  - `play(deviceId, uris, positionMs)` — `PUT /me/player/play`
  - `pause`, `resume`, `seek`, `queue(uri)` — `PUT /me/player/pause`, `PUT /me/player/play`, `PUT /me/player/seek`, `POST /me/player/queue`
  - `myPlaylists()` — `GET /me/playlists` (all pages)
  - `playlistTracks(id)` — `GET /playlists/{id}/items` (all pages)
  - `likedTracks()` — `GET /me/tracks` (all pages)
  - `album(id)` + album tracks, `track(id)` — for link expansion
  - Handles 401 (refresh, retry once), 429 (`Retry-After`), maps other errors to a
    message string (Spotify's own message where present).
- `SpotifyPoller` — polls `GET /me/player`: ~2 s while the selected device is playing
  or qmmp's engine is active, ~10 s otherwise. Emits `stateChanged(PlayerState)`.
  Network errors back off exponentially up to 30 s. Pauses while logged out.
- `SpotifyLink` — parses `spotify://{track,album,playlist}/<id>`,
  `spotify:{track,album,playlist}:<id>` and `https://open.spotify.com/[intl-xx/]{track,album,playlist}/<id>[?…]`.
  Canonical playlist path for a track: `spotify://track/<id>`.
- Selected device id persisted in qmmp settings.

`PlayerState`: device id, is_playing, track id, title, artists, album, duration,
progress, timestamp.

### Engine

- `supports(path)`: `SpotifyLink` parses it as a track.
- `enqueue(source)`: first source → current; later sources → pending next.
- `play()`: if the poller says the selected device is already playing this track,
  **adopt** (no API call, just start mirroring). Otherwise `play(device, [uri], offset)`.
- `pause()`, `seek()`, `stop()` → matching API calls. `stop()` = pause on device.
- Mirroring: on each `stateChanged` dispatch state (Playing/Paused), elapsed time,
  duration and track metadata to `StateHandler`.
- Gapless: the engine dispatches elapsed time every 250 ms; qmmp's `StateHandler` then
  requests the next track by itself 7 s before the end (`qmmp/statehandler.cpp:69-91`).
  When the next source arrives, `queue(uri)` it on the device. When the poller shows the queued track
  playing, switch current to it and dispatch the new track (same pattern as the internal
  engine's gapless path).
- If the device plays a track the engine didn't expect, the engine does nothing
  itself — the follower handles it (below).
- No device selected / device gone → dispatch `NormalError`; general plugin shows a
  message suggesting to pick a device.

### General plugin

- **Tools → Spotify** menu:
  - *Device…* — list of devices with refresh; selecting stores the device id.
  - *Load playlist…* — list of my playlists; loading fills a qmmp playlist named "Spotify: <playlist name>" (never a local playlist of the plain name).
  - *Load liked songs* — fills qmmp playlist "Spotify: Liked Songs".
  - Re-loading clears and refills the same-named qmmp playlist.
- **Settings dialog**: client id, redirect port (default 8898), the exact redirect URI
  to register (`http://127.0.0.1:8898/callback`), Login/Logout button, status line.
- **Follower**: on a change of the device's track or play state (when `stateChanged`
  fires for the selected device), if the track differs from what qmmp is playing
  (including qmmp stopped while the device plays): find `spotify://track/<id>` in the
  current playlist, else append it (with metadata); make it current and start playback.
  The engine adopts, so the device is not restarted. Events for other devices are ignored.
- **Link expansion**: connected to `tracksAdded` of every playlist
  (`PlayListManager` signals for new playlists). Each added track whose path parses as
  a `SpotifyLink` and is not already a canonical, tagged track is replaced in place by
  the expansion: one tagged track for a track link, all tracks for album/playlist links.
  Raw links are visible briefly until expansion completes.
- Tracks built for qmmp: path `spotify://track/<id>`, title, artist, album, track
  number, duration. Local files, episodes and unavailable tracks are skipped.
- Notices (not logged in, token expired, no device, API errors) are shown via a
  non-modal message, not repeated on every poll.

## Error handling summary

| Case | Behaviour |
|---|---|
| Not configured / not logged in | Menu actions and playback show "Spotify: log in in plugin settings". |
| 401 | Refresh token, retry once. |
| Refresh rejected (incl. 6-month refresh-token expiry) | Log out, notice "Spotify: please log in again". |
| 429 | Honour `Retry-After`; poller waits the same. |
| 404 / `NO_ACTIVE_DEVICE` | Engine error; notice suggests picking a device. |
| 403 (e.g. not Premium) | Show Spotify's message. |
| Network down | Poller backs off to 30 s; no error spam. |

## Testing

Unit tests (Qt Test), minimal:

- JSON parsing of devices, player state, paged playlist/liked items, album — against
  saved real responses.
- `SpotifyLink` parsing.
- Engine state logic with a fake API/poller: adopt vs start, queue-ahead timing,
  switch on queued track, error on missing device.
- Follower: select existing vs append, ignore other devices.

Manual checklist against a real account with Soloist and a phone: login; device
switch; play/pause/seek from qmmp and from the phone; phone song change while qmmp
playing and while stopped; gapless transition; add track/album/playlist links in both
forms; load playlist and liked songs; token expiry (corrupt stored token).

## Out of scope

Local audio decoding, being a Connect target, search, playlist editing, podcasts.

## To verify during implementation

- `/playlists/{id}/items` and paging behave as documented (quick curl spike first).
- Whether Spotify accepts a dynamic loopback port; if yes, the port setting can go.
- Build dependencies available: `qt6-qtbase-devel`, `qt6-qtnetworkauth-devel`
  (not installed yet), and a qmmp 2.4.2 install prefix with its `.pc` files.

## Changes during implementation

- The follower is edge-triggered: it acts only when the selected device switches track or starts playing compared with the previous poll. qmmp therefore does not take over playback at startup if the device is already playing, and a stale poll after Stop cannot restart playback.
- Login success and failure are reported through the plugin's notices, so the outcome is shown even if the settings dialog was closed; a new login releases any pending callback listener first.
- On Qt 6.11 a rejected token refresh arrives only as requestFailed(ServerError); that logs the user out (a transient 5xx does too), network errors do not.
- Device problems and all other play failures dispatch `FatalError`, not `NormalError`: qmmp stops with the notice instead of trying every following track of the playlist (e.g. when logged out on a long playlist). Failures of a play that was already superseded or stopped are ignored.
- If the device has not started the requested track 10 s after `play`, playback fails with "Spotify: the device did not start playing" (`FatalError`).
- `stop()` defers its pause to the next event-loop turn; a `play()` in the same call stack (Next, Previous, double-click, restart) cancels it, so no pause/play race reaches the device. Replaying the track that was just stopped sends play from 0 instead of adopting the device's playback. Stop pauses whenever qmmp believes it is playing its track on the device; a paused device is left alone.
- Loaded Spotify playlists are named "Spotify: <playlist name>"; only such playlists are reused and refilled.
- A 429 with `Retry-After` above 10 s fails at once with "Spotify: rate limited, try again in N min" instead of waiting.
- Known limitation kept: Spotify queue items cannot be removed. Queuing late and skipping
  to an already-queued track instead of replaying it (see below) together shrink the
  window in which this could cause a duplicate to a tight race around the queue call
  landing, not eliminate it.
- The next track is queued 1.5 s before the end instead of when qmmp announces it (7 s).
- If the device reaches the end of a track with the next one already queued but has not
  advanced to it on its own, qmmp sends Spotify's skip-to-next instead of `play`, so the
  already-queued track is consumed instead of replayed; only if the queue call itself
  failed does qmmp fall back to `play`.
- Volume control added: while a Spotify track is current, qmmp's volume slider and mute
  drive the device (`PUT /me/player/volume`, debounced 300 ms; mute sends 0, unmute restores
  the level), and volume changes on the device move qmmp's slider. Echoes of our own changes
  are ignored; devices that disallow remote volume get one notice and the slider snaps back
  to the device value. qmmp has one volume, so the level carries over to local files.
