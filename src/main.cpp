#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include "HT_SSD1306Wire.h"
#include "json.h"
#include "RS485Master.h"
#include "FileSyncMaster.h"
#include "EspNowHead.h"
#include "SplashScreen.h"
#include "logo_gif.h"
#include "RelayView.h"
#include "TopSequencer.h"

TFT_eSPI     tft = TFT_eSPI();
SplashScreen splash(tft);
RelayView    relayView(tft);
TopSequencer sequencer;

static SSD1306Wire display(0x3C, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

RS485Master    master(Serial1, /*TX*/1, /*RX*/2, /*DE*/5, /*baud*/115200, /*heartbeat*/5000);
FileSyncMaster fileSync(master, "/plans");
EspNowHead     espNow(master, fileSync);

Top*     tops         = nullptr;
uint16_t topCount     = 0;
State    currentState = State::Preparation;

static constexpr uint8_t DETECT_PINS[RELAY_COUNT] = { 32, 33, 25, 23 };
static bool s_lastConnected[RELAY_COUNT]          = {};
static bool s_pendingConfigLoad                   = false;

// Shows a full-screen popup in portrait mode asking whether to restore the saved
// relay config. Returns true if the user taps YES or the 5-second countdown
// expires (auto-load); false if the user taps NO.
// Must be called after relayView.calibrateTouch() so touch coords are valid.
static bool showConfigPopup()
{
    // Portrait 320×480 — calibration is at rotation 0
    tft.fillScreen(TFT_NAVY);
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(&FreeSansBold18pt7b);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.drawString("Config found!", 160, 80);
    tft.drawString("Reload relay", 160, 150);
    tft.drawString("assignments?", 160, 200);

    tft.drawFastHLine(0, 360, 320, TFT_WHITE);

    // YES button — left half, ~1/4 screen height
    tft.fillRect(0, 361, 160, 119, TFT_DARKGREEN);
    tft.drawFastVLine(159, 361, 119, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.drawString("YES", 80, 420);

    // NO button — right half, ~1/4 screen height
    tft.fillRect(160, 361, 160, 119, 0xC000); // dark red
    tft.setTextColor(TFT_WHITE, 0xC000);
    tft.drawString("NO", 240, 420);

    const uint32_t start   = millis();
    uint32_t       lastSec = 99;

    while (true)
    {
        const uint32_t elapsed = millis() - start;
        if (elapsed >= 5000) return true; // auto-load

        const uint32_t remaining = (5000 - elapsed + 999) / 1000;
        if (remaining != lastSec)
        {
            lastSec = remaining;
            tft.fillRect(0, 300, 320, 30, TFT_NAVY);
            tft.setTextColor(TFT_YELLOW, TFT_NAVY);
            tft.drawString("Auto-load in " + String(remaining) + "s", 160, 315);
        }

        uint16_t tx = 0, ty = 0;
        if (tft.getTouch(&tx, &ty))
        {
            if (ty >= 361)
            {
                delay(50); // debounce
                return tx < 160; // YES = left, NO = right
            }
        }
        delay(20);
    }
}

void VextON() { pinMode(Vext, OUTPUT); digitalWrite(Vext, LOW); }

void setUpOled() {
    VextON();
    display.init();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
}

void displayOled(String text, String text2 = "") {
    display.clear();
    display.drawString(0, 0, text);
    if (text2 != "")
        display.drawString(0, 10, text2);
    display.display();
}

void checkIgniters()
{
    for (uint8_t i = 0; i < RELAY_COUNT; ++i)
    {
        // TODO: replace with digitalRead(DETECT_PINS[i]) == HIGH
        // when real hardware detection is ready
        const bool connected = (i == 0 || i == 1); // ← relay 0 always connected for testing

        if (connected != s_lastConnected[i])
        {
            s_lastConnected[i] = connected;
            relayView.setConnected(i, connected);
 
            Serial.printf("[App] Relay %u igniter %s\n",
                          i + 1, connected ? "connected" : "disconnected");
        }
    }
}

void drawState(State state) {
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(&FreeSansBold18pt7b);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    String stateStr = StateToString(state);
    tft.drawString(stateStr, 480 / 2, 20);
}

void loadTops(String fileName) {
    if (tops) { delete[] tops; tops = nullptr; topCount = 0; }

    JsonDocument doc;
    String path = "/plans/" + fileName;
    if (fileName != "" && LittleFS.exists(path) && Json::loadFile(path.c_str(), doc)) {
        JsonArray JsonTops = doc["tops"].as<JsonArray>();
        topCount = JsonTops.size();
        tops = new Top[topCount];
        uint16_t i = 0;
        for (JsonVariant v : JsonTops) {
            tops[i++] = Top(v["id"] | 0, v["timeInTenths"] | 0);
        }
        Serial.printf("[App] Loaded %u tops from '%s'\n", topCount, fileName.c_str());
    } else {
        Serial.printf("[App] Could not load tops from '%s'\n", fileName.c_str());
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    setUpOled();
    displayOled("Starting...");

    tft.init();
    tft.setRotation(0);  // portrait for touch calibration

    if (!LittleFS.begin(false)) {
        Serial.println("[FS] Mount failed, formatting...");
        displayOled("FS formatting...");
        LittleFS.format();
        if (!LittleFS.begin(true)) {
            Serial.println("[FS] Mount failed after format — halting");
            displayOled("FS error!");
            while (true) delay(1000);
        }
    }
    Serial.println("[FS] LittleFS mounted");

    if (!LittleFS.exists("/plans"))
        LittleFS.mkdir("/plans");

    relayView.calibrateTouch();
    if (LittleFS.exists(RELAY_CONFIG_PATH))
        s_pendingConfigLoad = showConfigPopup();

    //for (uint8_t i = 0; i < RELAY_COUNT; ++i)
    //    pinMode(DETECT_PINS[i], INPUT);

    // ── Splash screen ─────────────────────────────────────────────────────────
    tft.setRotation(1);  // landscape for splash
    splash.play(logo_gif, logo_gif_size);

    // ── RS485 master callbacks ────────────────────────────────────────────────
    master.registry().onNodeJoined([](const SlaveNode& node) {
        Serial.printf("[App] Node JOINED  id=0x%02X  MAC=%s\n", node.id, node.mac);
    });

    master.registry().onNodeLost([](const SlaveNode& node) {
        Serial.printf("[App] Node LOST    id=0x%02X  MAC=%s\n", node.id, node.mac);
    });

    master.onError([](uint8_t dst, const char* reason) {
        Serial.printf("[App] Error → 0x%02X : %s\n", dst, reason);
    });

    master.onStateChanged([]() {
        State s = master.getCurrentState();
        currentState = s;

        displayOled(StateToString(s), master.getCurrentFileName());

        switch (s) {
            case State::NotConnected:
                drawState(State::NotConnected);
                break;

            case State::Preparation:
                relayView.begin();
                if (s_pendingConfigLoad)
                {
                    relayView.loadConfig(RELAY_CONFIG_PATH);
                    s_pendingConfigLoad = false;
                }
                break;

            case State::Armement:
                drawState(State::Armement);
                break;

            case State::Play: {
                String fileName = master.getCurrentFileName();
                if (tops == nullptr)
                    loadTops(fileName);
                if (sequencer.isPaused())
                    sequencer.resume();
                else {
                    sequencer.load(tops, topCount);
                    sequencer.start();
                }
                drawState(State::Play);
                break;
            }

            case State::Pause:
                if (sequencer.isRunning())
                    sequencer.pause();
                drawState(State::Pause);
                break;

            case State::Stop:
                if (sequencer.isPaused() || sequencer.isRunning())
                    sequencer.stop();
                drawState(State::Stop);
                break;

            case State::Fin:
                if (sequencer.isPaused() || sequencer.isRunning())
                    sequencer.stop();
                drawState(State::Fin);
                break;
        }
    });

    // ── Sequencer fire callback ───────────────────────────────────────────────
    sequencer.onFire([](int id) {
        Serial.printf("[App] Fire top id=%d\n", id);
        tft.drawString("fire " + String(id), 480/2, 120);
        // TODO: trigger relay via I2C GPIO extender
    });

    master.begin();
    delay(1000);

    fileSync.begin();
    espNow.begin();

    // ── Jump forwarded from bridge → local sequencer ──────────────────────────
    espNow.onJump([](int timeInTenths) {
        Serial.println("main jump");
        sequencer.jumpTo(timeInTenths);
    });

    displayOled("Master Started");
    master.setCurrentState(State::Preparation);
}

void loop() {
    master.update();
    espNow.update();

    if (currentState == State::Preparation) {
        checkIgniters();
        relayView.update();
    }

    if (currentState == State::Play)
        sequencer.update();

    if (Serial.available() && Serial.read() == 'p') {
        Serial.println("[App] Pushing file list to all nodes");
        fileSync.pushToAll();
    }

    delay(1);
}
