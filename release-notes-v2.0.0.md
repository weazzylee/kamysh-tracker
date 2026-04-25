# KamyshTracker v2.0.0

- Native OBS Studio plugin for Windows x64.
- Removed old tray app, local HTTP API, `/json`, `/widget`, and Browser Source flow.
- Reads current media through Windows SMTC.
- Adds OBS Tools menu settings UI.
- Supports Twitch Device Code Flow auth without client secret.
- Uses Twitch EventSub WebSocket and Helix Send Chat Message.
- Replies from the authorized streamer account after OBS/Twitch account check.
- Supports multiple command aliases separated by spaces, default `!трек !track !shazam !шазам`.
- `Test chat message` sends the real current-track response through the normal chat-send path.
- Tested with OBS Studio `32.1.2` on Windows x64.
