#include "SplashScreen.h"

// Static member definitions
TFT_eSPI* SplashScreen::s_tft     = nullptr;
int       SplashScreen::s_offsetX  = 0;
int       SplashScreen::s_offsetY  = 0;

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────────────────

SplashScreen::SplashScreen(TFT_eSPI& tft)
    : m_tft(tft)
{}

// ─────────────────────────────────────────────────────────────────────────────
//  play
// ─────────────────────────────────────────────────────────────────────────────

void SplashScreen::play(const uint8_t* data, uint32_t dataSize)
{
    s_tft = &m_tft;

    // Try little-endian first — matches most ILI9341 setups.
    // If colors look wrong, change to GIF_PALETTE_RGB565_BE.
    m_gif.begin(GIF_PALETTE_RGB565_LE);

    if (!m_gif.open((uint8_t*)data, dataSize, gifDraw))
    {
        Serial.printf("[Splash] Failed to open GIF: %d\n", m_gif.getLastError());
        m_gif.close();
        s_tft = nullptr;
        return;
    }

    // Compute centering offset once from canvas size — NOT frame size.
    // Individual delta frames can be smaller than the canvas so using
    // frame size would cause the centering to shift between frames.
    const int gifW    = m_gif.getCanvasWidth();
    const int gifH    = m_gif.getCanvasHeight();
    const int screenW = m_tft.width();
    const int screenH = m_tft.height();

    s_offsetX = max(0, (screenW - gifW) / 2);
    s_offsetY = max(0, (screenH - gifH) / 2);

    Serial.printf("[Splash] GIF=%dx%d  Screen=%dx%d  Offset=(%d,%d)\n",
                  gifW, gifH, screenW, screenH, s_offsetX, s_offsetY);

    m_tft.fillScreen(TFT_BLACK);
    m_tft.startWrite(); // hold SPI bus for speed

    while (m_gif.playFrame(true, nullptr))
        yield();

    m_tft.endWrite();
    m_gif.close();
    s_tft     = nullptr;
    s_offsetX = 0;
    s_offsetY = 0;

    Serial.println("[Splash] Done");
}

// ─────────────────────────────────────────────────────────────────────────────
//  gifDraw  –  called for each scan line of each frame
// ─────────────────────────────────────────────────────────────────────────────

void SplashScreen::gifDraw(GIFDRAW* pDraw)
{
    if (!s_tft)
        return;

    // Position of this scan line on screen.
    // s_offsetX/Y are the canvas centering offsets computed once in play().
    // pDraw->iX/iY are the frame position within the canvas.
    // pDraw->y is the current scan line within the frame.
    const int x = s_offsetX + pDraw->iX;
    const int y = s_offsetY + pDraw->iY + pDraw->y;

    if (y < 0 || y >= s_tft->height())
        return;

    // Clamp width to screen boundary
    int w = pDraw->iWidth;
    if (x + w > s_tft->width())
        w = s_tft->width() - x;
    if (w <= 0)
        return;

    if (pDraw->ucHasTransparency)
    {
        // With palette-indexed pixels, transparent pixels must be skipped.
        // Build a line buffer replacing transparent entries with background.
        static uint16_t lineBuf[320];
        const uint8_t   transpIdx = pDraw->ucTransparent;
        const uint8_t*  src       = pDraw->pPixels;

        for (int i = 0; i < w; ++i)
        {
            if (src[i] == transpIdx)
            {
                lineBuf[i] = TFT_BLACK;
            }
            else
            {
                // Swap high and low bytes to convert RGB565 → BGR565
                const uint16_t c = pDraw->pPalette[src[i]];
                lineBuf[i] = (c >> 8) | (c << 8);
            }
        }
        s_tft->pushImage(x, y, w, 1, lineBuf);
    }
    else
    {
        // No transparency — swap bytes of every pixel in-place
        static uint16_t lineBuf[320];
        const uint16_t* src = reinterpret_cast<const uint16_t*>(pDraw->pPixels);
        for (int i = 0; i < w; ++i)
            lineBuf[i] = (src[i] >> 8) | (src[i] << 8);
        s_tft->pushImage(x, y, w, 1, lineBuf);
    }
}