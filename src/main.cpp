#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
//#include "Keypad.h"
#include "json.h"
#include "HT_SSD1306Wire.h"
#include "RS485Master.h"
#include "FileSyncMaster.h"


#define useKeypad   false
#define BUTTON_PIN 0  // physical PROG button

TFT_eSPI tft = TFT_eSPI();
//Keypad   keypad(tft);
JsonDocument doc;
static SSD1306Wire display(0x3C, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED); 
RS485Master master(Serial1, /*TX*/1, /*RX*/2, /*DE*/5, /*baud*/115200, /*heartbeat*/5000);
FileSyncMaster fileSync(master, "/plans");

bool lastButtonState = HIGH;

void VextON() { pinMode(Vext, OUTPUT); digitalWrite(Vext, LOW); }

void setUpOled() {
  VextON();
  display.init();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.clear();
  display.drawString(0, 0, "set up");
  display.display();
}

void displayOled(String text, String text2 = "") {
  display.clear();
  display.drawString(0, 0, text);
  if(text2 != "")
    display.drawString(0, 10, text2);
  display.display();
}

void setup() {
  Serial.begin(115200);
  tft.begin();
  delay(2000);
  setUpOled();
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  displayOled("Master starting...");

  if (!LittleFS.begin(false)) // true = format if mount fails
  {
      Serial.println("[FS] Mount failed!");
      return;
  }
  Serial.println("[FS] LittleFS mounted");

  tft.setRotation(0);
  //if(useKeypad) keypad.begin();

  tft.drawString("Hello World", 0, 0);

  // ── Registry callbacks ────────────────────────────────────────────────────
  master.registry().onNodeJoined([](const SlaveNode& node)
  {
      Serial.printf("[App] Node JOINED  id=0x%02X  MAC=%s\n",
                    node.id, node.mac);
  });

  master.registry().onNodeLost([](const SlaveNode& node)
  {
      Serial.printf("[App] Node LOST    id=0x%02X  MAC=%s\n",
                    node.id, node.mac);
  });

  // ── Error callback ────────────────────────────────────────────────────────
  master.onError([](uint8_t dst, const char* reason)
  {
      Serial.printf("[App] Error → 0x%02X : %s\n", dst, reason);
  });

  master.begin();
  delay(1000);

  master.setCurrentFileName("Exemple TopTir.json");

  // sync.begin() must be called AFTER master.begin()
  fileSync.begin();

  // ── Example: write a test plan file so there is something to sync ─────────
  if (!LittleFS.exists("/plans/plan1.json"))
  {
      File f = LittleFS.open("/plans/plan1.json", "w", true);
      f.print(R"({"hash":"a3f1c9b2","plan_name":"Show 1","shots":[]})");
      f.close();
      Serial.println("[App] Created test plan1.json");
  }

  displayOled(String(master.getCurrentState()), master.getCurrentFileName());

} 

void loop() {

  // if(useKeypad) {
  //   keypad.update();

  //   int value;
  //   if (keypad.getConfirmedValue(value)) {
  //     Serial.printf("Confirmed: %d\n", value);
  //   }
  // }
  
  // --- Physical button ---
  bool currentButtonState = digitalRead(BUTTON_PIN);
  if (currentButtonState == LOW && lastButtonState == HIGH) {
    if(master.getCurrentState() == State::Fin) {
      master.setCurrentState(State::Preparation);
    }
    else {
      master.addToCurrentState(1);
    }
    displayOled(String(master.getCurrentState()), master.getCurrentFileName());
  }
  lastButtonState = currentButtonState;
  master.update();

  // ── Example: push a new plan to all nodes when 'p' is typed in Serial ─────
  if (Serial.available() && Serial.read() == 'p')
  {
      Serial.println("[App] Pushing file list to all nodes");
      fileSync.pushToAll();
  }

  delay(1);

}