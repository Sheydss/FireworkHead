#pragma once

#include <TFT_eSPI.h>
#include <AnimatedGIF.h>

class SplashScreen
{
public:
    explicit SplashScreen(TFT_eSPI& tft);

    /// Play the GIF once and block until complete.
    /// The display must already be initialised (tft.init() called).
    void play(const uint8_t* data, uint32_t dataSize);

private:
    TFT_eSPI&   m_tft;
    AnimatedGIF m_gif;

    // Shared with the static GIF draw callback
    static TFT_eSPI* s_tft;
    static int       s_offsetX;
    static int       s_offsetY;

    static void gifDraw(GIFDRAW* pDraw);
};
