#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include "RS485Master.h"

// ─────────────────────────────────────────────────────────────────────────────
//  EspNowHead
//
//  Handles ESP-NOW communication between the Heltec head and the WROOM bridge.
//
//  Responsibilities:
//    - Announce itself to the bridge on boot and periodically
//    - Receive state changes from the bridge and apply them via RS485Master
//    - Send status updates back to the bridge
//
//  The bridge sends:
//    {"cmd":"set_state","state":2,"filename":"plan1.json"}
//
//  The head announces itself with:
//    {"type":"announce","mac":"AA:BB:CC:DD:EE:FF"}
//
//  The head sends status with:
//    {"type":"status","mac":"AA:BB:CC:DD:EE:FF","state":2}
//
//  Usage
//    EspNowHead espNow(master);
//    espNow.begin();
//
//    // In loop():
//    espNow.update();
// ─────────────────────────────────────────────────────────────────────────────

// Must match bridge firmware
static constexpr uint8_t  ESPNOW_CHANNEL       = 1;

// How often to re-announce to the bridge (ms)
static constexpr uint32_t ANNOUNCE_INTERVAL_MS = 10000;

// How often to send status updates (ms)
static constexpr uint32_t STATUS_INTERVAL_MS   = 2000;

class EspNowHead
{
public:
    /// @param master  Reference to the RS485Master instance
    explicit EspNowHead(RS485Master& master);

    /// Initialise WiFi and ESP-NOW, send first announce.
    /// Call once in setup() after master.begin().
    void begin();

    /// Re-announce and send status updates periodically.
    /// Call every loop().
    void update();

private:
    // ── ESP-NOW helpers ───────────────────────────────────────────────────────
    void sendAnnounce();
    void sendStatus();
    void handleReceivedFrame(const uint8_t* mac, const uint8_t* data, int len);
    void ensureBridgePeer   (const uint8_t* mac);

    // ── Static ESP-NOW callbacks ──────────────────────────────────────────────
    static void onSent    (const uint8_t* mac, esp_now_send_status_t status);
    static void onReceived(const uint8_t* mac, const uint8_t* data, int len);

    // ── Members ───────────────────────────────────────────────────────────────
    RS485Master& m_master;

    char    m_mac[18]       = {}; ///< Own MAC as "AA:BB:CC:DD:EE:FF"
    uint8_t m_bridgeMac[6]  = {}; ///< Bridge MAC (learned on first receive)
    bool    m_bridgeKnown   = false;

    uint32_t m_lastAnnounceMs = 0;
    uint32_t m_lastStatusMs   = 0;

    // Static pointer for callbacks — only one EspNowHead instance expected
    static EspNowHead* s_instance;
};