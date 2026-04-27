# KamyshTracker v2.0.2

- Fixed a crash risk on OBS 32.1.1 caused by packaging Qt runtime DLLs from a different Qt build.
- The release zip now contains only the OBS plugin DLL and plugin data files.
- KamyshTracker now relies on the Qt runtime bundled with the installed OBS Studio.

If you installed v2.0.1, replace it with this release and make sure old copied files from that package are removed from `bin\64bit`:

- `Qt6WebSockets.dll`
- `tls\qcertonlybackend.dll`
- `tls\qschannelbackend.dll`
