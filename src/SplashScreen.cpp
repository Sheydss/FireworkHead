#include "SplashScreen.h"

TFT_eSPI* SplashScreen::s_tft    = nullptr;
int       SplashScreen::s_offsetX = 0;
int       SplashScreen::s_offsetY = 0;

// Static line buffer in internal SRAM — safe for TFT_eSPI DMA on ESP32-S3.
// 480 covers any ILI9488 canvas width in either orientation.
static uint16_t s_lineBuf[480];

// ─────────────────────────────────────────────────────────────────────────────

SplashScreen::SplashScreen(TFT_eSPI& tft)
    : m_tft(tft)
{}

// ─────────────────────────────────────────────────────────────────────────────

void SplashScreen::play(const uint8_t* data, uint32_t dataSize)
{
    s_tft = &m_tft;

    // GIF_PALETTE_RGB565_LE gives uint16_t values with R in the high bits —
    // the same format TFT_eSPI uses internally, so pushImage() handles them
    // correctly without any extra byte-swap.
    m_gif.begin(GIF_PALETTE_RGB565_LE);

    if (!m_gif.open((uint8_t*)data, dataSize, gifDraw))
    {
        Serial.printf("[Splash] open failed: %d\n", m_gif.getLastError());
        m_gif.close();
        s_tft = nullptr;
        return;
    }

    const int canvasW = m_gif.getCanvasWidth();
    const int canvasH = m_gif.getCanvasHeight();
    const int screenW = m_tft.width();
    const int screenH = m_tft.height();
    s_offsetX = max(0, (screenW - canvasW) / 2);
    s_offsetY = max(0, (screenH - canvasH) / 2);

    Serial.printf("[Splash] GIF=%dx%d  Screen=%dx%d  Offset=(%d,%d)\n",
                  canvasW, canvasH, screenW, screenH, s_offsetX, s_offsetY);

    m_tft.fillScreen(TFT_BLACK);

    // playFrame(false, nullptr) → ignore per-frame delay, play as fast as
    // the display allows.  Returns false when the GIF is finished.
    while (m_gif.playFrame(false, nullptr))
        yield();

    m_gif.close();
    s_tft     = nullptr;
    s_offsetX = 0;
    s_offsetY = 0;

    Serial.println("[Splash] Done");
}

// ─────────────────────────────────────────────────────────────────────────────
//  gifDraw  –  called by AnimatedGIF for each scan line of each frame.
//  Runs synchronously inside playFrame(), no PSRAM buffer needed.
// ─────────────────────────────────────────────────────────────────────────────

void SplashScreen::gifDraw(GIFDRAW* pDraw)
{
    if (!s_tft)
        return;

    const int      w         = min((int)pDraw->iWidth, 480);
    const uint8_t* src       = pDraw->pPixels;
    const uint8_t  transpIdx = pDraw->ucTransparent;

    for (int i = 0; i < w; ++i)
    {
        if (pDraw->ucHasTransparency && src[i] == transpIdx)
            s_lineBuf[i] = 0x0000; // transparent → black background
        else
            s_lineBuf[i] = pDraw->pPalette[src[i]]; // RGB565
    }

    const int x = s_offsetX + pDraw->iX;
    const int y = s_offsetY + pDraw->iY + pDraw->y;
    s_tft->pushImage(x, y, w, 1, s_lineBuf);
}
