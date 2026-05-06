#include <json.h>
        
bool Json::loadFile(const char* filePath, JsonDocument& doc) {
  if (!LittleFS.begin(true)) {
    Serial.println("FS failed");
    return false;
  }

  fs::File f = LittleFS.open(filePath, "r");
  if (!f) {
    Serial.printf("Failed to open %s\n", filePath);
    return false;
  }

  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.printf("JSON parse error: %s\n", err.c_str());
    return false;
  }

  return true;
}

// reveive and send using uart
void receiveFile() {
  Serial1.begin(115200, SERIAL_8N1, 3, 1); // RX, TX
  if (!LittleFS.begin()) return;

  // Wait for size header
  while (Serial1.available() < 4) delay(10);
  uint32_t size;
  Serial1.readBytes((char*)&size, 4);

  // Receive and write file
  fs::File f = LittleFS.open("/config.json", "w");
  uint32_t received = 0;
  while (received < size) {
    if (Serial1.available()) {
      f.write(Serial1.read());
      received++;
    }
  }
  f.close();
  Serial.println("File received");
}

void sendFile() {
  fs::File f = LittleFS.open("/config.json", "r");
  if (!f) return;

  Serial1.begin(115200, SERIAL_8N1, 44, 43); // RX, TX
  
  // Send file size first so receiver knows when to stop
  uint32_t size = f.size();
  Serial1.write((uint8_t*)&size, 4);

  // Send file content
  while (f.available()) {
    Serial1.write(f.read());
  }
  f.close();
  Serial.println("File sent");
}