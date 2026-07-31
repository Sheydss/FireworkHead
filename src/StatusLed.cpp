#include "StatusLed.h"

void StatusLed::begin()
{
    FastLED.addLeds<WS2811, PIN, RGB>(m_leds, NUM_LEDS);
    FastLED.setBrightness(BRIGHTNESS);
    fill_solid(m_leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
}

void StatusLed::setConnected(bool connected)
{
    m_connected    = connected;
    m_transferring = false;
    if (connected)
    {
        fill_solid(m_leds, NUM_LEDS, CRGB::Green);
        FastLED.show();
    }
}

void StatusLed::startTransfer()
{
    m_transferring = true;
    m_slidePos     = 0;
    m_lastSlideMs  = millis();
    fill_solid(m_leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
}

void StatusLed::endTransfer()
{
    m_transferring = false;
    if (m_connected)
    {
        fill_solid(m_leds, NUM_LEDS, CRGB::Green);
        FastLED.show();
    }
}

void StatusLed::renderSlide(fl::u8 R, fl::u8 G, fl::u8 B)
{
    fill_solid(m_leds, NUM_LEDS, CRGB::Black);
    for (int i = 0; i < NUM_LEDS; i++)
    {
        const int dist = m_slidePos - i;
        if (dist >= 0 && dist < COMET_LENGTH)
        {
            const uint8_t brightness = 255 - (dist * 255 / COMET_LENGTH);
            m_leds[i] = CRGB(R, G, B);
            m_leds[i].nscale8_video(brightness);
        }
    }
}

void StatusLed::update()
{
    const uint32_t now = millis();

    if (m_transferring)
    {
        if (now - m_lastSlideMs >= SLIDE_STEP_MS)
        {
            m_lastSlideMs = now;
            m_slidePos    = (m_slidePos + 1) % NUM_LEDS;
            renderSlide(255, 120, 0); // orange
            FastLED.show();
        }
        return;
    }

    if (m_connected)
        return;

    if (now - m_lastToggleMs >= BLINK_MS)
    {
        m_lastToggleMs = now;
        m_blinkOn      = !m_blinkOn;
        fill_solid(m_leds, NUM_LEDS, m_blinkOn ? CRGB::Red : CRGB::Black);
        FastLED.show();
    }
}
