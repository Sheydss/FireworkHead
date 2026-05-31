#include "EspNowHead.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Static member definition
// ─────────────────────────────────────────────────────────────────────────────

EspNowHead* EspNowHead::s_instance = nullptr;

// Broadcast MAC — used for announce and status before bridge MAC is known
static const uint8_t k_broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────────────────

EspNowHead::EspNowHead(RS485Master& master)
    : m_master(master)
{}

// ─────────────────────────────────────────────────────────────────────────────
//  begin
// ─────────────────────────────────────────────────────────────────────────────

void EspNowHead::begin()
{
    s_instance = this;

    // ── WiFi — station mode required for ESP-NOW ──────────────────────────────
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    // Store own MAC
    strlcpy(m_mac, WiFi.macAddress().c_str(), sizeof(m_mac));
    Serial.printf("[EspNow] MAC=%s  channel=%u\n", m_mac, ESPNOW_CHANNEL);

    // ── ESP-NOW init ──────────────────────────────────────────────────────────
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("[EspNow] Init failed");
        return;
    }

    esp_now_register_send_cb(onSent);
    esp_now_register_recv_cb(onReceived);

    // Register broadcast peer so we can send before knowing bridge MAC
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

    // Broadcast — bridge learns our MAC from the ESP-NOW sender field
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
    doc["filename"] = m_master.getCurrentFileName();

    // TODO: add GPS coordinates here when GPS is implemented
    // doc["lat"] = gps.location.lat();
    // doc["lng"] = gps.location.lng();

    String json;
    serializeJson(doc, json);

    // Send directly to bridge if known, otherwise broadcast
    const uint8_t* dst = m_bridgeKnown ? m_bridgeMac : k_broadcast;
    esp_now_send(dst,
                 reinterpret_cast<const uint8_t*>(json.c_str()),
                 json.length());

    m_lastStatusMs = millis();
}

// ─────────────────────────────────────────────────────────────────────────────
//  handleReceivedFrame
// ─────────────────────────────────────────────────────────────────────────────

void EspNowHead::handleReceivedFrame(const uint8_t* mac,
                                      const uint8_t* data,
                                      int            len)
{
    // Learn bridge MAC on first receive
    ensureBridgePeer(mac);

    JsonDocument doc;
    if (deserializeJson(doc, data, len) != DeserializationError::Ok)
    {
        Serial.println("[EspNow] Received invalid JSON");
        return;
    }

    const char* cmd = doc["cmd"] | "";

    if (strcmp(cmd, "set_state") == 0)
    {
        const int stateVal = doc["state"] | static_cast<int>(NotConnected);

        // Validate — only accept known states
        if (stateVal < Preparation || stateVal > Fin)
        {
            Serial.printf("[EspNow] Invalid state value: %d\n", stateVal);
            return;
        }

        const State newState = static_cast<State>(stateVal);
        Serial.printf("[EspNow] State received: %d\n", stateVal);

        // Update filename first if provided (needed before Play)
        const char* fileName = doc["filename"] | "";
        if (strlen(fileName) > 0)
        {
            m_master.setCurrentFileName(String(fileName));
            Serial.printf("[EspNow] Filename: %s\n", fileName);
        }

        // Apply state — calls broadcastState() internally via RS485
        m_master.setCurrentState(newState);

        // Confirm new state to bridge immediately
        sendStatus();
    }
    else
    {
        Serial.printf("[EspNow] Unknown cmd: %s\n", cmd);
    }
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

void EspNowHead::onSent(const uint8_t* mac, esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS)
        Serial.println("[EspNow] Send failed");
}

void EspNowHead::onReceived(const uint8_t* mac, const uint8_t* data, int len)
{
    if (s_instance)
        s_instance->handleReceivedFrame(mac, data, len);
}