# qmmp-spotify backlog

Open items found during implementation, review and manual testing. Each needs its own
short design check before implementation.

## High priority

### Volume control (needed)

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

### Skinned UI: Tools menu missing until restart (cause unknown)

Seen by Alex 2026-09-24: enabled the Spotify plugin while qmmp (skinned UI) was running,
logged in via its Preferences (OK, not Cancel) — the right-click menu had no Tools. After
restarting qmmp with the plugin enabled, Tools → Spotify shows. The Simple UI showed Tools.
An isolated investigation could not reproduce the no-Cancel case (it only reproduced the
symptom when qmmp's Settings dialog was closed with Cancel, which reverts plugin
enabling — that is not what happened). Needs a fresh look at the enable-at-runtime path in
the skinned UI (`UiHelper` TOOLS_MENU autoHide, `skinnedmainwindow.cpp:429`).

### Crash on quit

qmmp segfaults on every quit with the plugin loaded: `~SpotifyGeneral` calls
`UiHelper::instance()->removeAction(...)` after UiHelper is destroyed
(`src/general/spotifygeneral.cpp:33`, confirmed in gdb). Fix: drop the call — UiHelper
already removes the action when it is destroyed (`uihelper.cpp:133`).

### Stale queue entry replays a track

Spotify queue items cannot be removed through the Web API. qmmp announces the next track
~7 s before the end and the engine queues it at once; acting in that window (Next, pick
another track, Stop) leaves the queued track on the device, which then plays it once more.
Found by code review, not seen in testing.
Decision (2026-09-24): **queue late** for custom playlists; context mode for Spotify-sourced
ones. Options considered ("skip the leftover when it starts" rejected as ugly and brittle):
- No queue: the engine starts the next track itself exactly at the end of the current
  one — never stale, but a short gap (~0.3–1 s) between tracks.
- Queue late: hold qmmp's next track and queue it only ~1.5 s before the end — gapless,
  window shrinks from 7 s to 1.5 s; a slow network can still cause a gap.
- Context mode makes it irrelevant for Spotify-sourced playlists (nothing is queued).

## Normal priority

### Play Spotify-sourced playlists as Spotify contexts (wanted, needs a design round)

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
- End of playlist (decided 2026-09-24): in context mode qmmp follows the server (Spotify's
  Autoplay may continue, qmmp follows it); for custom or edited playlists (track-by-track
  mode) qmmp pauses the device when its playlist ends.

### Unavailable tracks: show them, skip them on play (wanted)

Alex (2026-09-24): show all songs in loaded playlists, even unplayable ones, but never stop
on them — skip to the next track, as Spotify does.
- Today every play failure stops qmmp (ruling from the final review, made to stop qmmp
  walking a whole playlist on errors that affect every track). Split the errors instead:
  track-specific (unavailable, removed) → skip to the next track; systemic (not logged in,
  device gone, no network, Premium required, rate limited) → stop with a notice.
- Stop rule: rely on qmmp's built-in guard — on a track error qmmp skips to the next track
  and stops after trackCount consecutive failures (`qmmpui/mediaplayer.cpp:130-136`), so an
  all-unplayable playlist stops after one pass. Track-specific failures must therefore be
  reported as NormalError (skip), systemic ones as FatalError (stop).
- Loading: `parseTrack` currently drops tracks with `is_playable: false`; keep them
  (optionally marked) instead.
- Verify first with the CLI: whether `market` on playlist/liked-songs requests still makes
  Development Mode return `is_playable` / `restrictions` (documented on Get Playlist Items;
  not mentioned in the Feb 2026 migration guide).
- Related, from the final code review: when a track change fails on a systemic error, the
  previous track keeps playing on the device and qmmp's Stop cannot pause it (the pending
  pause was cancelled). Pause the device on that path.

### Double-click on the current track does nothing (low priority)

Double-clicking the track that is playing should behave like double-clicking any other
track: start it from the beginning. It does nothing in the Simple UI. The Play button
does restart it.

Cause (traced 2026-09-24): qsui's double-click calls `MediaPlayer::stop()` then `play()`
(`qsuilistwidget.cpp:278-280`), and `SoundCore::play()` calls stop again, so the engine
sees stop → stop → play. The second `EngineLogic::stop()` has no current track and
overwrites the first one's restart candidate and pending pause
(`src/logic/enginelogic.cpp:114-115`); `play()` then adopts the still-playing track and
sends nothing.
Fix: make `stop()` a no-op when there is no current track; tests for stop → stop → play of
the same track (expects play from 0) and for a double stop still pausing the device.

### Moving from a Spotify track to a local file leaves the device playing

When qmmp leaves a Spotify track for a non-Spotify item, the device must be paused.
Two paths fail today:
- Switching by double-click: the double stop cancels the pending pause (same root cause
  as "Double-click on the current track does nothing", `src/logic/enginelogic.cpp:114`).
  Seen in manual testing 2026-09-24: local file plays through qmmp while the device keeps
  playing the Spotify track.
- Natural end of a Spotify track with a local file next: `EngineLogic::finish()` never
  pauses the device. With Spotify Autoplay on, the device continues with a recommendation
  and the follower may pull qmmp back to Spotify (predicted, not yet tested).
Fix: pause the device whenever the engine hands over to another engine; covers both paths.

### Attach to the device's ongoing playback at startup

After a qmmp restart the login survives, but qmmp does not show what the device is
already playing until the next track starts: the follower is edge-triggered and treats
the first poll as a baseline (ruling during Task 13, to avoid grabbing playback at startup
and to protect Stop from stale polls). Seen in manual testing 2026-09-24.
Proposed: the first poll after startup attaches when the selected device is playing
(select or append the current track and show its position). Safe for Stop, since nothing
has been stopped at startup. Change in `Follower`/`isFollowTrigger` plus a test.

### Adding another user's playlist link leaves the raw link behind

Add URL with a playlist owned by someone else: Spotify answers "Resource not found"
(Development Mode cannot read other users' playlists), the notice shows Spotify's raw text,
and the unexpanded link stays in the qmmp playlist (`src/general/linkexpander.cpp:56-59`
keeps the raw entry on failure); it cannot be played. Seen in manual testing 2026-09-24.
Fix: remove the raw entry when expansion fails; for playlist links answered with 403/404
say "Spotify doesn't let this app read other users' playlists".

### Closing the device leaves qmmp "playing"

Manual test 2026-09-24: with a Spotify track playing on the web player, closing the web
player leaves qmmp in Playing with the position still advancing. Expected: qmmp notices
the device is gone (paused/stopped) and shows it. About 30 s later a notice appears:
"the selected device is not available". Not yet investigated.
