# qmmp-spotify backlog

Open items found during implementation, review and manual testing. Each needs its own
short design check before implementation.

## Normal priority

### Play Spotify-sourced playlists as Spotify contexts (wanted, needs a design round)

Today qmmp owns the order and feeds the device one track at a time (plus one queued
track 1.5 s before the end). Other Spotify clients instead play a *context* ("Liked Songs
from track 37"): Spotify owns order, shuffle, repeat and Up Next, and playback continues
after any client closes. Alex prefers the context model: he does not edit playlists in
qmmp.

Direction (2026-09-24):
- A qmmp playlist loaded from Spotify (Load Playlist, Load Liked Songs, a pasted album or
  playlist link) is bound to its context. Playing track N sends `PUT /me/player/play` with
  `context_uri` + `offset`; next/previous become Spotify's; qmmp's shuffle/repeat toggle
  Spotify's; qmmp follows whatever Spotify plays. Closing qmmp changes nothing.
- qmmp has no read-only playlists, so detect structural edits (add/remove/move/sort) and
  detach the playlist: it then plays track-by-track in qmmp's order, with a one-time
  notice. Loading it again re-binds it.
- Hand-built or mixed playlists keep track-by-track playback.
- To verify first: Liked Songs as a context (`spotify:user:<id>:collection`) is not
  officially documented for playback; Spotify's shuffle order is not visible via the API,
  so qmmp only follows, it cannot know the next track in advance.
- Supersedes "hand the upcoming qmmp playlist to Spotify as a uris list".
- End of playlist (decided 2026-09-24): in context mode qmmp follows the server (Spotify's
  Autoplay may continue, qmmp follows it); for custom or edited playlists (track-by-track
  mode) qmmp pauses the device when its playlist ends.

### Mark unavailable tracks in the playlist (wanted, 2026-09-26)

Spotify returns `is_playable: false` (with `restrictions.reason`, e.g. `market`) for tracks
that can't be played in Alex's country: 14 in "My India" (e.g. "Jeva Nadhi"), 30 in Liked
Songs. The plugin ignores the flag and finds out only when playing fails.
- Prefix the title of such tracks with `(unavailable) `, e.g. "(unavailable) Jeva Nadhi".
- Possibly skip them without sending a play request (to decide at design time).
- No message box when an unavailable track is skipped: today Spotify's 403 text
  ("restriction violated") pops up even though qmmp skips to the next track
  (Alex, 2026-09-26: "the message box is not needed").

### Clear message for a broken link (wanted, 2026-09-26)

Adding a malformed Spotify link (e.g. a mangled track ID) shows Spotify's raw error
"bad base62 URL". Replace it with a plain message along the lines of
"Spotify: cannot add this link" (Alex).

### No device selection: play on the current device (wanted, 2026-09-26)

Replaces "follow playback to another device". Today qmmp targets one device chosen in the
Device dialog and pauses when playback moves elsewhere ("closed device" ruling, 5b6f17d).
Alex wants no device selection:
- Something is active: qmmp plays on the active device (Web API calls without
  `device_id`). When playback moves to another device, qmmp simply follows it; the
  "moved elsewhere → pause" logic goes away.
- Nothing is active: pick a device silently, no dialog:
  1. the last device qmmp played on, if it is in `GET /me/player/devices`;
  2. otherwise the first device that is not `is_restricted`;
  3. no devices: notice "Spotify: no device available, open Spotify somewhere".
- The Device dialog goes away, or stays only as an optional menu item to pick a device by
  hand (decide at design time).
- Fits context playback.

## Bugs from testing (2026-09-26)

Found by Alex in the manual session after B7/B8; details and log excerpts in
`.superpowers/sdd/2026-09-22-qmmp-spotify/progress.md`.

### "Nothing active" (204) does not stop polling while qmmp is Playing
Web player closed while qmmp played: `GET /me/player` → 204 every 2 s, polling never
stopped (the 204 stop is skipped while the engine is active, and the engine stays
active).

### Next while offline advances and "plays"; a stale play fires after reconnect
Offline, Next moved to the next track and qmmp showed it playing; the error came only
after the 15 s request timeout. Two plays were sent; a third went out when the first
timed out (looks like a skip on a network error, which should stop). When the network
returned, a pending play went through (09:59:12.37) and the new track played, although
qmmp sent a pause 0.08 s later. Ideas: shorter timeout for playback commands (~5 s); a
network error stops without skipping; drop/cancel a play superseded by a later command.

### Duplicate polls ~0.4 s apart
After the stop in the offline test, polls sometimes came in pairs ~0.4 s apart
(two poll chains, or the in-flight replay flag).

### Closing the web player tab while playing: unclear state, no notice
Position stops, but qmmp doesn't clearly show Paused and no "playback stopped" notice
appears. Superseded by "No device selection" if that lands first.

### Logging out while playing leaves qmmp Playing
After Log out, qmmp keeps Playing with the position running (device keeps playing); Stop
then shows "not logged in". Logging out should stop qmmp's engine quietly (and maybe say
the device keeps playing).

### Next then Previous before a poll leaves qmmp and the device apart
Found by the B9 round-3 re-review (older bug, easier to hit with rapid Next/Previous).
`EngineLogic::play()` has a shortcut that trusts the last poll even when it is stale: A
plays, Next sends B, Previous before any poll → qmmp shows A and sends nothing while the
device plays B, until A's predicted end.

### A poll can end the wait while a coalesced start is still pending
B9 round-3 re-review, Minor 1. If a skip-to-queued is pending and the device reaches that
track by itself, the pending skip is still sent and jumps past it (qmmp shows one track,
the device plays another). Needs the track to end within 1 s of the start; rare. Fix: drop
a pending skip in `apply()`, or don't let a poll end the wait while a start is pending.

### Start coalescing resets with each engine
B9 round-3 re-review, Minor 2. qmmp deletes the Spotify engine when switching to a local
file, so a held Next over a playlist alternating Spotify and local items isn't throttled
and can trip the 60 s block (then Next stops qmmp). Fix: keep the last-start time on
`SpotifyClient`.
