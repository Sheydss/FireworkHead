#include "RS485Node.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Static look-up tables
// ─────────────────────────────────────────────────────────────────────────────

const char RS485Node::k_b64Chars[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor / begin
// ─────────────────────────────────────────────────────────────────────────────

RS485Node::RS485Node(HardwareSerial& serial,
                     uint8_t         pinTx,
                     uint8_t         pinRx,
                     uint8_t         pinDE,
                     uint32_t        baud)
    : m_serial(serial)
    , m_pinTx(pinTx)
    , m_pinRx(pinRx)
    , m_pinDE(pinDE)
    , m_baud(baud)
{}

void RS485Node::begin()
{
    // ── 1. Start UART first ───────────────────────────────────────────────────
    // end() ensures a clean state if begin() was ever called before.
    m_serial.end();
    delay(10);
    m_serial.begin(m_baud, SERIAL_8N1, m_pinRx, m_pinTx);
    delay(10);

    // ── 2. Configure DE pin AFTER UART is up ──────────────────────────────────
    // The ESP32 GPIO mux is reconfigured by Serial.begin() — setting DE before
    // that call can leave the pin in an undefined state.
    pinMode(m_pinDE, OUTPUT);
    digitalWrite(m_pinDE, LOW);  // idle = RX mode
}

// ─────────────────────────────────────────────────────────────────────────────
//  Direction control
// ─────────────────────────────────────────────────────────────────────────────

void RS485Node::setTxMode()
{
    digitalWrite(m_pinDE, HIGH);
    delayMicroseconds(RS485_DE_DELAY_US);
}

void RS485Node::setRxMode()
{
    // Wait for TX hardware buffer to drain before releasing the bus.
    // NOTE: flush() on ESP32 only drains TX — it does NOT clear RX,
    // but we call it before switching DE to avoid cutting a transmission short.
    m_serial.flush();

    digitalWrite(m_pinDE, LOW);

    // Discard any echo bytes we received of our own transmission.
    // On a shared RS485 bus the sender sees its own bytes reflected back.
    // We must drop exactly those bytes and nothing else — so we only flush
    // bytes that are already in the buffer RIGHT NOW, not future ones.
    const uint32_t drainDeadline = millis() + 5;
    while (m_serial.available() && millis() < drainDeadline)
        m_serial.read();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Frame TX
// ─────────────────────────────────────────────────────────────────────────────

bool RS485Node::sendFrame(uint8_t        dst,
                          uint8_t        src,
                          const uint8_t* payload,
                          uint16_t       payloadLen)
{
    if (payloadLen > RS485_MAX_PAYLOAD)
        return false;

    // Header: STX, dst, src, len_h, len_l
    uint8_t  buf[RS485_MAX_PAYLOAD + 8];
    uint16_t idx = 0;

    buf[idx++] = RS485_STX;
    buf[idx++] = dst;
    buf[idx++] = src;
    buf[idx++] = static_cast<uint8_t>((payloadLen >> 8) & 0xFF);
    buf[idx++] = static_cast<uint8_t>( payloadLen       & 0xFF);

    memcpy(buf + idx, payload, payloadLen);
    idx += payloadLen;

    // CRC covers bytes [1 … 4+payloadLen] (dst through last payload byte)
    const uint16_t crc = rs485ComputeCRC(buf + 1, 4 + payloadLen);
    buf[idx++] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    buf[idx++] = static_cast<uint8_t>( crc        & 0xFF);
    buf[idx++] = RS485_ETX;

    setTxMode();
    m_serial.write(buf, idx);
    m_serial.flush();           // wait for last byte to leave the UART TX register
    delayMicroseconds(100);     // let the bus line settle before releasing DE
    setRxMode();

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Frame RX
// ─────────────────────────────────────────────────────────────────────────────

bool RS485Node::readFrame(RS485Frame& frame, uint32_t timeoutMs)
{
    frame.valid = false;

    const uint32_t deadline = millis() + timeoutMs;

    // ── 1. Hunt for STX ──────────────────────────────────────────────────────
    bool stxFound = false;
    while (millis() < deadline)
    {
        if (!m_serial.available())
            continue;

        const uint8_t b = static_cast<uint8_t>(m_serial.read());
        //Serial.printf("[readFrame] RX byte: 0x%02X (STX=0x%02X)\n", b, RS485_STX);

        if (b == RS485_STX)
        {
            stxFound = true;
            break;
        }
    }

    if (!stxFound)
    {
        //Serial.println("[readFrame] STX not found — timeout");
        return false;
    }

    //Serial.println("[readFrame] STX found");

    const uint32_t innerTimeoutMs = max(timeoutMs, static_cast<uint32_t>(200));
    const uint32_t innerDeadline  = millis() + innerTimeoutMs;

    // ── 2. Read fixed header: [dst][src][len_h][len_l] ───────────────────────
    uint8_t header[4];
    if (!readBytes(header, sizeof(header), innerDeadline))
    {
        Serial.println("[readFrame] Header read timeout");
        return false;
    }

    //Serial.printf("[readFrame] Header: dst=0x%02X src=0x%02X len=%u\n",
                  //header[0], header[1],
                  //(static_cast<uint16_t>(header[2]) << 8) | header[3]);

    frame.dst        = header[0];
    frame.src        = header[1];
    frame.payloadLen = (static_cast<uint16_t>(header[2]) << 8) | header[3];

    if (frame.payloadLen > RS485_MAX_PAYLOAD)
    {
        Serial.printf("[readFrame] Payload too large: %u\n", frame.payloadLen);
        return false;
    }

    // ── 3. Read payload ───────────────────────────────────────────────────────
    if (!readBytes(frame.payload, frame.payloadLen, innerDeadline))
    {
        Serial.println("[readFrame] Payload read timeout");
        return false;
    }

    Serial.printf("[readFrame] Payload (%u B): %.*s\n",
                  frame.payloadLen, frame.payloadLen,
                  reinterpret_cast<const char*>(frame.payload));

    // ── 4. Read CRC (2 bytes) + ETX ───────────────────────────────────────────
    uint8_t tail[3];
    if (!readBytes(tail, sizeof(tail), innerDeadline))
    {
        Serial.println("[readFrame] Tail read timeout");
        return false;
    }

    if (tail[2] != RS485_ETX)
    {
        Serial.printf("[readFrame] Bad ETX: 0x%02X (expected 0x%02X)\n",
                      tail[2], RS485_ETX);
        return false;
    }

    const uint16_t rxCRC = (static_cast<uint16_t>(tail[0]) << 8) | tail[1];

    // ── 5. Verify CRC ─────────────────────────────────────────────────────────
    uint8_t crcBuf[RS485_MAX_PAYLOAD + 4];
    memcpy(crcBuf,     header,        4);
    memcpy(crcBuf + 4, frame.payload, frame.payloadLen);
    const uint16_t calcCRC = rs485ComputeCRC(crcBuf, 4 + frame.payloadLen);

    if (rxCRC != calcCRC)
    {
        Serial.printf("[readFrame] CRC mismatch: rx=0x%04X calc=0x%04X\n",
                      rxCRC, calcCRC);
        return false;
    }

    //Serial.println("[readFrame] Frame OK");
    frame.valid = true;
    return true;
}

bool RS485Node::readBytes(uint8_t* buf, size_t n, uint32_t deadline)
{
    for (size_t i = 0; i < n; ++i)
    {
        while (!m_serial.available())
        {
            if (millis() > deadline)
                return false;
        }
        buf[i] = static_cast<uint8_t>(m_serial.read());
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Base-64 helpers
// ─────────────────────────────────────────────────────────────────────────────

String RS485Node::base64Encode(const uint8_t* data, size_t len)
{
    String out;
    out.reserve(((len + 2) / 3) * 4 + 1);

    for (size_t i = 0; i < len; i += 3)
    {
        const uint32_t b =   (static_cast<uint32_t>(data[i])             << 16)
                           | (i + 1 < len ? static_cast<uint32_t>(data[i + 1]) << 8 : 0)
                           | (i + 2 < len ? static_cast<uint32_t>(data[i + 2])     : 0);

        out += k_b64Chars[(b >> 18) & 0x3F];
        out += k_b64Chars[(b >> 12) & 0x3F];
        out += (i + 1 < len) ? k_b64Chars[(b >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? k_b64Chars[ b        & 0x3F] : '=';
    }
    return out;
}

size_t RS485Node::base64Decode(const char* src,
                               size_t      srcLen,
                               uint8_t*    dst,
                               size_t      dstMax)
{
    // Helper lambda: look up one base64 character, return 0xFF if invalid
    auto lookup = [](char c) -> uint8_t {
        if (c >= 'A' && c <= 'Z') return static_cast<uint8_t>(c - 'A');
        if (c >= 'a' && c <= 'z') return static_cast<uint8_t>(c - 'a' + 26);
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0' + 52);
        if (c == '+') return 62;
        if (c == '/') return 63;
        return 0xFF; // padding '=' or invalid
    };

    size_t out = 0;

    // Process groups of 4 base64 characters → up to 3 decoded bytes
    //
    //  ┌── 6 bits ──┬── 6 bits ──┬── 6 bits ──┬── 6 bits ──┐
    //  │     a      │     b      │     c      │     d      │
    //  └─────┬──────┴──┬─────────┴──────┬─────┴────────────┘
    //        │  byte1  │    byte2       │    byte3         │
    //   [a7..a2|a1a0 b5..b0] [b3..b0 c5..c2] [c1c0 d5..d0]
    //
    //  byte1 = (a << 2) | (b >> 4)
    //  byte2 = ((b & 0x0F) << 4) | (c >> 2)
    //  byte3 = ((c & 0x03) << 6) | d

    for (size_t i = 0; i + 4 <= srcLen && out < dstMax; i += 4)
    {
        const uint8_t a = lookup(src[i]);
        const uint8_t b = lookup(src[i + 1]);
        const uint8_t c = lookup(src[i + 2]);
        const uint8_t d = lookup(src[i + 3]);

        if (a == 0xFF || b == 0xFF)
            break; // invalid input

        dst[out++] = static_cast<uint8_t>((a << 2) | (b >> 4));

        if (src[i + 2] != '=' && out < dstMax)
            dst[out++] = static_cast<uint8_t>(((b & 0x0F) << 4) | (c >> 2));

        if (src[i + 3] != '=' && out < dstMax)
            dst[out++] = static_cast<uint8_t>(((c & 0x03) << 6) | d);
    }
    return out;
}