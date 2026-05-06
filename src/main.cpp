#include <Arduino.h>
//#include <TFT_eSPI.h>
//#include "Keypad.h"
#include "json.h"
#include "HT_SSD1306Wire.h"
#include "RS485Master.h"
#include <FS.h>
#include <LittleFS.h>
#include "FileSyncMaster.h"


#define useKeypad   false

//TFT_eSPI tft = TFT_eSPI();
//Keypad   keypad(tft);
JsonDocument doc;
static SSD1306Wire display(0x3C, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
//static constexpr uint8_t SLAVE_2 = 0x02;
 
RS485Master master(Serial1, /*TX*/48, /*RX*/47, /*DE*/5, /*baud*/115200, /*heartbeat*/5000);
FileSyncMaster fileSync(master, "/plans");

// Small test payload
static const uint8_t TEST_FILE[] = "Hello from Heltec! This is a test file.";

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

void displayOled(String text) {
  display.clear();
  display.drawString(0, 0, text);
  display.display();
}

void setup() {
  Serial.begin(115200);
  //tft.begin();
  delay(2000);
  setUpOled();

  displayOled("Master starting...");

  if (!LittleFS.begin(true)) // true = format if mount fails
  {
      Serial.println("[FS] Mount failed!");
      return;
  }
  Serial.println("[FS] LittleFS mounted");

  //tft.setRotation(0);
  //if(useKeypad) keypad.begin();

  // ── Registry callbacks ────────────────────────────────────────────────────
  master.registry().onNodeJoined([](const SlaveNode& node)
  {
      Serial.printf("[App] Node JOINED  id=0x%02X  MAC=%s\n",
                    node.id, node.mac);

      // Example: send a file as soon as a node comes online
      // master.sendFile(node.id, "config.bin", TEST_FILE,
      //                 sizeof(TEST_FILE) - 1);
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

  displayOled("Master started !");

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


  // // ── Ping both slaves ──────────────────────────────────────────────────────
  // for (const uint8_t addr : { SLAVE_1 })
  // {
  //     const int32_t rtt = master.ping(addr);
  //     if (rtt >= 0)
  //         Serial.printf("[Ping] slave 0x%02X → %d ms\n", addr, rtt);
  //     else
  //         Serial.printf("[Ping] slave 0x%02X → no response\n", addr);
  // }

  // // ── Custom command ────────────────────────────────────────────────────────
  // {
  //     JsonDocument doc;
  //     doc["cmd"]   = "set_led";
  //     doc["color"] = "blue";
  //     doc["blink"] = true;
  //     master.sendCommand(SLAVE_1, doc);
  // }

  // displayOled("File transfert starting ...");
  // // ── File transfer ─────────────────────────────────────────────────────────
  // master.sendFile(SLAVE_1,
  //                 "test.txt",
  //                 TEST_FILE,
  //                 sizeof(TEST_FILE) - 1, // exclude null terminator
  //                 64);                   // 64-byte chunks
  // displayOled("File transfert ended");
} 

void loop() {

  // if(useKeypad) {
  //   keypad.update();

  //   int value;
  //   if (keypad.getConfirmedValue(value)) {
  //     Serial.printf("Confirmed: %d\n", value);
  //   }
  // }
  
  master.update();

  // ── Example: push a new plan to all nodes when 'p' is typed in Serial ─────
  if (Serial.available() && Serial.read() == 'p')
  {
      Serial.println("[App] Pushing file list to all nodes");
      fileSync.pushToAll();
  }

  delay(1);

}