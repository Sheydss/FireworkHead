#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include "RS485Master.h"
#include "FileSyncMaster.h"
#include "RS485Protocol.h"  // for rs485ComputeCRC

// ─────────────────────────────────────────────────────────────────────────────
//  EspNowHead
//
//  Handles ESP-NOW communication between the Heltec head and the WROOM bridge.
//
//  Responsibilities:
//    - Announce itself periodically
//    - Receive and apply state changes via RS485Master
//    - Receive files chunk by chunk, save to /plans/, trigger RS485 sync
//    - Respond to file list requests
//    - Delete files and trigger RS485 sync
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint8_t  ESPNOW_CHANNEL       = 1;
static constexpr uint32_t ANNOUNCE_INTERVAL_MS = 10000;
static constexpr uint32_t STATUS_INTERVAL_MS   = 2000;

class EspNowHead
{
public:
    /// @param master    RS485Master instance
    /// @param fileSync  FileSyncMaster instance (for triggering RS485 sync)
    EspNowHead(RS485Master& master, FileSyncMaster& fileSync);

    void begin();
    void update();

private:
    // ── Outgoing ──────────────────────────────────────────────────────────────
    void sendAnnounce();
    void sendStatus();
    void sendFileList();
    void sendAck   (const char* type, bool success);
    void sendAckIdx(const char* type, uint16_t idx, bool success);

    // ── Incoming ──────────────────────────────────────────────────────────────
    void handleReceivedFrame(const uint8_t* mac,
                             const uint8_t* data, int len);
    void handleFileStart (JsonDocument& doc);
    void handleFileChunk (const uint8_t* mac, JsonDocument& doc);
    void handleFileEnd   (const uint8_t* mac, JsonDocument& doc);
    void handleFileDelete(const uint8_t* mac, JsonDocument& doc);

    // ── Helpers ───────────────────────────────────────────────────────────────
    void   ensureBridgePeer(const uint8_t* mac);
    // base64 and CRC use shared free functions from RS485Protocol.h

    // ── Static callbacks ──────────────────────────────────────────────────────
    static void onSent    (const uint8_t* mac, esp_now_send_status_t status);
    static void onReceived(const uint8_t* mac, const uint8_t* data, int len);

    // ── File transfer state ───────────────────────────────────────────────────
    struct FileTransferState
    {
        bool     active      = false;
        char     name[64]    = {};
        size_t   totalSize   = 0;
        uint16_t totalChunks = 0;
        uint16_t nextChunk   = 0;
        uint8_t* buf         = nullptr;
        size_t   received    = 0;

        void reset()
        {
            active = false;
            name[0] = '\0';
            totalSize = received = nextChunk = totalChunks = 0;
            if (buf) { free(buf); buf = nullptr; }
        }
    };

    // ── Members ───────────────────────────────────────────────────────────────
    RS485Master&    m_master;
    FileSyncMaster& m_fileSync;

    char     m_mac[18]      = {};
    uint8_t  m_bridgeMac[6] = {};
    bool     m_bridgeKnown  = false;

    uint32_t m_lastAnnounceMs = 0;
    uint32_t m_lastStatusMs   = 0;

    FileTransferState m_fileTransfer;

    static EspNowHead* s_instance;
};