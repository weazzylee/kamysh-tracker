# Installing MediaTracker and Displaying the Widget in OBS via Browser Source

## Prerequisites

- MediaTracker is installed and running. Installation instructions are in the file [README.md](../README.md#installation-of-the-pre-built-executable)

## Step 1: Setting up OBS

1. Open OBS Studio.
2. Add a new Browser Source to your scene:
   - Click "+" in the sources window.
   - Select "Browser Source".
   - Name the source, for example, "Media Widget".
   - In the source settings:
     - URL: `http://127.0.0.1:5050/widget`
     - Width: 400 (or to your taste)
     - Height: 180
     - Check "Shutdown source when not visible" to save resources.
     - Check "Refresh browser when scene becomes active".
   - Click "OK" to save.

The widget automatically updates every 5 seconds and changes style depending on the application (Spotify - green, Yandex Music - blue, others - gray).

## Notes

- Make sure MediaTracker is running before streams.
- The widget uses a transparent background suitable for OBS.
- If the widget doesn't load, check that the endpoint is available at http://127.0.0.1:5050/widget
- For customization, create a widget.html file next to the .exe file. Template for customization - [widget.html](../Resources/widget.html)