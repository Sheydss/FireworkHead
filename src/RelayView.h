#pragma once

#include <TFT_eSPI.h>
#include <FS.h>
#include <LittleFS.h>
#include "RelayButton.h"
#include "Keypad.h"

#define RELAY_CONFIG_PATH "/relay_config.json"

// ─────────────────────────────────────────────────────────────────────────────
//  RelayView
//
//  Manages the full ILI9341 UI in portrait mode (rotation 0, 240×320).
//
//  Both the main view (2×2 relay buttons) and the keypad use the same
//  portrait orientation — no rotation switching needed.
//
//  Layout
//    Main view : 8 relay buttons arranged in a 2×4 grid (120×80 each)
//    Keypad    : full-screen numeric entry, opened on top when needed
//
//  Usage
//    RelayView view(tft);
//    view.begin();              // calibrate touch, draw main view
//    view.setConnected(0, true);
//
//    // In loop():
//    view.update();
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint8_t RELAY_COUNT = 8;

class RelayView
{
public:
    explicit RelayView(TFT_eSPI& tft);

    /// Initialise display, calibrate touch, draw main view.
    /// Call once in setup().
    void begin();

    /// Poll touch and handle state transitions.
    /// Call every loop().
    void update();

    /// Update igniter connection state for relay at @p index (0–3).
    void setConnected(uint8_t index, bool connected);

    /// Read-only access to a relay button (e.g. to read its shot number).
    const RelayButton& button(uint8_t index) const;

    /// Load touch calibration without drawing the relay UI.
    /// Call before using touch input prior to begin().
    void calibrateTouch();

    /// Persist all ASSIGNED relay→shot mappings to @p path (LittleFS JSON).
    void saveConfig(const char* path) const;

    /// Restore ASSIGNED relay→shot mappings from @p path.
    /// Returns false if the file is missing or corrupt.
    bool loadConfig(const char* path);

private:
    // ── Main view ─────────────────────────────────────────────────────────────
    void drawMainView();
    void handleMainTouch();

    // ── Keypad ────────────────────────────────────────────────────────────────
    void openKeypad (uint8_t relayIndex);
    void closeKeypad();
    void handleKeypadResult(int value);
    void syncOkButtonLabel();

    // ── Members ───────────────────────────────────────────────────────────────
    TFT_eSPI&   m_tft;
    Keypad      m_keypad;
    RelayButton m_buttons[RELAY_COUNT];

    bool    m_keypadOpen          = false;
    uint8_t m_activeRelay         = 0;
    bool    m_wasEmpty            = true;
    bool    m_wasTouching         = false;
    bool    m_waitForTouchRelease = false; ///< true until finger lifts after opening keypad
};