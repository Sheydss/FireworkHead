# Fireworks Head — CLAUDE.md

## Project overview

This is the firmware for the **Heltec LoRa 32 V4** (ESP32-S3) which acts as the
**master controller** for one fireworks launching line.

It sits between:
- The **Android app** (communicates via ESP-NOW through a WROOM bridge)
- The **WROOM modules** (communicates via RS485 half-duplex bus)

---

## Hardware

| Component | Details |
|---|---|
| Board | Heltec WiFi LoRa 32 V4 (ESP32-S3, 240MHz, 8MB Flash) |
| Display | ILI9341 TFT (touch, connected via SPI) |
| RS485 | MAX485 module |
| Storage | LittleFS on internal flash |

### Pin assignments

| Signal | GPIO | Notes |
|---|---|---|
| RS485 TX (DI) | 1 | Serial1 |
| RS485 RX (RO) | 2 | Serial1 |
| RS485 DE/RE | 5 | Direction control |
| TFT MOSI | 47 | SPI |
| TFT SCK | 48 | SPI |
| TFT CS | 6 | |
| TFT DC | 7 | |
| TFT RST | -1 | Tied to 3.3V |
| Touch CS | 4 | |
| GPS | 38-42 | Not yet implemented |

### Pins to avoid on Heltec V4 ESP32-S3

- GPIO 33–38 → internal Flash/SubSPI
- GPIO 39–42 → JTAG
- GPIO 43/44 → USB Serial (U0TXD/U0RXD)
- GPIO 19/20 → USB D-/D+
- GPIO 8–14 → LoRa radio
- GPIO 15–18 → OLED
- GPIO 36 → Vext_Ctrl (internal power)
- GPIO 0 → USER_SW (boot strapping)

---

## File structure

```
src/
├── main_master.cpp      # Entry point — setup() and loop()
├── RS485Protocol.h      # Frame format, CRC-16, base64, constants, State enum
├── RS485Node.h/.cpp     # Base class — UART + frame TX/RX
├── RS485Master.h/.cpp   # Master — discovery, heartbeat, commands, file transfer
├── NodeRegistry.h/.cpp  # Node table — MAC→ID mapping, liveness tracking
├── FileSyncMaster.h/.cpp# File sync — lists /plans/, responds to sync requests
├── EspNowHead.h/.cpp    # ESP-NOW — bridge communication, file receive, state relay
├── State.h              # State enum: NotConnected=-1, Preparation=0..Fin=5
└── top.h                # Top struct: id, timeInTenths, alert, separatorBefore
```

---

## Architecture

```
Android app (Flutter)
    ↓ USB Serial (JSON, newline-delimited)
WROOM Bridge
    ↓ ESP-NOW (channel 1, broadcast + unicast)
EspNowHead (this board)
    ├── setCurrentState()   → RS485Master::broadcastState()
    ├── setCurrentFileName()
    ├── File receive        → save to /plans/ → FileSyncMaster::pushToAll()
    └── sendFileList()      → bridge → app
RS485Master
    ↓ RS485 half-duplex bus (115200 baud)
WROOM Modules (up to 31, addresses 0x01–0x1F)
```

---

## RS485 Protocol

### Frame format
```
[STX 0x02][DST][SRC][LEN_H][LEN_L][JSON payload…][CRC_H][CRC_L][ETX 0x03]
```

### Addressing
- `0x00` = Master (Heltec)
- `0x01–0x1F` = Modules (assigned dynamically)
- `0xFE` = Unregistered (used by modules before welcome)
- `0xFF` = Broadcast

### CRC
CRC-16/CCITT-FALSE: poly=0x1021, init=0xFFFF. Verification: `rs485ComputeCRC("123456789", 9) == 0x29B1`

### Discovery flow
```
Module boots → sends hello {cmd:"hello", mac:"..."} from 0xFE
Master → registers MAC, assigns ID → sends welcome {cmd:"welcome", mac:"...", id:N} broadcast
Module → adopts ID, sends sync_request
Master → sends sync_list [{name, hash}...]
Module → sends sync_need [filenames]
Master → sends each missing file in chunks
```

### Module reconnection
If module receives no ping for `connectionTimeoutMs` (default 15s), it resets registration and re-sends hello.

---

## ESP-NOW Protocol

### Channel
Fixed channel 1. Must match bridge firmware.

### Messages head receives from bridge
```json
{"cmd":"set_state","state":2,"filename":"plan1.json"}
{"cmd":"get_files"}
{"cmd":"file_start","name":"plan1.json","size":1712,"chunks":14}
{"cmd":"file_chunk","idx":0,"data":"<base64>","crc":12345}
{"cmd":"file_end","name":"plan1.json","total_crc":19542}
{"cmd":"file_delete","name":"plan1.json"}
{"cmd":"jump","timeInTenths":150}
```

### Messages head sends to bridge
```json
{"type":"announce","mac":"AA:BB:CC:DD:EE:FF"}
{"type":"status","mac":"...","state":2}
{"type":"file_list","mac":"...","free_bytes":1234,"total_bytes":5678,"files":[...]}
{"type":"chunk_ack","mac":"...","idx":0,"success":true}
{"type":"file_start_ack","mac":"...","success":true}
{"type":"file_end_ack","mac":"...","success":true}
{"type":"delete_ack","mac":"...","success":true}
```

---

## File sync

- Plan files stored in `/plans/` on LittleFS
- Each JSON plan has: `name`, `tops`, `hash` (SHA-256), `audioFilePath`, `audioTotalDuration`
- Hash is used for comparison — not recomputed, just compared
- After any file add/delete on the head, `FileSyncMaster::pushToAll()` is called
  which triggers RS485 sync to all connected modules

---

## State machine

```
NotConnected(-1) → Preparation(0) → Armement(1) → Play(2)
                                                 ↕
                                              Pause(3)
                                                 ↓
                                              Stop(4) → Fin(5)
```

State changes arrive via ESP-NOW, are applied via `setCurrentState()`,
and broadcast to all RS485 modules via `broadcastState()`.

---

## Key decisions & known issues

- `readFrame()` debug prints (`[readFrame] RX byte:`) must be removed in production
  — they cause RS485 timeouts by slowing down the byte reading loop
- `base64Encode/Decode` are free inline functions in `RS485Protocol.h` —
  do NOT duplicate them in other classes
- `State.h` defines the State enum — include it everywhere, never redefine it
- ESP-NOW and RS485 coexist without conflict (different hardware)
- When sending `sendState` after `handleHello`, delay by ~500ms to avoid
  RS485 bus collision with the module's sync_request
- Touch calibration for ILI9341 stored in `/TouchCalV` on LittleFS —
  lost if LittleFS is formatted

---

## platformio.ini key settings

```ini
platform = espressif32@6.6.0
board = heltec_wifi_lora_32_V4
framework = arduino
upload_port = COM4
upload_speed = 115200
monitor_speed = 115200
build_flags =
    -D HELTEC_BOARD
    -D WIFI_LORA_32_V4
    -D ARDUINO_USB_MODE=1
    -D ARDUINO_USB_CDC_ON_BOOT=1
```