# qmmp-spotify backlog

Open items found during implementation, review and manual testing. Each needs its own
short design check before implementation.

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
