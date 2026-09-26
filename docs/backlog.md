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

### Follow playback to another device (wanted, 2026-09-26)

Today, when playback moves from the selected device to another one (e.g. transferred to
the phone), qmmp pauses with a notice ("closed device" ruling, 5b6f17d). Alex prefers
following, like Spotify's own apps: make the new device the selected one and keep going —
qmmp shows what it plays and further commands act on it. A device that disappears with
nothing playing elsewhere still pauses. Fits context playback.
