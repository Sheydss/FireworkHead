#pragma once

#include <TFT_eSPI.h>

// ─────────────────────────────────────────────────────────────────────────────
//  RelayButton
//
//  One relay channel button in a 2×4 portrait layout (240×320).
//
//  Each button occupies half the screen width and a quarter of the height:
//    index 0 → row 0, col 0  (  0,   0, 120, 80)
//    index 1 → row 0, col 1  (120,   0, 120, 80)
//    index 2 → row 1, col 0  (  0,  80, 120, 80)
//    index 3 → row 1, col 1  (120,  80, 120, 80)
//    index 4 → row 2, col 0  (  0, 160, 120, 80)
//    index 5 → row 2, col 1  (120, 160, 120, 80)
//    index 6 → row 3, col 0  (  0, 240, 120, 80)
//    index 7 → row 3, col 1  (120, 240, 120, 80)
//
//  States
//    DISCONNECTED  Red    — igniter absent, tap ignored
//    CONNECTED     Orange — igniter detected, no shot assigned
//    ASSIGNED      Green  — shot number confirmed
// ─────────────────────────────────────────────────────────────────────────────

// Portrait screen dimensions
static constexpr uint16_t MAIN_SCREEN_W = 240;
static constexpr uint16_t MAIN_SCREEN_H = 320;
static constexpr uint16_t BTN_COLS      = 2;
static constexpr uint16_t BTN_ROWS      = 4;
static constexpr uint16_t BTN_W         = MAIN_SCREEN_W / BTN_COLS; // 120
static constexpr uint16_t BTN_H         = MAIN_SCREEN_H / BTN_ROWS; //  80

// Button colours (RGB565)
static constexpr uint16_t BTN_COL_DISCONNECTED = 0xF800; // red
static constexpr uint16_t BTN_COL_CONNECTED    = 0xFD20; // orange
static constexpr uint16_t BTN_COL_ASSIGNED     = 0x07E0; // green
static constexpr uint16_t BTN_COL_TEXT         = TFT_WHITE;
static constexpr uint16_t BTN_COL_BORDER       = TFT_WHITE;

enum class RelayState : uint8_t
{
    DISCONNECTED = 0,
    CONNECTED    = 1,
    ASSIGNED     = 2,
};

class RelayButton
{
public:
    /// @param index       0–7, determines position in the 2×4 grid
    /// @param relayNumber 1-based relay number shown as label
    RelayButton(uint8_t index, uint8_t relayNumber);

    // ── State setters — each redraws the button ───────────────────────────────
    void setConnected   (TFT_eSPI& tft, bool connected);
    void setAssigned    (TFT_eSPI& tft, int  shotNumber);
    void clearAssignment(TFT_eSPI& tft);

    // ── Draw ──────────────────────────────────────────────────────────────────
    void draw(TFT_eSPI& tft) const;

    // ── Touch hit test ────────────────────────────────────────────────────────
    bool contains(uint16_t tx, uint16_t ty) const;

    // ── Getters ───────────────────────────────────────────────────────────────
    RelayState  state()       const { return m_state; }
    uint8_t     relayNumber() const { return m_relayNumber; }
    int         shotNumber()  const { return m_shotNumber; }
    uint8_t     index()       const { return m_index; }
    uint16_t    x()           const;
    uint16_t    y()           const;

private:
    void     drawBackground(TFT_eSPI& tft) const;
    void     drawLabel     (TFT_eSPI& tft) const;
    uint16_t currentColor  ()              const;

    uint8_t    m_index;
    uint8_t    m_relayNumber;
    RelayState m_state      = RelayState::DISCONNECTED;
    int        m_shotNumber = -1;
};