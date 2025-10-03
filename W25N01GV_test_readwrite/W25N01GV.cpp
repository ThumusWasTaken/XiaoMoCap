// W25N01GV.cpp
#include "W25N01GV.h"

W25N01GV::W25N01GV(uint8_t csPin)
: _cs(csPin),
  _spiSettings(4000000, MSBFIRST, SPI_MODE0) // 4 MHz
{}

void W25N01GV::setSPIFrequency(uint32_t hz) {
    _spiSettings = SPISettings(hz, MSBFIRST, SPI_MODE0);
}
bool W25N01GV::begin() {
    pinMode(_cs, OUTPUT);
    deselect();
    SPI.begin();
    reset();
    delay(1);
    return true;
}

void W25N01GV::reset() {
    select();
    transfer(CMD_RESET);
    deselect();
    delay(1);
}

uint8_t W25N01GV::readStatus() {
    select();
    transfer(CMD_GET_FEATURE);
    transfer(REG_STATUS);
    uint8_t status = transfer(0x00);
    deselect();
    return status;
}

bool W25N01GV::writeEnable() {
    select();
    transfer(CMD_WRITE_ENABLE);
    deselect();
    delayMicroseconds(10); // Increased delay
    
    // Verify write enable was successful
    uint8_t status = readStatus();
    return (status & 0x02) != 0; // Check WEL bit
}

bool W25N01GV::isReady() {
    uint8_t status = readStatus();
    return !(status & 0x01); // Return true if not busy
}

bool W25N01GV::readPage(uint32_t page, uint8_t* buffer, uint16_t length) {
    select();
    transfer(CMD_READ_PAGE);
    transfer((page >> 8) & 0xFF);
    transfer(page & 0xFF);
    transfer(0x00);
    deselect();
    delay(1); // Wait for data to load into cache

    select();
    transfer(CMD_READ_FROM_CACHE);
    transfer(0x00); // Column address MSB
    transfer(0x00); // Column address LSB
    transfer(0x00); // Dummy byte

    for (uint16_t i = 0; i < length; i++) {
        buffer[i] = transfer(0x00);
    }
    deselect();
    return true;
}

bool W25N01GV::blockErase(uint32_t page) {
  writeEnable();
  uint8_t status = readStatus();
  if (!(status & 0x02)) {
    return false;
  }

  select();
  transfer(CMD_BLOCK_ERASE);
  transfer(0x00);
  transfer((page >> 8) & 0xFF);
  transfer(page & 0xFF);
  deselect();

  unsigned long startTime = millis();
  while (millis() - startTime < 5000) {
    status = readStatus();
    if (!(status & 0x01)) {
      break;
    }
    delay(10);
  }
  if (millis() - startTime >= 5000) {
    return false;
  }

  status = readStatus();
  return !(status & 0x04);
}


bool W25N01GV::programLoad(uint16_t column, const uint8_t* data, uint16_t length) {
    writeEnable();
    select();
    transfer(CMD_PROGRAM_LOAD);
    transfer((column >> 8) & 0xFF);
    transfer(column & 0xFF);
    for (uint16_t i = 0; i < length; i++) {
        transfer(data[i]);
    }
    deselect();
    return true;
}

bool W25N01GV::programExecute(uint32_t page) {
    select();
    transfer(CMD_PROGRAM_EXECUTE);
    transfer((page >> 8) & 0xFF);
    transfer(page & 0xFF);
    transfer(0x00);
    deselect();
    
    // Wait for operation to start
    delayMicroseconds(10);
    
    // Poll until program completes with timeout
    uint16_t timeout = 0;
    while ((readStatus() & 0x01) && timeout < 5000) {
        delay(1);
        timeout++;
    }
    
    if (timeout >= 5000) {
        return false; // Timeout
    }
    
    // Check P_FAIL
    uint8_t status = readStatus();
    if (status & 0x08) {
        return false; // Program failed
    }
    
    return true;
}

void W25N01GV::select() {
    SPI.beginTransaction(_spiSettings);
    digitalWrite(_cs, LOW);
}

void W25N01GV::deselect() {
    digitalWrite(_cs, HIGH);
    SPI.endTransaction();
}

uint8_t W25N01GV::transfer(uint8_t data) {
    return SPI.transfer(data);
}

bool isPageUsable(W25N01GV &flash, uint32_t page) {
  // Try to erase the block containing this page
  bool eraseResult = flash.blockErase(page);
  uint8_t status = flash.readStatus();
  
  // Check if erase succeeded and no error flags are set
  return eraseResult && !(status & 0x04); // No E_FAIL bit
}

uint16_t findUsablePage(W25N01GV &flash, uint16_t startPage = 100) {
  Serial.println("Scanning for usable pages...");
  
  for (uint32_t page = startPage; page < 65536; page += 64) { // Check every 64 pages (each block)
    Serial.print("Testing page ");
    Serial.print(page);
    Serial.print("... ");
    
    if (isPageUsable(flash, page)) {
      Serial.println("USABLE");
      return page;
    } else {
      Serial.println("BAD/PROTECTED");
    }
  }
  
  Serial.println("No usable pages found!");
  return 0; // Return 0 to indicate failure
}

bool W25N01GV::setECCEnabled(bool enabled) {
    // Read current configuration
    select();
    transfer(CMD_GET_FEATURE);
    transfer(REG_CONFIG);
    uint8_t config = transfer(0x00);
    deselect();
    
    // Modify ECC bit (bit 4)
    if (enabled) {
        config |= 0x10;  // Set ECC enable bit
    } else {
        config &= ~0x10; // Clear ECC enable bit
    }
    
    // Write back configuration
    select();
    transfer(CMD_SET_FEATURE);
    transfer(REG_CONFIG);
    transfer(config);
    deselect();
    
    // Verify the setting
    delayMicroseconds(10);
    select();
    transfer(CMD_GET_FEATURE);
    transfer(REG_CONFIG);
    uint8_t verify = transfer(0x00);
    deselect();
    
    return (verify & 0x10) == (enabled ? 0x10 : 0x00);
}

bool W25N01GV::clearBlockProtection() {
    // Clear all block protection bits
    select();
    transfer(CMD_SET_FEATURE);
    transfer(REG_PROTECTION);
    transfer(0x00);  // Clear all protection
    deselect();
    
    // Verify
    delayMicroseconds(10);
    select();
    transfer(CMD_GET_FEATURE);
    transfer(REG_PROTECTION);
    uint8_t protection = transfer(0x00);
    deselect();
    
    Serial.print("Block protection register: 0x");
    Serial.println(protection, HEX);
    
    return protection == 0x00;
}

uint8_t W25N01GV::readConfiguration() {
    select();
    transfer(CMD_GET_FEATURE);
    transfer(REG_CONFIG);
    uint8_t config = transfer(0x00);
    deselect();
    return config;
}

bool W25N01GV::readSpareArea(uint32_t page, uint8_t* buffer, uint16_t length) {
    // Load page into cache
    select();
    transfer(CMD_READ_PAGE);
    transfer((page >> 8) & 0xFF);
    transfer(page & 0xFF);
    transfer(0x00);
    deselect();
    delay(1);

    // Read spare area from column 2048 onward
    select();
    transfer(CMD_READ_FROM_CACHE);
    transfer(0x08); // Column 2048 MSB
    transfer(0x00); // Column 0 LSB
    transfer(0x00); // Dummy

    for (uint16_t i = 0; i < length; i++) {
        buffer[i] = transfer(0x00);
    }
    deselect();

    return true;
}

