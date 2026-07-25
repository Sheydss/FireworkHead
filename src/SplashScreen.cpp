#include "SplashScreen.h"

// Static member definitions
TFT_eSPI*  SplashScreen::s_tft      = nullptr;
int        SplashScreen::s_offsetX   = 0;
int        SplashScreen::s_offsetY   = 0;
uint8_t*   SplashScreen::s_frameBuf  = nullptr;
int        SplashScreen::s_canvasW   = 0;
int        SplashScreen::s_canvasH   = 0;
int        SplashScreen::s_dirtyX0   = 0;
int        SplashScreen::s_dirtyY0   = 0;
int        SplashScreen::s_dirtyX1   = -1;
int        SplashScreen::s_dirtyY1   = -1;

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

    s_canvasW = m_gif.getCanvasWidth();
    s_canvasH = m_gif.getCanvasHeight();

    const int screenW = m_tft.width();
    const int screenH = m_tft.height();
    s_offsetX = max(0, (screenW - s_canvasW) / 2);
    s_offsetY = max(0, (screenH - s_canvasH) / 2);

    Serial.printf("[Splash] GIF=%dx%d  Screen=%dx%d  Offset=(%d,%d)\n",
                  s_canvasW, s_canvasH, screenW, screenH, s_offsetX, s_offsetY);

    // 3 bytes/pixel (RGB888) PSRAM frame buffer — gifDraw fills it,
    // one writeBytes() burst per frame instead of per-pixel spi.transfer().
    const size_t fbBytes = (size_t)s_canvasW * s_canvasH * 3;
    s_frameBuf = (uint8_t*)heap_caps_malloc(fbBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_frameBuf)
        s_frameBuf = (uint8_t*)malloc(fbBytes);
    if (s_frameBuf)
        memset(s_frameBuf, 0, fbBytes);

    Serial.printf("[Splash] Frame buf %s (%u bytes)\n",
                  s_frameBuf ? "OK" : "FAILED", (unsigned)fbBytes);

    m_tft.fillScreen(TFT_BLACK);
    m_tft.startWrite();

    while (true)
    {
        // Reset dirty rect — gifDraw will expand it for each scan line decoded
        s_dirtyX0 = s_canvasW;  s_dirtyY0 = s_canvasH;
        s_dirtyX1 = -1;          s_dirtyY1 = -1;

        if (!m_gif.playFrame(false, nullptr))
            break;

        // Only push the changed region — typically just the moving spark pixels
        if (s_frameBuf && s_dirtyX1 >= s_dirtyX0)
        {
            const int dw = s_dirtyX1 - s_dirtyX0 + 1;
            m_tft.setAddrWindow(s_offsetX + s_dirtyX0,
                                s_offsetY + s_dirtyY0,
                                dw,
                                s_dirtyY1 - s_dirtyY0 + 1);
            for (int r = s_dirtyY0; r <= s_dirtyY1; ++r)
            {
                const uint8_t* p = s_frameBuf + (r * s_canvasW + s_dirtyX0) * 3;
                m_tft.getSPIinstance().writeBytes((uint8_t*)p, dw * 3);
            }
        }
        yield();
    }

    m_tft.endWrite();
    m_gif.close();

    if (s_frameBuf) { free(s_frameBuf); s_frameBuf = nullptr; }
    s_tft     = nullptr;
    s_offsetX = 0;
    s_offsetY = 0;
    s_canvasW = 0;
    s_canvasH = 0;

    Serial.println("[Splash] Done");
}

// ─────────────────────────────────────────────────────────────────────────────
//  gifDraw  –  called for each scan line of each frame
// ─────────────────────────────────────────────────────────────────────────────

void SplashScreen::gifDraw(GIFDRAW* pDraw)
{
    if (!s_tft)
        return;

    if (!s_frameBuf)
        return;

    // Write this scan line into the PSRAM canvas buffer.
    // pDraw->iX/iY = frame position within the canvas.
    // pDraw->y     = current scan line within the frame.
    const int row = pDraw->iY + pDraw->y;
    if (row < 0 || row >= s_canvasH)
        return;

    const uint8_t* src       = pDraw->pPixels;
    const uint8_t  transpIdx = pDraw->ucTransparent;
    const int      startCol  = pDraw->iX;
    const int      w         = min((int)pDraw->iWidth, s_canvasW - startCol);
    uint8_t*       dst       = s_frameBuf + (row * s_canvasW + startCol) * 3;

    for (int i = 0; i < w; ++i, dst += 3)
    {
        if (pDraw->ucHasTransparency && src[i] == transpIdx)
            continue; // keep existing pixel — correct for delta frames
        const uint16_t c = pDraw->pPalette[src[i]];
        dst[0] = (c >> 8) & 0xF8; // R
        dst[1] = (c >> 3) & 0xFC; // G
        dst[2] = (c << 3) & 0xF8; // B
    }

    // Expand dirty rect to cover this scan line so only the changed region is
    // sent to the display after playFrame() finishes decoding the full frame.
    if (w > 0)
    {
        if (startCol     < s_dirtyX0) s_dirtyX0 = startCol;
        if (startCol+w-1 > s_dirtyX1) s_dirtyX1 = startCol + w - 1;
        if (row          < s_dirtyY0) s_dirtyY0 = row;
        if (row          > s_dirtyY1) s_dirtyY1 = row;
    }
}