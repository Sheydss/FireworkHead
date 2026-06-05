#pragma once
 
#include <Arduino.h>
#include <ArduinoJson.h>
 
// ─────────────────────────────────────────────────────────────────────────────
//  Wire frame layout
//  [STX][DST][SRC][LEN_H][LEN_L][PAYLOAD … up to RS485_MAX_PAYLOAD][CRC_H][CRC_L][ETX]
// ─────────────────────────────────────────────────────────────────────────────
 
// Frame delimiters
static constexpr uint8_t RS485_STX = 0x02;
static constexpr uint8_t RS485_ETX = 0x03;
 
// Reserved addresses
static constexpr uint8_t RS485_MASTER_ADDR = 0x00;
static constexpr uint8_t RS485_BROADCAST   = 0xFF;
 
// Tuning
static constexpr uint16_t RS485_MAX_PAYLOAD  = 1024; ///< Maximum payload bytes per frame
static constexpr uint32_t RS485_TIMEOUT_MS   = 500;  ///< Default response timeout (ms)
static constexpr uint32_t RS485_DE_DELAY_US  = 50;   ///< DE settle time before TX (µs)
 
// JSON "cmd" field values
static constexpr char CMD_PING[]       = "ping";
static constexpr char CMD_PONG[]       = "pong";
static constexpr char CMD_ACK[]        = "ack";
static constexpr char CMD_NACK[]       = "nack";
static constexpr char CMD_FILE_START[] = "file_start";
static constexpr char CMD_FILE_CHUNK[] = "file_chunk";
static constexpr char CMD_FILE_END[]   = "file_end";
 
// Discovery commands
static constexpr char CMD_HELLO[]        = "hello";        ///< Slave → Master on boot (carries MAC)
static constexpr char CMD_WELCOME[]      = "welcome";      ///< Master → Slave (carries assigned ID)
static constexpr char CMD_GOODBYE[]      = "goodbye";      ///< Slave → Master on clean shutdown
 
// File sync commands
static constexpr char CMD_SYNC_REQUEST[] = "sync_request"; ///< Slave → Master : ask for file list
static constexpr char CMD_SYNC_LIST[]    = "sync_list";    ///< Master → Slave : [{name, hash}, …]
static constexpr char CMD_SYNC_NEED[]    = "sync_need";    ///< Slave → Master : [filenames needed]

static constexpr char CMD_SEND_STATE[] = "send_state";
static constexpr char CMD_SEND_FILENAME[] = "send_filename";
static constexpr char CMD_JUMP[] = "jump";

// ─────────────────────────────────────────────────────────────────────────────
//  CRC-16/CCITT-FALSE
//
//  Parameters (from Crc16 library / https://github.com/vinmenn/Crc16):
//    poly=0x1021  init=0xFFFF  refin=false  refout=false  xorout=0x0000
//
//  Standard verification: rs485ComputeCRC("123456789", 9) == 0x29B1
// ─────────────────────────────────────────────────────────────────────────────
 
inline uint16_t rs485ComputeCRC(const uint8_t* data, size_t len)
{
    static constexpr uint16_t POLY    = 0x1021;
    static constexpr uint16_t MSB     = 0x8000;
 
    uint16_t crc = 0xFFFF; // init
 
    for (size_t i = 0; i < len; ++i)
    {
        // XOR the next byte into the high byte of crc
        crc ^= static_cast<uint16_t>(data[i]) << 8;
 
        // Process each bit  (no reflection: refin=false, refout=false)
        for (uint8_t bit = 0; bit < 8; ++bit)
        {
            const bool topBitSet = (crc & MSB) != 0;
            crc = static_cast<uint16_t>(crc << 1);  // explicit cast kills promotion
            if (topBitSet)
                crc ^= POLY;
        }
    }
 
    return crc; // xorout=0x0000, so no final XOR needed
}
 
// ─────────────────────────────────────────────────────────────────────────────
//  Base-64 helpers
//
//  Shared by RS485Node (RS485 file transfer) and EspNowHead (ESP-NOW transfer).
//  Having them here avoids duplicating the implementation in both classes.
// ─────────────────────────────────────────────────────────────────────────────
 
inline String rs485Base64Encode(const uint8_t* data, size_t len)
{
    static const char k_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 
    String out;
    out.reserve(((len + 2) / 3) * 4 + 1);
 
    for (size_t i = 0; i < len; i += 3)
    {
        const uint32_t b =  (static_cast<uint32_t>(data[i])             << 16)
                          | (i + 1 < len ? static_cast<uint32_t>(data[i+1]) << 8 : 0)
                          | (i + 2 < len ? static_cast<uint32_t>(data[i+2])     : 0);
 
        out += k_chars[(b >> 18) & 0x3F];
        out += k_chars[(b >> 12) & 0x3F];
        out += (i + 1 < len) ? k_chars[(b >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? k_chars[ b        & 0x3F] : '=';
    }
    return out;
}
 
inline size_t rs485Base64Decode(const char* src, size_t srcLen,
                                 uint8_t*   dst, size_t dstMax)
{
    auto lookup = [](char c) -> uint8_t {
        if (c >= 'A' && c <= 'Z') return static_cast<uint8_t>(c - 'A');
        if (c >= 'a' && c <= 'z') return static_cast<uint8_t>(c - 'a' + 26);
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0' + 52);
        if (c == '+') return 62;
        if (c == '/') return 63;
        return 0xFF;
    };
 
    size_t out = 0;
    for (size_t i = 0; i + 4 <= srcLen && out < dstMax; i += 4)
    {
        const uint8_t a = lookup(src[i]);
        const uint8_t b = lookup(src[i + 1]);
        const uint8_t c = lookup(src[i + 2]);
        const uint8_t d = lookup(src[i + 3]);
        if (a == 0xFF || b == 0xFF) break;
        dst[out++] = static_cast<uint8_t>((a << 2) | (b >> 4));
        if (src[i + 2] != '=' && out < dstMax)
            dst[out++] = static_cast<uint8_t>(((b & 0x0F) << 4) | (c >> 2));
        if (src[i + 3] != '=' && out < dstMax)
            dst[out++] = static_cast<uint8_t>(((c & 0x03) << 6) | d);
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Parsed frame
// ─────────────────────────────────────────────────────────────────────────────
 
struct RS485Frame
{
    uint8_t  dst                        = 0;
    uint8_t  src                        = 0;
    uint8_t  payload[RS485_MAX_PAYLOAD] = {};
    uint16_t payloadLen                 = 0;
    bool     valid                      = false;
};
