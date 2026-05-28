#include <LittleFS.h>
#include <FS.h>
#include <ArduinoJson.h>

class Json {
    public: 
        bool static loadFile(const char* filePath, JsonDocument& doc);
    private:
};