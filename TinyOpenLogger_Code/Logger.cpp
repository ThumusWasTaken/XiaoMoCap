#include "Logger.h"

Logger::Logger(W25N01GV& flash, BadBlockManager& badBlockManager)
: _flash(flash), _bbm(badBlockManager),
  _recordCount(0), _currentPage(0), _currentBlock(0),
  _pageSeq(0), _headerWritten(false)
{}

bool Logger::begin() {
    Serial.printf("SensorRecord size = %u bytes\n", sizeof(SensorRecord));

    int16_t goodBlock = _bbm.getNextGoodBlock();
    if (goodBlock < 0) {
        Serial.println("Logger: No good blocks available!");
        return false;
    }

    _currentBlock = goodBlock;
    _currentPage = _currentBlock * 64;
    eraseNextBlock();

    _recordCount = 0;
    memset(_pageBuf, 0xFF, sizeof(_pageBuf));
    _headerWritten = false;
    _pageSeq = 0;
    return true;
}

bool Logger::logRecord(const SensorRecord& rec) {
    // Write header only once per page
    if (!_headerWritten) {
        writeHeader(rec.counter, rec.timestamp);
        _headerWritten = true;
    }

    // Append record
    uint16_t offset = HEADER_SIZE + _recordCount * RECORD_SIZE;
    memcpy(&_pageBuf[offset], &rec, RECORD_SIZE);
    _recordCount++;

    if (_recordCount >= RECORDS_PER_PAGE) {
        flush();
    }

    return true;
}

void Logger::writeHeader(uint32_t firstCounter, uint32_t firstTs) {
    PageHeader hdr;
    hdr.magic = PAGE_MAGIC;
    hdr.page_seq = _pageSeq;
    hdr.first_counter = firstCounter;
    hdr.first_ts = firstTs;
    memcpy(_pageBuf, &hdr, sizeof(hdr));
}

void Logger::flush() {
    if (_recordCount == 0) return;

    // Wait for flash to be ready before starting a new program cycle
    while (!_flash.isReady()) {
        delayMicroseconds(10);
    }

    _flash.writeEnable();

    // Load data into cache
    bool ok = _flash.programLoad(0, _pageBuf, PAGE_SIZE);
    if (!ok) {
        Serial.println("Logger: programLoad failed");
        return;
    }

    // Execute program command (writes page from cache to array)
    ok = _flash.programExecute(_currentPage);
    if (!ok) {
        Serial.println("Logger: programExecute failed");
        return;
    }

    // Wait for flash to complete the program operation before continuing
    while (!_flash.isReady()) {
        delayMicroseconds(10);
    }

    Serial.printf("Logger: Flushed page %lu seq=%lu with %u records\n",
                  _currentPage, _pageSeq, _recordCount);

    _recordCount = 0;
    memset(_pageBuf, 0xFF, PAGE_SIZE);
    _currentPage++;
    _pageSeq++;
    _headerWritten = false;

    // Move to next block if needed
    if ((_currentPage % 64) == 0) {
        int16_t nextBlock = _bbm.getNextGoodBlock();
        if (nextBlock < 0) {
            Serial.println("Logger: No more good blocks!");
            return;
        }
        _currentBlock = nextBlock;
        _currentPage = _currentBlock * 64;
        eraseNextBlock();
    }
}

void Logger::eraseNextBlock() {
    _flash.writeEnable();
    if (!_flash.blockErase(_currentPage)) {
        Serial.printf("Logger: Erase failed on block %u\n", _currentBlock);
    } else {
        Serial.printf("Logger: Erased block %u (page %lu)\n", _currentBlock, _currentPage);
    }
}

void Logger::eraseAll() {
    Serial.println("Logger: Erasing entire flash...");

    const uint16_t BLOCKS = 1024;
    uint16_t erased = 0;

    for (uint16_t block = 0; block < BLOCKS; block++) {
        if (!_bbm.isBlockGood(block)) continue;

        uint32_t page = block * 64;
        _flash.writeEnable();
        _flash.blockErase(page);
        erased++;
        if (erased % 10 == 0) {
            Serial.printf("Erased %u blocks\n", erased);
        }
        delay(2);
    }

    Serial.printf("Logger: Full erase complete (%u blocks)\n", erased);
}


void Logger::readAllLogs() {
    uint8_t readBuf[PAGE_SIZE];

    Serial.println("Reading back logs (header + binary records):");
    for (uint32_t page = 0; page < _currentPage; page++) {
        if (!_flash.readPage(page, readBuf, PAGE_SIZE)) continue;

        PageHeader* hdr = (PageHeader*)readBuf;
        if (hdr->magic != PAGE_MAGIC) continue; // skip invalid pages

        Serial.printf("\nPage %lu seq=%lu firstCounter=%lu ts=%lu\n",
                      page, hdr->page_seq, hdr->first_counter, hdr->first_ts);

        for (uint16_t i = 0; i < RECORDS_PER_PAGE; i++) {
            SensorRecord* rec = (SensorRecord*)&readBuf[HEADER_SIZE + i * RECORD_SIZE];
            if (rec->timestamp == 0xFFFFFFFF) break;

            Serial.printf("%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lu\n",
                          rec->timestamp, rec->ax, rec->ay, rec->az,
                          rec->gx, rec->gy, rec->gz,
                          rec->mx, rec->my, rec->mz, rec->counter);
        }
    }
    Serial.println("=== End of logs ===");
}


// #include "Logger.h"

// Logger::Logger(W25N01GV& flash, BadBlockManager& badBlockManager)
// : _flash(flash), _bbm(badBlockManager),
//   _recordCount(0), _currentPage(0), _currentBlock(0)
// {}

// bool Logger::begin() {
//     int16_t goodBlock = _bbm.getNextGoodBlock();
//     if (goodBlock < 0) {
//         Serial.println("Logger: No good blocks available!");
//         return false;
//     }

//     _currentBlock = goodBlock;
//     _currentPage = _currentBlock * 64;
//     eraseNextBlock();

//     _recordCount = 0;
//     memset(_pageBuf, 0xFF, sizeof(_pageBuf));
//     return true;
// }

// bool Logger::logRecord(const SensorRecord& rec) {
//     memcpy(&_pageBuf[_recordCount * RECORD_SIZE], &rec, RECORD_SIZE);
//     _recordCount++;

//     if (_recordCount >= RECORDS_PER_PAGE) {
//         flush();
//     }

//     return true;
// }

// void Logger::flush() {
//     if (_recordCount == 0) return;

//     _flash.writeEnable();
//     bool ok = _flash.programLoad(0, _pageBuf, PAGE_SIZE);
//     if (!ok) {
//         Serial.println("Logger: programLoad failed");
//         return;
//     }

//     ok = _flash.programExecute(_currentPage);
//     if (!ok) {
//         Serial.println("Logger: programExecute failed");
//         return;
//     }

//     Serial.printf("Logger: Flushed page %lu with %u records\n", _currentPage, _recordCount);

//     _recordCount = 0;
//     memset(_pageBuf, 0xFF, PAGE_SIZE);
//     _currentPage++;

//     // Move to next block if needed
//     if ((_currentPage % 64) == 0) {
//         int16_t nextBlock = _bbm.getNextGoodBlock();
//         if (nextBlock < 0) {
//             Serial.println("Logger: No more good blocks!");
//             return;
//         }
//         _currentBlock = nextBlock;
//         _currentPage = _currentBlock * 64;
//         eraseNextBlock();
//     }
// }

// void Logger::eraseNextBlock() {
//     _flash.writeEnable();
//     if (!_flash.blockErase(_currentPage)) {
//         Serial.printf("Logger: Erase failed on block %u\n", _currentBlock);
//     } else {
//         Serial.printf("Logger: Erased block %u (page %lu)\n", _currentBlock, _currentPage);
//     }
// }

// void Logger::readAllLogs() {
//     uint8_t readBuf[PAGE_SIZE];

//     Serial.println("Reading back logs (binary decoded):");
//     for (uint32_t page = 0; page < _currentPage; page++) {
//         if (!_flash.readPage(page, readBuf, PAGE_SIZE)) continue;

//         for (uint16_t i = 0; i < RECORDS_PER_PAGE; i++) {
//             SensorRecord* rec = (SensorRecord*)&readBuf[i * RECORD_SIZE];
//             if (rec->timestamp == 0xFFFFFFFF) break; // unused space

//             Serial.printf("%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lu\n",
//                           rec->timestamp, rec->ax, rec->ay, rec->az,
//                           rec->gx, rec->gy, rec->gz,
//                           rec->mx, rec->my, rec->mz, rec->counter);
//         }
//     }
//     Serial.println("=== End of logs ===");
// }


// #include "Logger.h"

// Logger::Logger(W25N01GV& flash, BadBlockManager& badBlockManager)
// : _flash(flash), _bbm(badBlockManager), _bufferPos(0), _currentPage(0), _currentBlock(0)
// {}

// bool Logger::begin() {
//     // Get first good block
//     int16_t goodBlock = _bbm.getNextGoodBlock();
//     if (goodBlock < 0) {
//         Serial.println("Logger: No good blocks available!");
//         return false;
//     }
//     _currentBlock = goodBlock;
//     _currentPage = _currentBlock * 64;

//     // Erase block
//     _flash.writeEnable();
//     if (!_flash.blockErase(_currentPage)) {
//         Serial.println("Logger: Erase failed!");
//         return false;
//     }

//     _bufferPos = 0;
//     return true;
// }

// void Logger::logRecord(const String& record) {
//     uint16_t len = record.length();
//     if (len + 1 > (sizeof(_buffer) - _bufferPos)) {
//         flush();
//     }

//     record.getBytes(_buffer + _bufferPos, len + 1);
//     _bufferPos += len;

//     // Add newline
//     _buffer[_bufferPos++] = '\n';

//     if (_bufferPos >= sizeof(_buffer)) {
//         flush();
//     }
// }

// void Logger::flush() {
//     if (_bufferPos == 0) return;

//     _flash.writeEnable();
//     _flash.programLoad(0, _buffer, _bufferPos);
//     _flash.programExecute(_currentPage);

//     Serial.print("Logger: Flushed page ");
//     Serial.println(_currentPage);

//     _bufferPos = 0;
//     _currentPage++;

//     // If we filled the block, get next one
//     if ((_currentPage % 64) == 0) {
//         int16_t nextBlock = _bbm.getNextGoodBlock();
//         if (nextBlock < 0) {
//             Serial.println("Logger: No more good blocks!");
//             return;
//         }
//         _currentBlock = nextBlock;
//         _currentPage = _currentBlock * 64;

//         // Erase new block
//         _flash.writeEnable();
//         _flash.blockErase(_currentPage);
//     }
// }

// void Logger::readAllLogs() {
//     uint8_t pageBuf[2048];

//     Serial.println("Reading back logs:");
//     //for (uint32_t page = 0; page < _currentPage; page++) {
//     for (uint32_t page = _currentBlock * 64; page < _currentPage; page++) {
//         _flash.readPage(page, pageBuf, 2048);

//         for (uint16_t i = 0; i < 2048; i++) {
//             char c = (char)pageBuf[i];
//             if (c == 0xFF || c == 0) break;
//             Serial.print(c);
//         }
//     }
//     Serial.println();
// }
