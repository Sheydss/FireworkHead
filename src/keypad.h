#pragma once // directive processeur pour que le fichier ne soit compiler qu'une fois https://fr.wikipedia.org/wiki/Pragma_once

#include <TFT_eSPI.h>
#include <LittleFS.h>
#include <FS.h>

#define CALIBRATION_FILE "/TouchCalV"
#define REPEAT_CAL       false

// Screen
#define SCREEN_W   240
#define SCREEN_H   320
#define HEADER_H    80

// OK button
#define OK_X        40
#define OK_Y        40
#define OK_W        68
#define OK_H        52

// DEL button
#define DEL_X      200
#define DEL_Y       40
#define DEL_W       68
#define DEL_H       52

// Display box
#define DISP_X      74
#define DISP_Y       6
#define DISP_W      92
#define DISP_H      68

// Keypad geometry
#define PAD_TOP     (HEADER_H + 4)
#define KEY_COLS    3
#define KEY_ROWS    4
#define KEY_W       72
#define KEY_H       54
#define KEY_GAP_X    6
#define KEY_GAP_Y    6
#define PAD_LEFT     6

// Colours
#define COL_BG        0x1082
#define COL_KEY       0x2965
#define COL_KEY_PR    0x07FF
#define COL_OK        0x0600
#define COL_OK_PR     0x07E0
#define COL_DEL       0x6000
#define COL_DEL_PR    0xF800
#define COL_DISP_BG   0x0000
#define COL_DISP_BR   0x07FF
#define COL_NUM       0x07FF
#define COL_KEY_TXT   TFT_WHITE
#define COL_SHADOW    0x0841

// Fonts
#define FONT_KEY      &FreeSansBold12pt7b
#define FONT_DISP     &FreeSansBold18pt7b

#define NUM_LEN   3
#define NUM_KEYS  12
#define MISS_TOL   4

class Keypad {
public:
  Keypad(TFT_eSPI& tft);

  void begin();           // call once in setup()
  void update();          // call every loop()

  // Returns true when OK was pressed, and fills value with the entered number
  bool getConfirmedValue(int& value);

private:
  TFT_eSPI&        _tft;
  TFT_eSPI_Button  _keys[NUM_KEYS];

  char    _numBuf[NUM_LEN + 1];
  uint8_t _numIdx;

  // Touch state
  int  _missCount;
  bool _wasTouching;
  bool _actionDone;
  int  _pressedIdx;

  // Confirmed value state
  bool _confirmed;
  int  _confirmedValue;

  void _touchCalibrate();
  void _drawUI();
  void _redrawDisplay();
  void _appendDigit(char c);
  void _deleteDigit();
  void _confirmValue();
};