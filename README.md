# qmmp-spotify

qmmp plugins that control Spotify Connect devices: play, pause, seek and skip on your phone, a speaker,
the desktop app or Spotify Soloist from qmmp; load your Spotify playlists and liked songs into qmmp
playlists; add `spotify:` and `open.spotify.com` links; and qmmp follows what the device plays.

Audio is played by the selected device, not by qmmp. For audio on this machine, run
[Spotify Soloist](https://developer.spotify.com/documentation/soloist) or the desktop app and pick it as the device.

## Requirements

- Spotify Premium.
- Your own Spotify app: create one at https://developer.spotify.com/dashboard (Web API), add the redirect URI
  `http://127.0.0.1:8898/callback`, copy the client ID. New apps are limited to 5 users by Spotify.
- qmmp 2.4, Qt 6 with NetworkAuth (`qt6-qtbase-devel`, `qt6-qtnetworkauth-devel`), CMake ≥ 3.18.

## Build and install

```bash
export PKG_CONFIG_PATH=<qmmp prefix>/lib64/pkgconfig
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
cmake --install build
```

## Use

1. Settings → Plugins → General → enable "Spotify Plugin" → Preferences: paste the client ID, Log In.
2. Tools → Spotify → Device…: pick a device.
3. Tools → Spotify → Load Playlist… / Load Liked Songs, or add a Spotify link via Add URL.

`build/tools/spotify-cli` (with `SPOTIFY_CLIENT_ID` set) runs the same API calls from a shell for debugging.
