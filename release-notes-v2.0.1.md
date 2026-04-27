# KamyshTracker v2.0.1

- Added optional `Twitch Client Secret` support for Twitch apps configured as Confidential clients.
- Token exchange and token refresh now include `client_secret` when it is configured.
- Improved the `missing client secret` error with a clear setup hint.
- Updated the default Twitch Client ID to `7hfl0kpbpsxpz0j2v1tg0q6hrvi6iw`.
- Updated README instructions for Public vs Confidential Twitch apps.

Note: this release currently publishes source archives only. A rebuilt plugin zip is not attached because the local Qt/OBS build dependency folder is not available in this workspace.
