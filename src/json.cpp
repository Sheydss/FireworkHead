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