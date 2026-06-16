#include "RelayView.h"

// Mirror OK button geometry from Keypad.h
static constexpr int16_t KP_OK_X = OK_X;
static constexpr int16_t KP_OK_Y = OK_Y;
static constexpr int16_t KP_OK_W = OK_W;
static constexpr int16_t KP_OK_H = OK_H;

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────────────────

RelayView::RelayView(TFT_eSPI& tft)
    : m_tft(tft)
    , m_keypad(tft)
    , m_buttons
      {
          RelayButton(0, 1),
          RelayButton(1, 2),
          RelayButton(2, 3),
          RelayButton(3, 4),
          RelayButton(4, 5),
          RelayButton(5, 6),
          RelayButton(6, 7),
          RelayButton(7, 8),

      }
{}

// ─────────────────────────────────────────────────────────────────────────────
//  begin
// ─────────────────────────────────────────────────────────────────────────────

void RelayView::begin()
{
    m_tft.init();
    m_tft.setRotation(0);

    // Calibrate touch without drawing the keypad UI — avoids the flash
    // that would occur if begin() drew the keypad before drawMainView()
    m_keypad.calibrateOnly();

    drawMainView();
}

// ─────────────────────────────────────────────────────────────────────────────
//  update
// ─────────────────────────────────────────────────────────────────────────────

void RelayView::update()
{
    if (m_keypadOpen)
    {
        // Wait until the finger that opened the keypad has been lifted.
        // Without this, the tap on the relay button is seen by the keypad
        // on the very next frame and can trigger an immediate confirmation.
        if (m_waitForTouchRelease)
        {
            uint16_t tx = 0, ty = 0;
            if (!m_tft.getTouch(&tx, &ty))
                m_waitForTouchRelease = false;
            return;
        }

        const bool nowEmpty = m_keypad.isEmpty();
        if (nowEmpty != m_wasEmpty)
        {
            syncOkButtonLabel();
            m_wasEmpty = nowEmpty;
        }

        m_keypad.update();

        int value = 0;
        if (m_keypad.getConfirmedValue(value))
            handleKeypadResult(value);
    }
    else
    {
        handleMainTouch();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  setConnected
// ─────────────────────────────────────────────────────────────────────────────

void RelayView::setConnected(uint8_t index, bool connected)
{
    if (index >= RELAY_COUNT)
        return;

    if (!m_keypadOpen)
    {
        m_buttons[index].setConnected(m_tft, connected);
    }
    else
    {
        // Update state silently while keypad is open
        if (!connected)
        {
            m_buttons[index].setConnected(m_tft, false);

            // Close keypad if the active relay was disconnected
            if (m_activeRelay == index)
                closeKeypad();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  button() accessor
// ─────────────────────────────────────────────────────────────────────────────

const RelayButton& RelayView::button(uint8_t index) const
{
    return m_buttons[index < RELAY_COUNT ? index : 0];
}

// ─────────────────────────────────────────────────────────────────────────────
//  drawMainView
// ─────────────────────────────────────────────────────────────────────────────

void RelayView::drawMainView()
{
    m_tft.fillScreen(TFT_BLACK);
    for (uint8_t i = 0; i < RELAY_COUNT; ++i)
        m_buttons[i].draw(m_tft);
}

// ─────────────────────────────────────────────────────────────────────────────
//  handleMainTouch
// ─────────────────────────────────────────────────────────────────────────────

void RelayView::handleMainTouch()
{
    uint16_t tx = 0, ty = 0;
    const bool pressed = m_tft.getTouch(&tx, &ty);

    if (pressed && !m_wasTouching)
    {
        m_wasTouching = true;
        Serial.printf("[Touch] tx=%u ty=%u\n", tx, ty);

        for (uint8_t i = 0; i < RELAY_COUNT; ++i)
        {
            if (!m_buttons[i].contains(tx, ty))
                continue;

            // DISCONNECTED → do nothing
            if (m_buttons[i].state() == RelayState::DISCONNECTED)
                break;

            // CONNECTED or ASSIGNED → open keypad
            openKeypad(i);
            break;
        }
    }
    else if (!pressed)
    {
        m_wasTouching = false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  openKeypad
// ─────────────────────────────────────────────────────────────────────────────

void RelayView::openKeypad(uint8_t relayIndex)
{
    m_activeRelay         = relayIndex;
    m_keypadOpen          = true;
    m_waitForTouchRelease = true;

    m_keypad.show();

    // If the relay already has an assigned shot number, pre-fill the buffer
    // so the user sees the current value and can edit or confirm it directly.
    const RelayButton& btn = m_buttons[relayIndex];
    if (btn.state() == RelayState::ASSIGNED && btn.shotNumber() > 0)
    {
        m_keypad.setValue(btn.shotNumber());
        m_wasEmpty = false; // buffer is not empty — show "OK" not "Cancel"
    }
    else
    {
        m_wasEmpty = true;  // buffer is empty — show "Cancel"
    }

    syncOkButtonLabel();
}

// ─────────────────────────────────────────────────────────────────────────────
//  closeKeypad
// ─────────────────────────────────────────────────────────────────────────────

void RelayView::closeKeypad()
{
    m_keypadOpen = false;
    drawMainView();
}

// ─────────────────────────────────────────────────────────────────────────────
//  handleKeypadResult
// ─────────────────────────────────────────────────────────────────────────────

void RelayView::handleKeypadResult(int value)
{
    if (value == 0)
    {
        // Empty OK = Cancel — restore previous state without changes
        closeKeypad();
        return;
    }

    m_buttons[m_activeRelay].setAssigned(m_tft, value);

    Serial.printf("[RelayView] Relay %u assigned to shot %d\n",
                  m_buttons[m_activeRelay].relayNumber(), value);

    closeKeypad();
}

// ─────────────────────────────────────────────────────────────────────────────
//  syncOkButtonLabel
// ─────────────────────────────────────────────────────────────────────────────

void RelayView::syncOkButtonLabel()
{
    const bool     empty     = m_keypad.isEmpty();
    const uint16_t bgColor   = empty ? COL_DEL   : COL_OK;
    const uint16_t textColor = empty ? TFT_WHITE : COL_KEY_TXT;
    const char*    label     = empty ? "X"  : "OK";

    m_tft.setFreeFont(FONT_KEY);
    m_tft.fillRoundRect(KP_OK_X - KP_OK_W / 2,
                        KP_OK_Y - KP_OK_H / 2,
                        KP_OK_W, KP_OK_H, 6, bgColor);
    m_tft.setTextDatum(MC_DATUM);
    m_tft.setTextColor(textColor, bgColor);
    m_tft.drawString(label, KP_OK_X, KP_OK_Y);
}