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

### Leftovers from the B10 review
- 403 "Restriction violated" is now always a silent skip; if Spotify uses it for a
  device-level restriction, qmmp could walk a whole playlist silently (~1 play/s).
- The 5 s timeout for playback commands may stop qmmp on a slow but successful start
  (e.g. a phone waking up); consider ~8 s for starts.
- Minor: a pause goes out before skipping over an unavailable next track; a second
  "not logged in" notice can follow the logout notice on session expiry; the follower
  adds a duplicate if a marked track ever plays; "Operation canceled" lines in api.log.
