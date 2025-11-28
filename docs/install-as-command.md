# Creating a !track Chat Command in Mix It Up to Display the Current Track

## Prerequisites

- MediaTracker is installed and running. Installation instructions are in the file [README.md](../README.md#installation-of-the-pre-built-executable)
- Mix It Up is connected to your Twitch channel.

## Step 1: Creating a Chat Command

1. In Mix It Up, open the "Commands" section -> Custom commands.
2. Click "New Command" to create a new command.
3. Configure the command:
   - **Name**: any name
   - **Chat trigger**: track
4. Below **Action** -> Web Request and click the plus button.

## Step 2: Getting the Song (Web Request)

1. **Web Request URL**: http://127.0.0.1:5050
2. Below **Action** -> Chat Message and click the plus button.

## Step 3: Adding Response to Chat (Chat Message)

1. **Send as streamer**: check the box
2. **Chat message**: `/announce Current track: $webrequestresult`

## Step 4: Saving and Testing

1. Click **"Save"** to save the command. (Floppy disk icon)
2. Enable the command if there is a toggle.
3. Test: in the Twitch chat enter `!track`. Mix It Up should respond with the current track in the format "Current track: Artist - Title" or "Current track: Nothing is playing right now".

## Notes

- The command will only work if MediaTracker is running.
- If no response comes, check the Mix It Up logs for errors (Logs section).
- For more complex commands, you can add conditions, for example, checking if something is playing now, using the JSON endpoint (`http://127.0.0.1:5050/json`), but for simple text, GET / is sufficient.

## Usage Example

- Viewer writes: `!track`
- Bot responds: `Current track: The Beatles - Hey Jude`