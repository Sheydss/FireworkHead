#include "RS485Master.h"
#include <esp_mac.h>  // esp_read_mac()
#include <LittleFS.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor / lifecycle
// ─────────────────────────────────────────────────────────────────────────────

RS485Master::RS485Master(HardwareSerial& serial,
                         uint8_t         pinTx,
                         uint8_t         pinRx,
                         uint8_t         pinDE,
                         uint32_t        baud,
                         uint32_t        heartbeatIntervalMs)
    : RS485Node(serial, pinTx, pinRx, pinDE, baud)
    , m_heartbeatIntervalMs(heartbeatIntervalMs)
{}

void RS485Master::begin()
{
    RS485Node::begin();
    m_lastHeartbeatMs = millis();
    Serial.printf("[Master] RS485 ready  TX=%d  RX=%d  DE=%d  @%u baud"
                  "  heartbeat=%ums\n",
                  m_pinTx, m_pinRx, m_pinDE, m_baud, m_heartbeatIntervalMs);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Callback registration
// ─────────────────────────────────────────────────────────────────────────────

void RS485Master::onError  (OnError   callback) { m_onError   = callback; }
void RS485Master::onCommand(OnCommand callback) { m_onCommand = callback; }

void RS485Master::onStateChanged(OnStateChanged cb) { m_onStateChanged = cb; }

// ─────────────────────────────────────────────────────────────────────────────
//  Registry access
// ─────────────────────────────────────────────────────────────────────────────

NodeRegistry&       RS485Master::registry()       { return m_registry; }
const NodeRegistry& RS485Master::registry() const { return m_registry; }

// ─────────────────────────────────────────────────────────────────────────────
//  update()  –  called every loop()
// ─────────────────────────────────────────────────────────────────────────────

void RS485Master::update()
{
    // ── 1. Process any waiting incoming frame ─────────────────────────────────
    // 50 ms poll window: long enough to catch the STX of an arriving hello
    // frame, short enough not to block the heartbeat scheduler noticeably.
    RS485Frame frame;
    if (readFrame(frame, 50))
        dispatchFrame(frame);

    // ── 2. Periodic heartbeat ─────────────────────────────────────────────────
    if (m_heartbeatIntervalMs > 0 &&
        millis() - m_lastHeartbeatMs >= m_heartbeatIntervalMs)
    {
        runHeartbeat();
        m_lastHeartbeatMs = millis();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  runHeartbeat()  –  ping every active node
// ─────────────────────────────────────────────────────────────────────────────

void RS485Master::runHeartbeat()
{
    const SlaveNode* active[NODE_MAX_NODES];
    const uint8_t    count = m_registry.activeNodes(active, NODE_MAX_NODES);

    if (count == 0)
        return;

    Serial.printf("[Master] Heartbeat — pinging %u node(s)\n", count);

    for (uint8_t i = 0; i < count; ++i)
    {
        const uint8_t id  = active[i]->id;
        const int32_t rtt = ping(id);

        if (rtt >= 0)
        {
            m_registry.markSeen(id);
            Serial.printf("[Master] Node 0x%02X  alive  rtt=%dms\n", id, rtt);
        }
        else
        {
            m_registry.recordMissedPing(id);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  sendCommand
// ─────────────────────────────────────────────────────────────────────────────

bool RS485Master::sendCommand(uint8_t       dstAddr,
                               JsonDocument& doc,
                               uint32_t      timeoutMs)
{
    String json;
    serializeJson(doc, json);

    const bool sent = sendFrame(dstAddr,
                                RS485_MASTER_ADDR,
                                reinterpret_cast<const uint8_t*>(json.c_str()),
                                static_cast<uint16_t>(json.length()));
    if (!sent)
        return false;

    return waitResponse(dstAddr, timeoutMs);
}

bool RS485Master::sendCommand(uint8_t     dstAddr,
                               const char* cmdName,
                               uint32_t    timeoutMs)
{
    JsonDocument doc;
    doc["cmd"] = cmdName;
    return sendCommand(dstAddr, doc, timeoutMs);
}

// ─────────────────────────────────────────────────────────────────────────────
//  ping
// ─────────────────────────────────────────────────────────────────────────────

int32_t RS485Master::ping(uint8_t dstAddr)
{
    const uint32_t t0 = millis();
    const bool     ok = sendCommand(dstAddr, CMD_PING, RS485_TIMEOUT_MS);
    return ok ? static_cast<int32_t>(millis() - t0) : -1;
}

// state

State RS485Master::getCurrentState() {
    return m_currentState;
}

bool RS485Master::sendState(uint8_t dstAddr, State state) {
    Serial.printf("Sending State %d to %d\n", state, dstAddr);
    
    JsonDocument doc;
    doc["cmd"] = CMD_SEND_STATE;
    doc["state"] = state;
    if (state == State::Play)
    {
        if (m_currentFileName.isEmpty())
            return false;
        doc["filename"] = m_currentFileName;
    }
    
    const uint32_t t0 = millis();
    const bool ok = sendCommand(dstAddr, doc, RS485_TIMEOUT_MS);
    return ok;
}

void RS485Master::broadcastState(State state)
{
    JsonDocument doc;
    doc["cmd"]   = CMD_SEND_STATE;
    doc["state"] = static_cast<int>(state);

    if (state == State::Play)
    {
        if (m_currentFileName.isEmpty())
        {
            Serial.println("[Master] broadcastState: no file set for Play");
            return;
        }
        doc["filename"] = m_currentFileName;
    }

    String json;
    serializeJson(doc, json);

    // Send to broadcast address — no ACK expected, no timeout wait
    sendFrame(RS485_BROADCAST,
              RS485_MASTER_ADDR,
              reinterpret_cast<const uint8_t*>(json.c_str()),
              static_cast<uint16_t>(json.length()));

    Serial.printf("[Master] Broadcast state=%d to all nodes\n",
                  static_cast<int>(state));
}

State RS485Master::addToCurrentState(int addition) {
  int intState = m_currentState;
  if(intState + addition >= 0 && intState + addition <= 5) {
    m_currentState = static_cast<State>(intState + addition);
    broadcastState(m_currentState);
  }
  return m_currentState;
}

void RS485Master::setCurrentState(State state) {
  m_currentState = state;
  Serial.println("setCurrentState");
  broadcastState(m_currentState);
  if(m_onStateChanged)
            m_onStateChanged();
}

// fileName

String RS485Master::getCurrentFileName() {
    return m_currentFileName;
}

void RS485Master::setCurrentFileName(String fileName) {
    m_currentFileName = fileName;
}

// ─────────────────────────────────────────────────────────────────────────────
//  sendFile
// ─────────────────────────────────────────────────────────────────────────────

bool RS485Master::sendFile(uint8_t        dstAddr,
                            const char*    fileName,
                            const uint8_t* data,
                            size_t         dataLen,
                            size_t         chunkSize)
{
    const uint16_t totalChunks =
        static_cast<uint16_t>((dataLen + chunkSize - 1) / chunkSize);

    // ── 1. Handshake ──────────────────────────────────────────────────────────
    {
        JsonDocument doc;
        doc["cmd"]    = CMD_FILE_START;
        doc["name"]   = fileName;
        doc["size"]   = dataLen;
        doc["chunks"] = totalChunks;

        if (!sendCommand(dstAddr, doc))
        {
            reportError(dstAddr, "file_start rejected");
            return false;
        }
    }

    // ── 2. Chunks ─────────────────────────────────────────────────────────────
    for (uint16_t i = 0; i < totalChunks; ++i)
    {
        const size_t   offset    = i * chunkSize;
        const size_t   thisChunk = min(chunkSize, dataLen - offset);
        const String   b64       = base64Encode(data + offset, thisChunk);
        const uint16_t crc       = rs485ComputeCRC(data + offset, thisChunk);

        JsonDocument doc;
        doc["cmd"]  = CMD_FILE_CHUNK;
        doc["idx"]  = i;
        doc["data"] = b64;
        doc["crc"]  = crc;

        Serial.printf("[Master] Chunk %u/%u  (%zu B)\n",
                      i + 1, totalChunks, thisChunk);

        if (!sendCommand(dstAddr, doc, RS485_TIMEOUT_MS * 2))
        {
            reportError(dstAddr, "chunk rejected");
            return false;
        }
    }

    // ── 3. Finalise ───────────────────────────────────────────────────────────
    {
        JsonDocument doc;
        doc["cmd"]       = CMD_FILE_END;
        doc["name"]      = fileName;
        doc["total_crc"] = rs485ComputeCRC(data, dataLen);

        if (!sendCommand(dstAddr, doc))
        {
            reportError(dstAddr, "file_end rejected");
            return false;
        }
    }

    Serial.printf("[Master] File '%s' (%zu B) delivered to slave 0x%02X\n",
                  fileName, dataLen, dstAddr);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Private — frame handling
// ─────────────────────────────────────────────────────────────────────────────

void RS485Master::reportError(uint8_t dstAddr, const char* reason)
{
    Serial.printf("[Master] ERROR → slave 0x%02X : %s\n", dstAddr, reason);
    if (m_onError)
        m_onError(dstAddr, reason);
}

bool RS485Master::waitResponse(uint8_t expectedSrc, uint32_t timeoutMs)
{
    const uint32_t deadline = millis() + timeoutMs;

    while (millis() < deadline)
    {
        const uint32_t remaining = deadline - millis();
        RS485Frame frame;

        if (readFrame(frame, min(remaining, static_cast<uint32_t>(50))))
        {
            // Accept frames addressed to master from the expected slave
            if (frame.dst == RS485_MASTER_ADDR && frame.src == expectedSrc)
            {
                dispatchFrame(frame);
                return true;
            }

            // Also handle unsolicited hello/goodbye that arrive while waiting
            if (frame.dst == RS485_MASTER_ADDR || frame.dst == RS485_BROADCAST)
                dispatchFrame(frame);
        }
    }

    reportError(expectedSrc, "timeout");
    return false;
}

void RS485Master::dispatchFrame(const RS485Frame& frame)
{
    m_lastDoc.clear();
    const DeserializationError err = deserializeJson(
        m_lastDoc,
        reinterpret_cast<const char*>(frame.payload),
        frame.payloadLen);

    if (err)
    {
        Serial.printf("[Master] JSON parse error from 0x%02X : %s\n",
                      frame.src, err.c_str());
        return;
    }

    const char* cmd = m_lastDoc["cmd"] | "";

    // ── Route to discovery handlers first ─────────────────────────────────────
    if (strcmp(cmd, CMD_HELLO) == 0)
    {
        handleHello(frame, m_lastDoc);
        return;
    }

    if (strcmp(cmd, CMD_GOODBYE) == 0)
    {
        handleGoodbye(frame, m_lastDoc);
        return;
    }

    // ── All other frames update the last-seen timestamp ───────────────────────
    if (frame.src != NODE_UNREGISTERED_ADDR)
        m_registry.markSeen(frame.src);

    // ── Forward to application command handler ────────────────────────────────
    // ACK and NACK are protocol-level replies handled by waitResponse() —
    // they don't need to reach the application layer.
    if (strcmp(cmd, CMD_ACK)  != 0 &&
        strcmp(cmd, CMD_NACK) != 0 &&
        strcmp(cmd, CMD_PONG) != 0)
    {
        if (m_onCommand)
            m_onCommand(frame.src, cmd, m_lastDoc);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Private — discovery handlers
// ─────────────────────────────────────────────────────────────────────────────

void RS485Master::handleHello(const RS485Frame& frame, JsonDocument& doc)
{
    const char* mac = doc["mac"] | "";
    if (strlen(mac) == 0)
    {
        Serial.println("[Master] hello received with missing MAC — ignored");
        return;
    }

    // Register (or re-register) the node and get its assigned ID
    const uint8_t assignedId = m_registry.registerNode(mac);
    if (assignedId == 0)
    {
        Serial.printf("[Master] Registry full — rejecting hello from MAC=%s\n", mac);
        return;
    }

    // Reply on broadcast so all unregistered slaves receive it.
    // Each slave checks the MAC field and only accepts the welcome if it
    // matches its own MAC — this correctly handles simultaneous hellos.
    JsonDocument reply;
    reply["cmd"] = CMD_WELCOME;
    reply["mac"] = mac;         // ← slave uses this to self-identify
    reply["id"]  = assignedId;

    String json;
    serializeJson(reply, json);

    sendFrame(RS485_BROADCAST,
              RS485_MASTER_ADDR,
              reinterpret_cast<const uint8_t*>(json.c_str()),
              static_cast<uint16_t>(json.length()));

    Serial.printf("[Master] Welcomed MAC=%s  assigned id=0x%02X\n",
                  mac, assignedId);

    sendState(assignedId, m_currentState);
}

void RS485Master::handleGoodbye(const RS485Frame& frame, JsonDocument& doc)
{
    const SlaveNode* node = m_registry.find(frame.src);
    if (!node)
    {
        Serial.printf("[Master] goodbye from unknown id=0x%02X\n", frame.src);
        return;
    }

    Serial.printf("[Master] Node 0x%02X  MAC=%s  said goodbye\n",
                  frame.src, node->mac);

    // recordMissedPing to NODE_MAX_MISSED_PINGS immediately marks it lost
    for (uint8_t i = 0; i < NODE_MAX_MISSED_PINGS; ++i)
        m_registry.recordMissedPing(frame.src);
}