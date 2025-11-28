# MediaTracker

- **EN README** (You are here)
- **[RU README](README.ru.md)**

## Description

**MediaTracker** is a Windows application built on .NET 8 that tracks currently playing media via System Media Transport Controls (SMTC). The application runs in the system tray and provides an HTTP API on port 5050 for integration with external applications, such as MixItUp.

### Tracked Applications

The application tracks media from any application that supports SMTC, with a priority on music applications like Spotify and Yandex Music. It automatically filters out non-music sources, including browsers (Chrome, Firefox, Edge), video platforms (YouTube, Twitch, Netflix, Prime Video), social networks (Facebook, Instagram), and others, to display only relevant music information.

## System Requirements

- **Operating System**: Windows 10/11 (version 21H1 or newer)
- **[.NET 8 Runtime](https://dotnet.microsoft.com/en-us/download/dotnet/8.0)** - required to run the application (when built in Release mode, the application is self-contained, but in this project SelfContained=false to reduce the executable file size)

## Installation of the Pre-built Executable

1. Download the ready-made executable file `MediaTracker.exe` from the [project releases](https://github.com/weazzylee/media-tracker/releases)
2. Run `MediaTracker.exe` - the application will start in the system tray (near the clock in the taskbar)
3. Ensure the application is running: it should automatically start an HTTP server at `http://127.0.0.1:5050`
4. Check the endpoint: open a browser and navigate to `http://127.0.0.1:5050/` - you should see the current track in the format "Artist - Title" or "Nothing is playing right now"

## Building from Source

Building from source requires the .NET 8 SDK.

1. Clone the repository
2. Navigate to the project directory
3. Run the command:
   ```bash
   dotnet publish --configuration Release -o ./publish
   ```
4. The executable file will be located at `publish/MediaTracker.exe`

## Using Endpoints in MixItUp and OBS

**MediaTracker** provides 3 HTTP endpoints for retrieving current media information:

- `GET http://127.0.0.1:5050/` - Returns a string in the format "Artist - Title" or "Nothing is playing right now"
- `GET http://127.0.0.1:5050/json` - Returns a JSON object with fields: Artist, Title, IsPlaying, SourceApp, Timestamp
- `GET http://127.0.0.1:5050/widget` - HTML Widget for display in OBS

### MixItUp Integration

In **MixItUp**, you can use these endpoints for integration, for example, to create a command that returns the current track for a stream.

**Instructions** are in the file: [install-as-command.md](docs/install-as-command.md)

### OBS Integration

In **OBS**, you can use these endpoints for integration, for example, to display the current song on a stream.

**Instructions** are in the file: [install-as-obs-widget.md](docs/install-as-obs-widget.md)

## License

MIT License - see the [LICENSE](LICENSE) file for details.