#include "RelayButton.h"

extern const GFXfont FreeSansBold18pt7b;

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────────────────

RelayButton::RelayButton(uint8_t index, uint8_t relayNumber)
    : m_index(index)
    , m_relayNumber(relayNumber)
{}

// ─────────────────────────────────────────────────────────────────────────────
//  Position helpers
//
//  Portrait 2×4 grid — 2 columns, 4 rows:
//    col = index % 2      (0 = left, 1 = right)
//    row = index / 2      (0–3 top to bottom)
// ─────────────────────────────────────────────────────────────────────────────

uint16_t RelayButton::x() const
{
    return (m_index % BTN_COLS) * BTN_W;
}

uint16_t RelayButton::y() const
{
    return (m_index / BTN_COLS) * BTN_H;
}

// ─────────────────────────────────────────────────────────────────────────────
//  State setters
// ─────────────────────────────────────────────────────────────────────────────

void RelayButton::setConnected(TFT_eSPI& tft, bool connected)
{
    if (connected)
    {
        if (m_state == RelayState::DISCONNECTED)
            m_state = RelayState::CONNECTED;
    }
    else
    {
        m_state      = RelayState::DISCONNECTED;
        m_shotNumber = -1;
    }
    draw(tft);
}

void RelayButton::setAssigned(TFT_eSPI& tft, int shotNumber)
{
    m_state      = RelayState::ASSIGNED;
    m_shotNumber = shotNumber;
    draw(tft);
}

void RelayButton::clearAssignment(TFT_eSPI& tft)
{
    m_state      = RelayState::CONNECTED;
    m_shotNumber = -1;
    draw(tft);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Draw
// ─────────────────────────────────────────────────────────────────────────────

void RelayButton::draw(TFT_eSPI& tft) const
{
    drawBackground(tft);
    drawLabel(tft);
}

void RelayButton::drawBackground(TFT_eSPI& tft) const
{
    tft.fillRect(x(), y(), BTN_W, BTN_H, currentColor());

    // Vertical divider — draw on right edge of left-column buttons
    if (m_index % BTN_COLS == 0)
        tft.drawFastVLine(x() + BTN_W - 1, y(), BTN_H, BTN_COL_BORDER);

    // Horizontal dividers — draw on bottom edge of all rows except the last
    if (m_index / BTN_COLS < BTN_ROWS - 1)
        tft.drawFastHLine(x(), y() + BTN_H - 1, BTN_W, BTN_COL_BORDER);
}

void RelayButton::drawLabel(TFT_eSPI& tft) const
{
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(BTN_COL_TEXT, currentColor());
    tft.setFreeFont(&FreeSansBold18pt7b);

    const uint16_t cx = x() + BTN_W / 2;
    const uint16_t cy = y() + BTN_H / 2;

    char label[8] = {};
    switch (m_state)
    {
        case RelayState::DISCONNECTED:
        case RelayState::CONNECTED:
            snprintf(label, sizeof(label), "%u", m_relayNumber);
            break;
        case RelayState::ASSIGNED:
            snprintf(label, sizeof(label), "%d", m_shotNumber);
            break;
    }

    tft.drawString(label, cx, cy);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Touch hit test
// ─────────────────────────────────────────────────────────────────────────────

bool RelayButton::contains(uint16_t tx, uint16_t ty) const
{
    return tx >= x() && tx < x() + BTN_W &&
           ty >= y() && ty < y() + BTN_H;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Colour helper
// ─────────────────────────────────────────────────────────────────────────────

uint16_t RelayButton::currentColor() const
{
    switch (m_state)
    {
        case RelayState::DISCONNECTED: return BTN_COL_DISCONNECTED;
        case RelayState::CONNECTED:    return BTN_COL_CONNECTED;
        case RelayState::ASSIGNED:     return BTN_COL_ASSIGNED;
    }
    return BTN_COL_DISCONNECTED;
}