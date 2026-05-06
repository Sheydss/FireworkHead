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
static constexpr char CMD_HELLO[]      = "hello";    ///< Slave → Master on boot (carries MAC)
static constexpr char CMD_WELCOME[]    = "welcome";  ///< Master → Slave (carries assigned ID)
static constexpr char CMD_GOODBYE[]    = "goodbye";  ///< Slave → Master on clean shutdown

// File sync commands
static constexpr char CMD_SYNC_REQUEST[] = "sync_request"; ///< Slave → Master : ask for file list
static constexpr char CMD_SYNC_LIST[]    = "sync_list";    ///< Master → Slave : [{name, hash}, …]
static constexpr char CMD_SYNC_NEED[]    = "sync_need";    ///< Slave → Master : [filenames needed]

// ─────────────────────────────────────────────────────────────────────────────
//  CRC-16/CCITT-FALSE
// ─────────────────────────────────────────────────────────────────────────────

/// Compute CRC-16/CCITT-FALSE over @p len bytes starting at @p data.
uint16_t rs485ComputeCRC(const uint8_t* data, size_t len);

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