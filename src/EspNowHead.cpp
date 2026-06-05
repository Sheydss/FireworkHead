#include "EspNowHead.h"
#include <LittleFS.h>
#include "RS485Master.h"
#include "FileSyncMaster.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Static members
// ─────────────────────────────────────────────────────────────────────────────

EspNowHead* EspNowHead::s_instance = nullptr;

static const uint8_t k_broadcast[6] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────────────────

EspNowHead::EspNowHead(RS485Master& master, FileSyncMaster& fileSync)
    : m_master(master)
    , m_fileSync(fileSync)
{}

// ─────────────────────────────────────────────────────────────────────────────
//  begin
// ─────────────────────────────────────────────────────────────────────────────

void EspNowHead::begin()
{
    s_instance = this;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    strlcpy(m_mac, WiFi.macAddress().c_str(), sizeof(m_mac));
    Serial.printf("[EspNow] MAC=%s  channel=%u\n", m_mac, ESPNOW_CHANNEL);

    if (esp_now_init() != ESP_OK)
    {
        Serial.println("[EspNow] Init failed");
        return;
    }

    esp_now_register_send_cb(onSent);
    esp_now_register_recv_cb(onReceived);

    if (!esp_now_is_peer_exist(k_broadcast))
    {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, k_broadcast, 6);
        peer.channel = ESPNOW_CHANNEL;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    sendAnnounce();
}

// ─────────────────────────────────────────────────────────────────────────────
//  update
// ─────────────────────────────────────────────────────────────────────────────

void EspNowHead::update()
{
    const uint32_t now = millis();

    if (now - m_lastAnnounceMs >= ANNOUNCE_INTERVAL_MS)
        sendAnnounce();

    if (now - m_lastStatusMs >= STATUS_INTERVAL_MS)
        sendStatus();
}

// ─────────────────────────────────────────────────────────────────────────────
//  sendAnnounce
// ─────────────────────────────────────────────────────────────────────────────

void EspNowHead::sendAnnounce()
{
    JsonDocument doc;
    doc["type"] = "announce";
    doc["mac"]  = m_mac;

    String json;
    serializeJson(doc, json);
    esp_now_send(k_broadcast,
                 reinterpret_cast<const uint8_t*>(json.c_str()),
                 json.length());

    m_lastAnnounceMs = millis();
    Serial.printf("[EspNow] Announced  MAC=%s\n", m_mac);
}

// ─────────────────────────────────────────────────────────────────────────────
//  sendStatus
// ─────────────────────────────────────────────────────────────────────────────

void EspNowHead::sendStatus()
{
    JsonDocument doc;
    doc["type"]  = "status";
    doc["mac"]   = m_mac;
    doc["state"] = static_cast<int>(m_master.getCurrentState());

    String json;
    serializeJson(doc, json);

    const uint8_t* dst = m_bridgeKnown ? m_bridgeMac : k_broadcast;
    esp_now_send(dst,
                 reinterpret_cast<const uint8_t*>(json.c_str()),
                 json.length());

    m_lastStatusMs = millis();
}

// ─────────────────────────────────────────────────────────────────────────────
//  sendFileList  –  send /plans/ contents to bridge
// ─────────────────────────────────────────────────────────────────────────────

void EspNowHead::sendFileList()
{
    JsonDocument doc;
    doc["type"]        = "file_list";
    doc["mac"]         = m_mac;
    doc["free_bytes"]  = LittleFS.totalBytes() - LittleFS.usedBytes();
    doc["total_bytes"] = LittleFS.totalBytes();

    JsonArray files = doc["files"].to<JsonArray>();

    File dir = LittleFS.open("/plans");
    if (dir && dir.isDirectory())
    {
        File entry = dir.openNextFile();
        while (entry)
        {
            const char* name = entry.name();
            const size_t nameLen = strlen(name);
            if (nameLen >= 5 && strcmp(name + nameLen - 5, ".json") == 0)
            {
                JsonObject f = files.add<JsonObject>();
                f["name"] = name;
                f["size"] = entry.size();
            }
            entry = dir.openNextFile();
        }
    }

    String json;
    serializeJson(doc, json);

    const uint8_t* dst = m_bridgeKnown ? m_bridgeMac : k_broadcast;
    esp_now_send(dst,
                 reinterpret_cast<const uint8_t*>(json.c_str()),
                 json.length());

    Serial.println("[EspNow] File list sent");
}

// ─────────────────────────────────────────────────────────────────────────────
//  handleReceivedFrame
// ─────────────────────────────────────────────────────────────────────────────

void EspNowHead::handleReceivedFrame(const uint8_t* mac,
                                      const uint8_t* data,
                                      int            len)
{
    ensureBridgePeer(mac);

    JsonDocument doc;
    if (deserializeJson(doc, data, len) != DeserializationError::Ok)
    {
        Serial.println("[EspNow] Invalid JSON");
        return;
    }

    const char* cmd = doc["cmd"] | "";

    // ── State change ──────────────────────────────────────────────────────────
    if (strcmp(cmd, "set_state") == 0)
    {
        const int stateVal = doc["state"] | -1;
        if (stateVal < Preparation || stateVal > Fin)
        {
            Serial.printf("[EspNow] Invalid state: %d\n", stateVal);
            return;
        }
        const char* fileName = doc["filename"] | "";
        if (strlen(fileName) > 0)
            m_master.setCurrentFileName(String(fileName));
        m_master.setCurrentState(static_cast<State>(stateVal));
        sendStatus();
    }

    else if(strcmp(cmd, "jump") == 0) {
        const int timeInTenth = doc["timeInTenth"] | 0;
        if(timeInTenth == 0) {
            Serial.printf("[EspNow] time in tenth not found");
            return;
        }
        m_master.Jump(timeInTenth);
    }

    // ── File list request ─────────────────────────────────────────────────────
    else if (strcmp(cmd, "get_files") == 0)
    {
        sendFileList();
    }

    // ── File transfer — start ─────────────────────────────────────────────────
    else if (strcmp(cmd, "file_start") == 0)
    {
        handleFileStart(doc);
    }

    // ── File transfer — chunk ─────────────────────────────────────────────────
    else if (strcmp(cmd, "file_chunk") == 0)
    {
        handleFileChunk(mac, doc);
    }

    // ── File transfer — end ───────────────────────────────────────────────────
    else if (strcmp(cmd, "file_end") == 0)
    {
        handleFileEnd(mac, doc);
    }

    // ── File delete ───────────────────────────────────────────────────────────
    else if (strcmp(cmd, "file_delete") == 0)
    {
        handleFileDelete(mac, doc);
    }

    else
    {
        Serial.printf("[EspNow] Unknown cmd: %s\n", cmd);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  File transfer handlers
// ─────────────────────────────────────────────────────────────────────────────

void EspNowHead::handleFileStart(JsonDocument& doc)
{
    m_fileTransfer.reset();
    strlcpy(m_fileTransfer.name, doc["name"] | "unknown",
            sizeof(m_fileTransfer.name));
    m_fileTransfer.totalSize   = doc["size"]   | 0;
    m_fileTransfer.totalChunks = doc["chunks"] | 0;

    if (m_fileTransfer.totalSize == 0)
    {
        sendAck("file_start_ack", false);
        return;
    }

    m_fileTransfer.buf = static_cast<uint8_t*>(
        malloc(m_fileTransfer.totalSize));
    if (!m_fileTransfer.buf)
    {
        Serial.println("[EspNow] No memory for file transfer");
        sendAck("file_start_ack", false);
        return;
    }

    m_fileTransfer.active    = true;
    m_fileTransfer.nextChunk = 0;
    m_fileTransfer.received  = 0;

    Serial.printf("[EspNow] File start: %s (%zu B, %u chunks)\n",
                  m_fileTransfer.name,
                  m_fileTransfer.totalSize,
                  m_fileTransfer.totalChunks);

    sendAck("file_start_ack", true);
}

void EspNowHead::handleFileChunk(const uint8_t* mac, JsonDocument& doc)
{
    if (!m_fileTransfer.active)
    {
        sendAckIdx("chunk_ack", 0, false);
        return;
    }

    const uint16_t idx   = doc["idx"] | 0xFFFF;
    const uint16_t rxCrc = doc["crc"] | 0;
    const char*    b64   = doc["data"] | "";

    // ← Add this: if we already processed this chunk, just re-ACK it
    if (idx < m_fileTransfer.nextChunk)
    {
        Serial.printf("[EspNow] Duplicate chunk %u — re-ACKing\n", idx);
        sendAckIdx("chunk_ack", idx, true);
        return;
    }

    Serial.printf("[EspNow] Chunk %u: b64_len=%u rxCrc=0x%04X\n",
                  idx, strlen(b64), rxCrc);

    uint8_t  tmp[256];
    size_t   chunkLen = rs485Base64Decode(b64, strlen(b64), tmp, sizeof(tmp));

    Serial.printf("[EspNow] Decoded %u bytes, calcCrc=0x%04X\n",
                  chunkLen, rs485ComputeCRC(tmp, chunkLen));
                  
    if (chunkLen == 0)
    {
        sendAckIdx("chunk_ack", idx, false);
        return;
    }

    // Verify CRC
    if (rs485ComputeCRC(tmp, chunkLen) != rxCrc)
    {
        Serial.printf("[EspNow] Chunk %u CRC mismatch\n", idx);
        sendAckIdx("chunk_ack", idx, false);
        return;
    }

    memcpy(m_fileTransfer.buf + m_fileTransfer.received, tmp, chunkLen);
    m_fileTransfer.received  += chunkLen;
    m_fileTransfer.nextChunk++;

    Serial.printf("[EspNow] Chunk %u/%u OK\n",
                  m_fileTransfer.nextChunk, m_fileTransfer.totalChunks);

    sendAckIdx("chunk_ack", idx, true);
}

void EspNowHead::handleFileEnd(const uint8_t* mac, JsonDocument& doc)
{
    if (!m_fileTransfer.active)
    {
        sendAck("file_end_ack", false);
        return;
    }

    const uint16_t rxCrc   = doc["total_crc"] | 0;
    const uint16_t calcCrc = rs485ComputeCRC(
        m_fileTransfer.buf, m_fileTransfer.received);
    
    Serial.printf("[EspNow] FileEnd: received=%zu expected=%zu\n",
              m_fileTransfer.received, m_fileTransfer.totalSize);

    if (calcCrc != rxCrc)
    {
        Serial.printf("[EspNow] File CRC mismatch calc=0x%04X rx=0x%04X\n",
                      calcCrc, rxCrc);
        m_fileTransfer.reset();
        sendAck("file_end_ack", false);
        return;
    }

    // Save to LittleFS /plans/
    char path[80];
    snprintf(path, sizeof(path), "/plans/%s", m_fileTransfer.name);

    File f = LittleFS.open(path, "w", true);
    if (!f)
    {
        Serial.printf("[EspNow] Cannot open %s for writing\n", path);
        m_fileTransfer.reset();
        sendAck("file_end_ack", false);
        return;
    }

    f.write(m_fileTransfer.buf, m_fileTransfer.received);
    f.close();

    Serial.printf("[EspNow] Saved %s (%zu B)\n",
                  path, m_fileTransfer.received);

    m_fileTransfer.reset();
    sendAck("file_end_ack", true);

    // Trigger RS485 sync to modules
    m_fileSync.pushToAll();

    // Send updated file list to bridge
    sendFileList();
}

void EspNowHead::handleFileDelete(const uint8_t* mac, JsonDocument& doc)
{
    const char* name = doc["name"] | "";
    if (strlen(name) == 0)
    {
        sendAck("delete_ack", false);
        return;
    }

    char path[80];
    snprintf(path, sizeof(path), "/plans/%s", name);

    if (!LittleFS.exists(path))
    {
        Serial.printf("[EspNow] File not found: %s\n", path);
        sendAck("delete_ack", false);
        return;
    }

    LittleFS.remove(path);
    Serial.printf("[EspNow] Deleted %s\n", path);

    sendAck("delete_ack", true);

    // Trigger RS485 sync to modules (will delete from modules too)
    m_fileSync.pushToAll();

    // Send updated file list
    sendFileList();
}

// ─────────────────────────────────────────────────────────────────────────────
//  ACK helpers
// ─────────────────────────────────────────────────────────────────────────────

void EspNowHead::sendAck(const char* type, bool success)
{
    JsonDocument doc;
    doc["type"]    = type;
    doc["mac"]     = m_mac;
    doc["success"] = success;

    String json;
    serializeJson(doc, json);
    const uint8_t* dst = m_bridgeKnown ? m_bridgeMac : k_broadcast;
    esp_now_send(dst,
                 reinterpret_cast<const uint8_t*>(json.c_str()),
                 json.length());
}

void EspNowHead::sendAckIdx(const char* type, uint16_t idx, bool success)
{
    JsonDocument doc;
    doc["type"]    = type;
    doc["mac"]     = m_mac;
    doc["idx"]     = idx;
    doc["success"] = success;

    String json;
    serializeJson(doc, json);
    const uint8_t* dst = m_bridgeKnown ? m_bridgeMac : k_broadcast;
    esp_now_send(dst,
                 reinterpret_cast<const uint8_t*>(json.c_str()),
                 json.length());
}

// ─────────────────────────────────────────────────────────────────────────────
//  ensureBridgePeer
// ─────────────────────────────────────────────────────────────────────────────

void EspNowHead::ensureBridgePeer(const uint8_t* mac)
{
    if (m_bridgeKnown && memcmp(m_bridgeMac, mac, 6) == 0)
        return;

    memcpy(m_bridgeMac, mac, 6);
    m_bridgeKnown = true;

    if (!esp_now_is_peer_exist(mac))
    {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac, 6);
        peer.channel = ESPNOW_CHANNEL;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.printf("[EspNow] Bridge learned  MAC=%s\n", macStr);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Static ESP-NOW callbacks
// ─────────────────────────────────────────────────────────────────────────────

void EspNowHead::onSent(const uint8_t*, esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS)
        Serial.println("[EspNow] Send failed");
}

void EspNowHead::onReceived(const uint8_t* mac, const uint8_t* data, int len)
{
    if (s_instance)
        s_instance->handleReceivedFrame(mac, data, len);
}