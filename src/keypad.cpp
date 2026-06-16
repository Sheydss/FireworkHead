#include "Keypad.h"

static const char* keyLabel[NUM_KEYS] = {
  "1","2","3",
  "4","5","6",
  "7","8","9",
  "0","","",
};

Keypad::Keypad(TFT_eSPI& tft)
  : _tft(tft),
    _numIdx(0),
    _missCount(0),
    _wasTouching(false),
    _actionDone(false),
    _pressedIdx(-1),
    _confirmed(false),
    _confirmedValue(0)
{
  memset(_numBuf, 0, sizeof(_numBuf));
}

void Keypad::begin() {
  _touchCalibrate();
  _drawUI();
}

void Keypad::calibrateOnly() {
  // Calibrate touch without drawing the keypad UI.
  // Use this when another view owns the screen at startup.
  _touchCalibrate();
}

void Keypad::show() {
  // Reset all state so the keypad starts completely fresh
  _numIdx        = 0;
  _numBuf[0]     = 0;
  _pressedIdx    = -1;
  _confirmed     = false;
  _confirmedValue = 0;
  _wasTouching   = false;
  _actionDone    = false;
  _missCount     = 0;

  // Redraw the UI — this calls initButton() on each key which resets
  // their internal pressed/lastPressed state
  _drawUI();

  // Explicitly flush all button press states.
  // TFT_eSPI_Button::press() sets _lastpress = _currpress then _currpress = v.
  // Calling it twice ensures both _lastpress and _currpress are false,
  // so justPressed() cannot fire on the first update() after opening.
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    _keys[i].press(false);
    _keys[i].press(false);
  }
}

void Keypad::setValue(int v) {
  // Pre-fill the buffer with an existing value.
  // Call after show() so _drawUI() has already run.
  char tmp[NUM_LEN + 1] = {};
  snprintf(tmp, sizeof(tmp), "%d", v);
  _numIdx = 0;
  for (uint8_t i = 0; tmp[i] != '\0' && _numIdx < NUM_LEN; ++i)
    _numBuf[_numIdx++] = tmp[i];
  _numBuf[_numIdx] = 0;
  _redrawDisplay();
}

void Keypad::update() {
  uint16_t tx = 0, ty = 0;
  bool pressed = _tft.getTouch(&tx, &ty);

  if (pressed) {
    _missCount = 0;
    for (uint8_t i = 0; i < NUM_KEYS; i++)
      _keys[i].press(_keys[i].contains(tx, ty));
    _wasTouching = true;
  } else {
    _missCount++;
    if (_wasTouching && _missCount >= MISS_TOL) {
      for (uint8_t i = 0; i < NUM_KEYS; i++) _keys[i].press(false);

      // Restore pressed button appearance
      if (_pressedIdx >= 0) {
        _tft.setFreeFont(FONT_KEY);
        if (_pressedIdx == 10) {
          _tft.fillRoundRect(OK_X - OK_W/2, OK_Y - OK_H/2, OK_W, OK_H, 6, COL_OK);
          _tft.setTextDatum(MC_DATUM);
          _tft.setTextColor(COL_KEY_TXT, COL_OK);
          _tft.drawString("OK", OK_X, OK_Y);
        } else if (_pressedIdx == 11) {
          _tft.fillRoundRect(DEL_X - DEL_W/2, DEL_Y - DEL_H/2, DEL_W, DEL_H, 6, COL_DEL);
          _tft.setTextDatum(MC_DATUM);
          _tft.setTextColor(COL_KEY_TXT, COL_DEL);
          _tft.drawString("<", DEL_X, DEL_Y);
        } else {
          _keys[_pressedIdx].drawButton(false);
        }
        _pressedIdx = -1;
      }

      _wasTouching = false;
      _actionDone  = false;
      _missCount   = 0;
    }
  }

  // Handle presses
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    _tft.setFreeFont(FONT_KEY);

    if (_keys[i].justPressed() && !_actionDone) {
      _actionDone = true;
      _pressedIdx = i;

      if (i == 10) {
        _tft.fillRoundRect(OK_X - OK_W/2, OK_Y - OK_H/2, OK_W, OK_H, 6, COL_OK_PR);
        _tft.setFreeFont(FONT_KEY);
        _tft.setTextDatum(MC_DATUM);
        _tft.setTextColor(TFT_BLACK, COL_OK_PR);
        _tft.drawString("OK", OK_X, OK_Y);
        _confirmValue();

      } else if (i == 11) {
        _tft.fillRoundRect(DEL_X - DEL_W/2, DEL_Y - DEL_H/2, DEL_W, DEL_H, 6, COL_DEL_PR);
        _tft.setFreeFont(FONT_KEY);
        _tft.setTextDatum(MC_DATUM);
        _tft.setTextColor(TFT_WHITE, COL_DEL_PR);
        _tft.drawString("<", DEL_X, DEL_Y);
        _deleteDigit();

      } else {
        _keys[i].drawButton(true);
        _appendDigit(keyLabel[i][0]);
      }
    }
  }
}

bool Keypad::getConfirmedValue(int& value) {
  if (_confirmed) {
    value      = _confirmedValue;
    _confirmed = false;   // reset so it only fires once
    return true;
  }
  return false;
}

// ── Private ───────────────────────────────────────────────────────────────────

void Keypad::_touchCalibrate() {
  uint16_t calData[5];
  uint8_t  calDataOK = 0;
  if (!LittleFS.begin()) { LittleFS.format(); LittleFS.begin(); }
  if (LittleFS.exists(CALIBRATION_FILE)) {
    if (REPEAT_CAL) { LittleFS.remove(CALIBRATION_FILE); }
    else {
      fs::File f = LittleFS.open(CALIBRATION_FILE, "r");
      if (f) { if (f.readBytes((char*)calData, 14) == 14) calDataOK = 1; f.close(); }
    }
  }
  if (calDataOK && !REPEAT_CAL) {
    _tft.setTouch(calData);
  } else {
    _tft.fillScreen(TFT_BLACK);
    _tft.setTextColor(TFT_WHITE, TFT_BLACK);
    _tft.setTextFont(2);
    _tft.println("Touch corners as indicated");
    _tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);
    _tft.setTextColor(TFT_GREEN, TFT_BLACK);
    _tft.println("Done!");
    fs::File f = LittleFS.open(CALIBRATION_FILE, "w");
    if (f) { f.write((const unsigned char*)calData, 14); f.close(); }
    delay(600);
  }
}

void Keypad::_redrawDisplay() {
  _tft.setFreeFont(FONT_DISP);
  _tft.setTextDatum(MR_DATUM);
  _tft.setTextColor(COL_NUM, COL_DISP_BG);
  _tft.setTextPadding(DISP_W - 10);
  const char* shown = (_numIdx == 0) ? "0" : _numBuf;
  _tft.drawString(shown, DISP_X + DISP_W - 6, DISP_Y + DISP_H / 2);
  _tft.setTextPadding(0);
}

void Keypad::_drawUI() {
  _tft.fillScreen(COL_BG);
  _tft.fillRect(0, 0, SCREEN_W, HEADER_H, 0x0841);
  _tft.fillRoundRect(DISP_X, DISP_Y, DISP_W, DISP_H, 6, COL_DISP_BG);
  _tft.drawRoundRect(DISP_X, DISP_Y, DISP_W, DISP_H, 6, COL_DISP_BR);
  _tft.drawFastHLine(0, HEADER_H, SCREEN_W, COL_DISP_BR);

  // OK button
  { char lbl[] = "OK";
    _keys[10].initButton(&_tft, OK_X, OK_Y, OK_W, OK_H,
      COL_OK, COL_OK, COL_KEY_TXT, lbl, 1);
    _tft.setFreeFont(FONT_KEY); _keys[10].drawButton(); }

  // DEL button
  { char lbl[] = "<";
    _keys[11].initButton(&_tft, DEL_X, DEL_Y, DEL_W, DEL_H,
      COL_DEL, COL_DEL, COL_KEY_TXT, lbl, 1);
    _tft.setFreeFont(FONT_KEY); _keys[11].drawButton(); }

  // Digit keys 1-9
  for (uint8_t row = 0; row < 3; row++) {
    for (uint8_t col = 0; col < KEY_COLS; col++) {
      uint8_t idx = row * KEY_COLS + col;
      int cx = PAD_LEFT + col * (KEY_W + KEY_GAP_X) + KEY_W / 2;
      int cy = PAD_TOP  + row * (KEY_H + KEY_GAP_Y) + KEY_H / 2;
      _tft.fillRoundRect(cx - KEY_W/2 + 2, cy - KEY_H/2 + 3, KEY_W, KEY_H, 8, COL_SHADOW);
      char lbl[4]; strncpy(lbl, keyLabel[idx], 3); lbl[3] = 0;
      _keys[idx].initButton(&_tft, cx, cy, KEY_W, KEY_H,
        COL_KEY, COL_KEY, COL_KEY_TXT, lbl, 1);
      _tft.setFreeFont(FONT_KEY); _keys[idx].drawButton();
    }
  }

  // Digit key 0
  { int cx = PAD_LEFT + 1 * (KEY_W + KEY_GAP_X) + KEY_W / 2;
    int cy = PAD_TOP  + 3 * (KEY_H + KEY_GAP_Y) + KEY_H / 2;
    _tft.fillRoundRect(cx - KEY_W/2 + 2, cy - KEY_H/2 + 3, KEY_W, KEY_H, 8, COL_SHADOW);
    char lbl[] = "0";
    _keys[9].initButton(&_tft, cx, cy, KEY_W, KEY_H,
      COL_KEY, COL_KEY, COL_KEY_TXT, lbl, 1);
    _tft.setFreeFont(FONT_KEY); _keys[9].drawButton(); }

  _redrawDisplay();
}

void Keypad::_appendDigit(char c) {
  if (_numIdx >= NUM_LEN) return;
  _numBuf[_numIdx++] = c;
  _numBuf[_numIdx]   = 0;
  _redrawDisplay();
}

void Keypad::_deleteDigit() {
  if (_numIdx > 0) { 
    _numBuf[--_numIdx] = 0; 
    _redrawDisplay(); 
  }
}

void Keypad::_confirmValue() {
  _confirmedValue = (_numIdx > 0) ? atoi(_numBuf) : 0; // équivalent de => if(_numIdx > 0) { _confirmedValue = atoi(_numBuf) } else { _confirmedValue = 0 }
  _confirmed      = true;
  // Flash display green as feedback
  _tft.drawRoundRect(DISP_X, DISP_Y, DISP_W, DISP_H, 6, TFT_GREEN);
  delay(80);
  _tft.drawRoundRect(DISP_X, DISP_Y, DISP_W, DISP_H, 6, COL_DISP_BR);
}