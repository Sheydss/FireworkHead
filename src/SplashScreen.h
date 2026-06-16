#pragma once

#include <TFT_eSPI.h>
#include <AnimatedGIF.h>

// ─────────────────────────────────────────────────────────────────────────────
//  SplashScreen
//
//  Plays a GIF stored in PROGMEM once at startup.
//
//  Usage
//    #include "logo_gif.h"
//    SplashScreen splash(tft);
//    splash.play(logo_gif, logo_gif_size);
// ─────────────────────────────────────────────────────────────────────────────

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

    // Static state shared with the draw callback
    static TFT_eSPI* s_tft;
    static int       s_offsetX;
    static int       s_offsetY;

    static void gifDraw(GIFDRAW* pDraw);
};