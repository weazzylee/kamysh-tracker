# 2.0.3

- OBS Studio baseline is now 30.0.0+.
- CMake supports both OBS frontend API source layouts:
  - `UI/obs-frontend-api`
  - `frontend/api`
- Added a runtime guard so OBS versions below 30.0.0 log a clear error and do not load the plugin.
- Kept the optimized SMTC lifecycle: event-driven refresh with a rare fallback poll.
- Kept Twitch chat replies on a dedicated queue/worker so EventSub WebSocket callbacks are not blocked by Helix requests.
- Release package contains only the plugin DLL and KamyshTracker data files; Qt runtime DLLs and TLS plugins are not bundled.
