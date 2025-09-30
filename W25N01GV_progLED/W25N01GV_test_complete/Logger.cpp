#include "Logger.h"

Logger::Logger(W25N01GV& flash, BadBlockManager& badBlockManager)
: _flash(flash), _bbm(badBlockManager), _bufferPos(0), _currentPage(0), _currentBlock(0)
{}

bool Logger::begin() {
    // Get first good block
    int16_t goodBlock = _bbm.getNextGoodBlock();
    if (goodBlock < 0) {
        Serial.println("Logger: No good blocks available!");
        return false;
    }
    _currentBlock = goodBlock;
    _currentPage = _currentBlock * 64;

    // Erase block
    _flash.writeEnable();
    if (!_flash.blockErase(_currentPage)) {
        Serial.println("Logger: Erase failed!");
        return false;
    }

    _bufferPos = 0;
    return true;
}

void Logger::logRecord(const String& record) {
    uint16_t len = record.length();
    if (len + 1 > (sizeof(_buffer) - _bufferPos)) {
        flush();
    }

    record.getBytes(_buffer + _bufferPos, len + 1);
    _bufferPos += len;

    // Add newline
    _buffer[_bufferPos++] = '\n';

    if (_bufferPos >= sizeof(_buffer)) {
        flush();
    }
}

void Logger::flush() {
    if (_bufferPos == 0) return;

    _flash.writeEnable();
    _flash.programLoad(0, _buffer, _bufferPos);
    _flash.programExecute(_currentPage);

    Serial.print("Logger: Flushed page ");
    Serial.println(_currentPage);

    _bufferPos = 0;
    _currentPage++;

    // If we filled the block, get next one
    if ((_currentPage % 64) == 0) {
        int16_t nextBlock = _bbm.getNextGoodBlock();
        if (nextBlock < 0) {
            Serial.println("Logger: No more good blocks!");
            return;
        }
        _currentBlock = nextBlock;
        _currentPage = _currentBlock * 64;

        // Erase new block
        _flash.writeEnable();
        _flash.blockErase(_currentPage);
    }
}

void Logger::readAllLogs() {
    uint8_t pageBuf[2048];

    Serial.println("Reading back logs:");
    //for (uint32_t page = 0; page < _currentPage; page++) {
    for (uint32_t page = _currentBlock * 64; page < _currentPage; page++) {
        _flash.readPage(page, pageBuf, 2048);

        for (uint16_t i = 0; i < 2048; i++) {
            char c = (char)pageBuf[i];
            if (c == 0xFF || c == 0) break;
            Serial.print(c);
        }
    }
    Serial.println();
}
