# qmmp-spotify backlog

Open items found during implementation, review and manual testing. Each needs its own
short design check before implementation.

## Play Spotify-sourced playlists as Spotify contexts (wanted, needs a design round)

Today qmmp owns the order and feeds the device one track at a time (plus one queued
track ~7 s before the end). Other Spotify clients instead play a *context* ("Liked Songs
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

## Volume control (needed)

qmmp's volume slider does nothing for Spotify playback: it drives qmmp's local output,
which the Spotify engine does not use.

Agreed direction (2026-09-24):
- qmmp → device: while the Spotify engine is active, listen to `SoundCore::volumeChanged`
  and `mutedChanged`; send `PUT /me/player/volume?volume_percent=N&device_id=…`,
  debounced (~300 ms). Mute sends 0, unmute restores the previous level.
- Device → qmmp: the polled player state carries `device.volume_percent`; when it changes
  on the device (e.g. from the phone), set qmmp's volume. Ignore echoes of our own changes.
- Debounce, echo suppression and mute handling as a pure, unit-tested unit in `src/logic`;
  the engine only wires it.
- Caveats: qmmp has one volume, so it carries over to local files; some devices reject
  remote volume (`VOLUME_CONTROL_DISALLOW`, often iPhones) → one notice, slider snaps back.

## Failed track change leaves the old track playing

Next/double-click from track A to B, where B's play fails or times out: the pending pause
for A was cancelled, so A keeps playing on the device and qmmp's Stop cannot pause it
(the engine no longer owns a track). Fix: remember the cancelled pause for that play
generation and send it in `EngineLogic::fail()` when the device still plays A; add tests
for the failure and the timeout path.

## Skinned UI: Tools menu missing

With the skinned UI, the main menu shows no Tools entry although the Spotify plugin is
loaded; the Simple UI shows Tools → Spotify correctly. Unexplained: `UiHelper` hides the
Tools submenu while it has no actions. Observed after enabling the plugin while qmmp was
running and accepting its settings; not yet checked after a clean restart.

## Stale queue entry replays a track

Spotify queue items cannot be removed through the Web API. Skipping or stopping in the
last ~7 s of a track leaves the queued next track on the device, which may then play it
twice. Accepted by the spec; options if it bites: skip the stale entry with
`POST /me/player/next`, or drop queueing and start the next track explicitly (short gap).
