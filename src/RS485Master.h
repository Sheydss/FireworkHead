#pragma once

#include "RS485Node.h"
#include "NodeRegistry.h"
#include "State.h"

#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
//  RS485Master  –  Heltec LoRa 32 V4
//
//  Wiring
//    MAX485 DI   ← GPIO 19  (TX / Serial1)
//    MAX485 RO   → GPIO 20  (RX / Serial1)
//    MAX485 DE+RE← GPIO 5   (direction)
//    VCC 3.3 V / GND
//
//  Usage
//    RS485Master master;
//    master.registry().onNodeJoined(…);
//    master.registry().onNodeLost(…);
//    master.onError(…);
//    master.begin();
//
//    master.sendCommand(0x01, doc);
//    master.sendFile(0x01, "fw.bin", data, size);
//    master.registry().dump();
//
//    // In loop():
//    master.update();   ← handles frames + periodic heartbeat
// ─────────────────────────────────────────────────────────────────────────────

class RS485Master : public RS485Node
{
public:
    // ── Callback signatures ───────────────────────────────────────────────────
    using OnError   = std::function<void(uint8_t dstAddr, const char* reason)>;

    /// Called for application-level commands not handled internally.
    /// (i.e. not ping/ack/nack/file_*/hello/welcome/goodbye)
    using OnCommand = std::function<void(uint8_t srcAddr,
                                         const char* cmd,
                                         JsonDocument& doc)>;

    using OnStateChanged = std::function<void()>;

    // ── Constructor ───────────────────────────────────────────────────────────

    /// @param heartbeatIntervalMs  How often to ping all active nodes (ms).
    ///                             Set to 0 to disable automatic heartbeat.
    explicit RS485Master(HardwareSerial& serial              = Serial1,
                         uint8_t         pinTx               = 17,
                         uint8_t         pinRx               = 18,
                         uint8_t         pinDE               = 16,
                         uint32_t        baud                = 115200,
                         uint32_t        heartbeatIntervalMs = 5000);

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /// Initialise GPIO and UART.  Call once in setup().
    void begin() override;

    /// Poll for incoming frames and run the heartbeat scheduler.
    /// Must be called every loop() iteration.
    void update();

    // ── Callback registration ─────────────────────────────────────────────────
    void onError  (OnError   callback);
    void onCommand(OnCommand callback);
    
    void onStateChanged(OnStateChanged callback);

    // ── Node registry (read/write access) ────────────────────────────────────

    /// Direct access to the node registry for registering callbacks and queries.
    NodeRegistry&       registry();
    const NodeRegistry& registry() const;

    // ── Communication API ─────────────────────────────────────────────────────

    /// Send a JSON document to @p dstAddr and block until ACK or timeout.
    /// @return true on acknowledged response.
    bool sendCommand(uint8_t       dstAddr,
                     JsonDocument& doc,
                     uint32_t      timeoutMs = RS485_TIMEOUT_MS);

    /// Convenience overload: send {"cmd": cmdName} and wait for ACK.
    bool sendCommand(uint8_t     dstAddr,
                     const char* cmdName,
                     uint32_t    timeoutMs = RS485_TIMEOUT_MS);

    /// Send a PING and return the round-trip time in ms, or -1 on timeout.
    int32_t ping(uint8_t dstAddr);

    State getCurrentState();
    void setCurrentState(State state);
    State addToCurrentState(int addition);
    bool sendState(uint8_t dstAddr, State state);
    //void sendAllState(State state);
    void broadcastState(State state);
    
    String getCurrentFileName();
    void setCurrentFileName(String fileName);

    bool Jump(int timeInTenths);

    /// Transfer a binary blob to @p dstAddr in CRC-verified chunks.
    /// @param chunkSize  Raw bytes per chunk before base64 encoding.
    /// @return true when the slave confirms the complete file.
    bool sendFile(uint8_t        dstAddr,
                  const char*    fileName,
                  const uint8_t* data,
                  size_t         dataLen,
                  size_t         chunkSize = 256);

    /// Run a heartbeat pass immediately (ping all active nodes).
    /// Normally called automatically by update() on the configured interval.
    void runHeartbeat();

private:
    // ── Frame handling ────────────────────────────────────────────────────────
    void reportError  (uint8_t dstAddr, const char* reason);
    bool waitResponse (uint8_t expectedSrc, uint32_t timeoutMs);
    void dispatchFrame(const RS485Frame& frame);

    // ── Discovery handlers ────────────────────────────────────────────────────

    /// Handle an incoming "hello" from an unregistered or reconnecting slave.
    void handleHello  (const RS485Frame& frame, JsonDocument& doc);

    /// Handle an incoming "goodbye" from a slave that is shutting down cleanly.
    void handleGoodbye(const RS485Frame& frame, JsonDocument& doc);

    // ── Members ───────────────────────────────────────────────────────────────
    NodeRegistry m_registry;
    OnError      m_onError;
    OnCommand    m_onCommand;
    OnStateChanged m_onStateChanged;
    JsonDocument m_lastDoc;
    State m_currentState = State::Preparation;
    String m_currentFileName = "";
    
    uint32_t m_heartbeatIntervalMs;
    uint32_t m_lastHeartbeatMs = 0;

    bool     m_pendingState     = false;
    uint8_t  m_pendingStateAddr = 0;
    uint32_t m_pendingStateMs   = 0;
};