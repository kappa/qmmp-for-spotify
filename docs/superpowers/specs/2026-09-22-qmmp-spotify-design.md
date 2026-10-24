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
  - Handles 401 (refresh, retry once), 429 (`Retry-After`, see the rate-limit gate below),
    maps other errors to a message string (Spotify's own message where present).
  - Sends at most 30 interactive requests in any rolling 10 s, and paces paged loads and
    link expansion to 20 per rolling 10 s on a separate budget (request budget, see below).
  - Logs every request to `api.log` next to the token file (see below).
  - Playback commands (play, pause, resume, seek, queue, next, volume) time out after 5 s,
    other requests after 15 s. `play` and `next` return a handle: a superseded start is
    cancelled (not sent if it has not been yet, aborted if in flight; its callback does not
    run), so a request stuck offline cannot reach the device later.
- `SpotifyPoller` — polls `GET /me/player` (the only periodic request), emits
  `stateChanged(PlayerState)`. The schedule (`pollpolicy`) depends on whether qmmp is
  focused (`QGuiApplication::applicationState() == Qt::ApplicationActive`: any qmmp window
  has the focus; reported by the general plugin, qmmp without it counts as focused;
  unfocused and minimized are the same state) and on whether the last poll showed the
  selected device playing (a paused track is not playing):
  - while the engine waits for the device to start a track (its start timeout needs polls):
    every 2 s, for at most 20 s;
  - focused and playing: every 5 s;
  - everything else (focused idle, paused or stopped; unfocused; minimized): every 60 s;
  - while the selected device plays, in every state, two track-end polls from the predicted
    end (poll time + duration − progress, from every successful poll): one 5 s before it,
    so the engine's queue step 1.5 s before the end acts on fresh state, and one 1.5 s after
    it, to confirm the device moved on (or let the engine's "did not advance" fallback act
    on fresh state). They replace the interval only when sooner; each fires at most once per
    track (again after a track change, or a restart or seek back of more than 2 s), and one
    less than 2 s away is skipped (a stalled progress must not repeat it);
  - a successful poll with no playback (204) stops polling unless the engine is waiting for
    a start (a device that has not started yet reports none);
  - failed polls retry after 2, 4, … 256 s; the 9th failure in a row stops polling with
    one notice "Spotify: lost connection, polling stopped";
  - stopped polling resumes on interaction: qmmp becoming focused or a Spotify dialog
    opened, at most one poll per 5 s after the last one (inside that window a
    trigger only brings a later poll forward to the current schedule, or, when polling had
    stopped, polls when the window ends). Command replies poll soon (`pollSoon()`, 300 ms
    after the reply, but at most one poll per 2 s after the last poll: a later
    request is merged into one poll at the end of that window); one arriving while a poll
    is in flight polls again after it. During the
    error backoff `pollSoon()` never polls ahead of the backoff schedule (a failing queue
    call must not turn it into a poll per reply); only a user playback command (play,
    pause, seek, device choice) or an interaction polls earlier, subject to the same 5 s
    throttle. A 204-stop resumes on any of these; a hard fail only on interaction or a user
    playback command, with a fresh retry sequence — automatic engine requests (queueing,
    track ends) do not restart it.
  The gaps count from the last poll's reply (from its sending while it is in flight), so a
  slow reply is not followed by another poll moments later.
  Pauses while logged out. While requests are rate-limit blocked, the next poll is
  scheduled 1 s after the block ends and at least 2 s after the last poll (`pollSoon()`
  does not shorten that); blocked polls
  and polls refused by the request budget are not failures.
- `SpotifyLink` — parses `spotify://{track,album,playlist}/<id>`,
  `spotify:{track,album,playlist}:<id>` and `https://open.spotify.com/[intl-xx/]{track,album,playlist}/<id>[?…]`.
  Canonical playlist path for a track: `spotify://track/<id>`; a track not playable in the
  user's country gets `spotify://track/<id>?unavailable`. Spotify IDs are 22 base62
  characters; a link with another ID is broken.
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
  number, duration. Local files and episodes are skipped; unavailable tracks are kept and
  marked (see "Unavailable tracks" below).
- Notices (not logged in, token expired, no device, API errors) are shown via a
  non-modal message, not repeated on every poll.

## Error handling summary

| Case | Behaviour |
|---|---|
| Not configured / not logged in | Menu actions and playback show "Spotify: log in in plugin settings". |
| 401 | Refresh token, retry once. |
| Refresh rejected (incl. 6-month refresh-token expiry) | Log out, notice "Spotify: please log in again" (see logout below). |
| Logged out while qmmp plays or pauses a Spotify track | qmmp stops quietly (no pause request, no "not logged in"); one notice "Spotify: logged out. The device keeps playing." |
| 429 | Block all Web API requests until `Retry-After` (missing → 30 s, at most 24 h); during a block of ≤ 10 s requests wait for its end, during a longer one they fail with one notice per block; poller waits for the block. |
| 404 / `NO_ACTIVE_DEVICE` | Engine error; notice suggests picking a device. |
| 403 (e.g. not Premium) | Show Spotify's message. |
| Network down | Poller retries after 2 … 256 s (command replies do not shorten that), then stops with one notice until the next interaction; no error spam. The engine sends the next track's queue call once; at the track end it makes one fallback attempt (skip or play), which stops qmmp with a notice when it fails. A play or Next fails within 5 s (command timeout) and stops qmmp with the notice, no skip; a start superseded by a later start, a stop or a pause is aborted, so it cannot land after reconnecting. |
| Request loop (safety net) | At most 30 interactive Web API requests in any rolling 10 s (token refreshes and bulk requests not counted). Paged loads and link expansion have their own pacing (20 per rolling 10 s) and wait, never refused. An interactive request over its budget is not sent: it fails with "Spotify: too many requests, pausing for 1 min" (one notice) and all interactive requests are held back for 60 s through the rate-limit gate; bulk requests wait the block out. |

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

- The follower is edge-triggered: it acts only when the selected device switches track or starts playing compared with the previous poll. A stale poll after Stop therefore cannot restart playback.
- Login success and failure are reported through the plugin's notices, so the outcome is shown even if the settings dialog was closed; a new login releases any pending callback listener first.
- On Qt 6.11 a rejected token refresh arrives only as requestFailed(ServerError); that logs the user out (a transient 5xx does too), network errors do not.
- Play failures are split (superseded by the unavailable-tracks bullet below): systemic ones dispatch `FatalError`, so qmmp stops with the notice instead of trying every following track of the playlist (e.g. when logged out on a long playlist). Failures of a play that was already superseded or stopped are ignored.
- If the device has not started the requested track 10 s after `play`, playback fails with "Spotify: the device did not start playing" (`NormalError`, qmmp skips to the next track). A second such timeout with no successful start in between is `FatalError`: qmmp stops with the same notice.
- `stop()` defers its pause to the next event-loop turn; a `play()` in the same call stack (Next, Previous, double-click, restart) cancels it, so no pause/play race reaches the device. Replaying the track that was just stopped sends play from 0 instead of adopting the device's playback; so does a play after a stop that came before the previous start was confirmed (Next, then Previous before a poll), since the last poll may predate that start. Stop pauses whenever qmmp believes it is playing its track on the device; a paused device is left alone.
- Loaded Spotify playlists are named "Spotify: <playlist name>"; only such playlists are reused and refilled.
- A 429 with `Retry-After` above 10 s fails at once with "Spotify: rate limited, try again in N min" instead of waiting (superseded by the rate-limit gate bullet below).
- Known limitation kept: Spotify queue items cannot be removed. Queuing late and skipping
  to an already-queued track instead of replaying it (see below) together shrink the
  window in which this could cause a duplicate to a tight race around the queue call
  landing, not eliminate it.
- The next track is queued 1.5 s before the end instead of when qmmp announces it (7 s).
- If the device reaches the end of a track with the next one already queued but has not
  advanced to it on its own, qmmp sends Spotify's skip-to-next instead of `play`, so the
  already-queued track is consumed instead of replayed; only if the queue call itself
  failed does qmmp fall back to `play`. The queue call is sent at most once per next track:
  a failed one is not resent (it used to be resent on every 250 ms tick), and the track-end
  fallback is a single attempt.
- Volume control added: while a Spotify track is current, qmmp's volume slider and mute
  drive the device (`PUT /me/player/volume`, debounced 300 ms; mute sends 0, unmute restores
  the level), and volume changes on the device move qmmp's slider. Echoes of our own changes
  are ignored; devices that disallow remote volume get one notice and the slider snaps back
  to the device value. qmmp has one volume, so the level carries over to local files.
- A second `stop()` without a current track is ignored (qsui's double-click runs stop, then
  play, which stops again), so double-clicking the current track restarts it and
  double-clicking a local file still pauses the device. When a Spotify track ends with no
  Spotify track next (a local file follows, or a custom playlist ends), the engine pauses
  the device (deferred like Stop's pause, so a Spotify track qmmp plays next cancels it; a
  403 for an already-paused device is not shown) and the follower ignores the track the
  device was on until the device is seen paused, plays another track, or qmmp starts a
  Spotify track, so Autoplay cannot pull qmmp back.
- At startup the follower compares the first poll with an empty state, so qmmp attaches to
  what the selected device is already playing (selects or appends the track and shows its
  position) instead of waiting for the next track change. This happens once per qmmp
  process: a follower re-created after the plugin's settings are accepted only takes a
  baseline.
- A link Spotify rejects (HTTP 400, 403 or 404) is removed from the playlist, since it could
  not be played either; after network, login, rate-limit or server errors the link is kept
  (a track link still plays after login or reconnect). A playlist link answered with 403 or 404 shows "Spotify: this app can't read
  other users' playlists" (Development Mode apps cannot read playlists owned by others).
  A link whose ID is not 22 base62 characters is removed without a request; it and a link
  answered with 400 ("bad base62 URL") show "Spotify: cannot add this link".
- While qmmp plays a track (not waiting for the device to start, not right after a
  pause/resume/seek, not near the track's end), a poll with no playback on the selected
  device (the device was closed, or playback moved to another device) turns qmmp to Paused
  with one notice "Spotify: playback stopped on <device name>". If the device later reports
  the track playing again, qmmp follows it as usual. A poll with no playback inside the
  pause/resume/seek grace is not trusted, and since polling stops on it, the engine polls
  again when the grace ends.
- Unavailable tracks are shown and skipped: loaded playlists, Liked Songs and expanded links
  keep tracks with `is_playable: false` (Spotify returns the flag without `market` too). A
  play failure that every track would hit (network, not logged in, 401 session expired, 429,
  5xx, device problems, `PREMIUM_REQUIRED`) is `FatalError`: qmmp stops with the notice. Any
  other failure (other 400/403/404, "Restriction violated", the first 10 s start timeout) is
  `NormalError`: qmmp skips to the next track and stops by itself after a whole playlist of
  consecutive failures. A failed skip to the already-queued next track is `FatalError`, since
  it concerns the queue mechanism, not the track. If the failed play had cancelled a deferred pause (Next during
  playback, the next track after a finish), that pause comes back, so when qmmp stops the
  previous track does not keep playing on the device.
- Unavailable tracks are marked: `is_playable: false` (missing: playable) gives the title
  prefix "(unavailable) " and the path `spotify://track/<id>?unavailable`, which qmmp keeps
  in its saved playlists. The engine reads the marker from the path: such a track fails at
  once as `NormalError` without a request (qmmp skips it), it is never queued as the next
  track, and when one follows a playing track the engine skips it at the track's end (if
  qmmp then finds nothing playable, its stop pauses the device). Skipping an unavailable
  track shows no notice, neither for a marked track nor when Spotify answers a play or queue
  call with 403 "Restriction violated".
- Logging out (in the settings, or a rejected refresh) stops the engine quietly: qmmp goes to
  Stopped through a `FatalError` without a message, no pause is sent, and the one notice is
  "Spotify: logged out. The device keeps playing." when qmmp was playing or paused a Spotify
  track (otherwise the logout's own notice). A rejected refresh stops the engine before the
  requests waiting for the token fail, so a play waiting there shows no "not logged in";
  a pause that cannot be sent while logged out shows nothing either.
- Rate-limit gate: any 429 blocks all Web API requests until now + `Retry-After` (missing or
  unparseable → 30 s; a block is extended, never shortened, but lasts at most 24 h). The block
  is kept in `ratelimit` next to the token file, so a restart does not hit a locked account
  (a stored block is capped at 24 h too); logging out clears it. While the remaining block is
  at most 10 s, requests are held and sent when it ends (through the gate again); the request
  that got the 429 is retried once. During a longer block requests are not sent: they fail on
  the next event-loop turn with HTTP 429 and "Spotify: rate limited, try again in N min"
  (N s under a minute). The first long 429 shows a notice; further
  rate-limit notices are suppressed until the block ends. The poller does not count blocked
  polls as failures and polls again 1 s after the block. Token refreshes are not gated.
- Request log: every Web API request and token request appends one tab-separated line to
  `api.log` next to the token file (`<qmmp config dir>/spotify/api.log`, the CLI its own
  dir): local time with ms, verb, path and query, HTTP status (0 = network error), duration
  in ms, note (`retry`, `retry-after=<s>` with the seconds applied, `blocked` for a locally refused
  request, `budget` for one refused by the request budget, or the network error). No headers, tokens or bodies. Mode 0600, rotated to `api.log.1` above
  5 MB. Each line is also sent to the `spotify.api` logging category (off by default;
  `QT_LOGGING_RULES="spotify.api=true"`).
- Request budget: two separate budgets in `SpotifyApi`, next to the rate-limit gate, on a
  monotonic clock (`RequestBudget`; token refreshes are not counted, only requests actually
  sent):
  - interactive requests (polls, playback, volume, dialogs): at most 30 in any rolling 10 s,
    counting only interactive requests, so bulk traffic can never push them over. A request
    over it means something loops: it is not sent, its callback fails on the next
    event-loop turn with status 0, `budgetExceeded` set and "Spotify: too many requests,
    pausing for 1 min", `api.log` gets a line with status 0, duration 0 and note `budget`,
    and a 60 s local block starts (monotonic clock, not persisted; logging out clears it).
    During it interactive requests fail like rate-limited ones ("rate limited, try again in
    N s", `blocked` in the log), whose notices are suppressed until the block ends, so the
    user sees one notice. The poller treats the refused poll like a rate-limited one: not a
    failure, the next poll 1 s after the block;
  - bulk requests (the pages of playlist, Liked Songs, album and playlist-list loads, and
    `track()` for link expansion): paced to at most 20 in any rolling 10 s, counting only
    bulk requests. They are finite, so they wait instead of failing: in a FIFO queue, woken
    in order, one per free slot (a woken request goes through the gate and the token check
    again). They also wait out the 60 s local block. During a long Spotify 429 block they
    still fail, and the load's notice names it ("Spotify: couldn't load <playlist | Liked
    Songs | links>: rate limited, try again in N min"), so it is not hidden with the block's
    other rate-limit notices.
  At most 50 requests per 10 s in total, and only while a load runs.
- Track starts are coalesced: the first play (or skip to the queued track) of a burst goes out
  at once, later ones within 1 s replace each other and the latest is sent when the 1 s ends,
  so a held Next key sends about one play per second, also across the engine instances qmmp
  creates for a playlist mixing Spotify and local items (the time of the last start sent is
  kept on `SpotifyClient`); qmmp's track and state switch at once,
  and the start timeout counts from the request actually sent. A pending start is dropped
  when qmmp stops, fails or the track changes. While a play is pending, a poll showing its
  track does not end the wait (the poll may predate the play); a pending skip to the queued
  track is dropped when a poll shows the device already reached that track. With the 2 s
  confirmation-poll gap, skimming (15 Next clicks in 10 s) or a held Next key stays at about
  15 interactive requests per 10 s, well under the budget.
- Seeks are coalesced like volume: the engine sends one seek 300 ms after the last of a burst
  (a held key, a scrolled slider); the local position moves at once. A pending seek is
  dropped when the track changes or stops.
- Polling by focus (replaces the window-visibility rule): the general plugin reports the
  application state instead of watching windows; focused and playing polls every 5 s,
  everything else every 60 s, and both track-end polls (5 s before, 1.5 s after) apply in
  every state, each at most once per track.
