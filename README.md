# ⬡ PADEL SCORE — ESP32 Scoreboard

A self-contained padel tennis scoring system that runs on an ESP32. No internet required — the ESP32 creates its own WiFi hotspot and serves everything locally.

## How It Works

```
┌─────────────────────────────────────────────────┐
│                 ESP32 (Padel-Court-1)            │
│                                                   │
│  WiFi AP  ──►  Web Server  ──►  WebSocket Hub    │
│                  │     │              │            │
│              /display  /input      /ws             │
└──────────────────┼───────┼──────────┼─────────────┘
                   │       │          │
            ┌──────┘       │    ┌─────┘
            ▼              ▼    ▼
      ┌──────────┐   ┌──────────┐
      │  TV/HDMI  │   │  Phone   │ ◄─ Any number of phones
      │  Display  │   │  Input   │    can connect and score
      │           │   │          │
      │ Shows the │   │ Register │
      │ court &   │   │ teams,   │
      │ scores    │   │ tap to   │
      │           │   │ score    │
      └──────────┘   └──────────┘
```

## Hardware Needed

- **ESP32** dev board (any variant with WiFi — DevKit, WROVER, S3, C3, etc.)
- **TV/Monitor** connected to a device with a browser (Raspberry Pi, Fire Stick, Chromecast, or any PC)
- **Phone(s)** for scoring input

## Setup

### Option A: PlatformIO (Recommended)

1. Install [PlatformIO](https://platformio.org/)
2. Open this project folder
3. Upload firmware:
   ```bash
   pio run --target upload
   ```
4. Upload HTML files to LittleFS:
   ```bash
   pio run --target uploadfs
   ```

### Option B: Arduino IDE

1. Install ESP32 board support in Arduino IDE
2. Install these libraries via Library Manager:
   - **ESPAsyncWebServer** by me-no-dev
   - **AsyncTCP** by me-no-dev
   - **ArduinoJson** by Benoit Blanchon (v7+)
3. Install the [ESP32 LittleFS Upload Plugin](https://github.com/lorol/arduino-esp32littlefs-plugin)
4. Open `padel_esp32.ino`
5. Select your ESP32 board from Tools → Board
6. Upload the sketch (→ button)
7. Upload data files: Tools → ESP32 LittleFS Data Upload

## Usage

1. **Power on** the ESP32
2. **Connect TV** to WiFi network `Padel-Court-1` (no password)
3. Open `http://192.168.4.1/display` on the TV browser → shows the court with idle screen + QR code
4. **Connect phone** to `Padel-Court-1` WiFi
5. Open `http://192.168.4.1/input` on phone (or scan the QR code)
6. **Set up teams** — enter player names for both sides and tap "Start Match"
7. **Play!** — tap the amber or blue button to score points for that side
8. Anyone with a phone can connect and score — no restrictions

## Features

- **Full padel scoring**: 0/15/30/40/Deuce/Advantage, games, sets (best of 3), tiebreak at 6-6
- **Real-time sync**: All connected clients update instantly via WebSocket
- **Multiple scorers**: Any phone can score for either side
- **Edit Teams**: Update player names mid-match
- **Undo**: Reverse the last point
- **Reset**: Clear scores, keep teams
- **Finish Match**: End match and return to idle screen
- **Idle screen**: Shows QR code when no match is active
- **Auto-reconnect**: Phones reconnect automatically if connection drops
- **Late joiners**: If a match is in progress, new phones skip registration and go straight to scoring

## File Structure

```
padel_esp32/
├── padel_esp32.ino      # ESP32 firmware (WiFi, WebSocket, scoring logic)
├── platformio.ini       # PlatformIO build config
├── data/
│   ├── display.html     # TV display page (court visualization)
│   └── input.html       # Phone input page (registration + scoring)
└── README.md
```

## Configuration

Edit these constants in `padel_esp32.ino`:

```cpp
const char* WIFI_SSID     = "Padel-Court-1";  // WiFi network name
const char* WIFI_PASSWORD = "";                 // Empty = open network
const int   WIFI_CHANNEL  = 6;                  // WiFi channel (1-13)
const int   MAX_CLIENTS   = 8;                  // Max simultaneous connections
```

## WebSocket Protocol

Clients send text commands, server broadcasts JSON state:

| Command | Description |
|---------|-------------|
| `getState` | Request current state |
| `score:0:PlayerName` | Score point for left side |
| `score:1:PlayerName` | Score point for right side |
| `undo` | Undo last point |
| `reset` | Reset scores (keep teams) |
| `finish` | End match, return to idle |
| `startMatch:{json}` | Start match with team info |
| `editTeams:{json}` | Update team names |

## Troubleshooting

- **Can't connect to WiFi**: Make sure you're connecting to `Padel-Court-1`, not your regular WiFi
- **Page doesn't load**: Wait 5 seconds after ESP32 boot, then try `http://192.168.4.1/input`
- **Fonts don't load**: Fonts require internet. On an isolated ESP32 network, the browser will fall back to system fonts. This is normal and looks fine.
- **QR code doesn't show**: Same as fonts — needs internet for the JS library. A text fallback is shown instead.
- **WebSocket disconnects**: The phone will auto-reconnect. If persistent, check ESP32 serial monitor for errors.

## Memory Notes

The ESP32 has limited RAM. The undo stack is capped at 200 entries. For typical matches this is more than enough (~150 points max in a 3-set match).

Google Fonts and the QR code library are loaded from CDN, which requires the phone/TV to have internet access *in addition to* the ESP32 WiFi. If you're on an isolated network (no internet), the system still works perfectly — it just uses fallback system fonts and a text-based QR placeholder.
