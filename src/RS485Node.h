#pragma once

#include "RS485Protocol.h"

// ─────────────────────────────────────────────────────────────────────────────
//  RS485Node
//
//  Abstract base class that owns the HardwareSerial port, the DE/RE direction
//  pin, and all low-level frame serialisation / deserialisation logic.
//
//  Both RS485Master and RS485Slave derive from this class.
// ─────────────────────────────────────────────────────────────────────────────

class RS485Node
{
public:
    /// @param serial   HardwareSerial instance wired to the MAX485
    /// @param pinTx    ESP32 TX GPIO  → MAX485 DI
    /// @param pinRx    ESP32 RX GPIO  ← MAX485 RO
    /// @param pinDE    Direction-enable GPIO → MAX485 DE+RE (HIGH = TX, LOW = RX)
    /// @param baud     UART baud rate (default 115 200)
    RS485Node(HardwareSerial& serial,
              uint8_t         pinTx,
              uint8_t         pinRx,
              uint8_t         pinDE,
              uint32_t        baud = 115200);

    /// Destructor — virtual so derived classes are cleaned up correctly.
    virtual ~RS485Node() = default;

    // Non-copyable — owns a hardware resource
    RS485Node(const RS485Node&)            = delete;
    RS485Node& operator=(const RS485Node&) = delete;

    /// Initialise GPIO and UART.  Must be called once in setup().
    virtual void begin();

protected:
    // ── Direction control ─────────────────────────────────────────────────────
    void setTxMode();
    void setRxMode();

    // ── Low-level frame I/O ───────────────────────────────────────────────────

    /// Serialise and transmit one frame.
    /// @return false if @p payloadLen exceeds RS485_MAX_PAYLOAD.
    bool sendFrame(uint8_t        dst,
                   uint8_t        src,
                   const uint8_t* payload,
                   uint16_t       payloadLen);

    /// Attempt to receive one complete, CRC-validated frame.
    /// Blocks for at most @p timeoutMs milliseconds.
    /// @return true when a valid frame has been written into @p frame.
    bool readFrame(RS485Frame& frame, uint32_t timeoutMs);

    // ── Base-64 helpers (shared by master encoder and slave decoder) ──────────
    static String  base64Encode(const uint8_t* data, size_t len);
    static size_t  base64Decode(const char*    src,
                                size_t         srcLen,
                                uint8_t*       dst,
                                size_t         dstMax);

    // ── Members ───────────────────────────────────────────────────────────────
    HardwareSerial& m_serial;
    uint8_t         m_pinTx;
    uint8_t         m_pinRx;
    uint8_t         m_pinDE;
    uint32_t        m_baud;

private:
    /// Read exactly @p n bytes before @p deadline (ms).
    bool readBytes(uint8_t* buf, size_t n, uint32_t deadline);

    static const char k_b64Chars[65]; ///< Encoding alphabet
};