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
