// W25N01GV.h
#ifndef W25N01GV_H
#define W25N01GV_H
#define CMD_BLOCK_ERASE  0xD8

#include <Arduino.h>
#include <SPI.h>

class W25N01GV {
public:
    W25N01GV(uint8_t csPin);

    bool begin();
    void reset();
    uint8_t readStatus();
    bool writeEnable();
    bool isReady();
    bool readPage(uint16_t page, uint8_t* buffer, uint16_t length);
    bool blockErase(uint16_t page);
    bool programLoad(uint16_t column, const uint8_t* data, uint16_t length);
    bool programExecute(uint16_t page);
    bool setECCEnabled(bool enabled);
    bool clearBlockProtection();
    uint8_t readConfiguration();
    uint16_t findUsablePage(W25N01GV &flash, uint16_t startPage = 100);
    bool isPageUsable(W25N01GV &flash, uint16_t page);
    void setSPIFrequency(uint32_t hz);
    bool readSpareArea(uint16_t page, uint8_t* buffer, uint16_t length);



private:
    uint8_t _cs;
    SPISettings _spiSettings;
    void select();
    void deselect();
    uint8_t transfer(uint8_t data);

    static const uint8_t CMD_RESET = 0xFF;
    static const uint8_t CMD_GET_FEATURE = 0x0F;
    static const uint8_t CMD_SET_FEATURE = 0x1F;
    static const uint8_t CMD_WRITE_ENABLE = 0x06;
    static const uint8_t CMD_READ_PAGE = 0x13;
    static const uint8_t CMD_READ_FROM_CACHE = 0x03;
    static const uint8_t CMD_PROGRAM_LOAD = 0x02;
    static const uint8_t CMD_PROGRAM_EXECUTE = 0x10;

    static const uint8_t REG_STATUS = 0xC0;
    static const uint8_t REG_CONFIG = 0xB0;
    static const uint8_t REG_PROTECTION = 0xA0;
};

#endif
