#include "RS485Protocol.h"

uint16_t rs485ComputeCRC(const uint8_t* data, size_t len)
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