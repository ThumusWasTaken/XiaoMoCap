#ifndef LOGGER_H
#define LOGGER_H

#include "W25N01GV.h"
#include "BadBlockManager.h"

// --- Binary record format ---
struct SensorRecord {
    uint32_t timestamp;  // ms since start
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
    int16_t mx, my, mz;
    uint32_t counter;
} __attribute__((packed));  // use only attribute, no pragma here

static_assert(sizeof(SensorRecord) == 26, "SensorRecord must be 26 bytes!");

#pragma pack(push, 1)
struct PageHeader {
    uint32_t magic;         // 0xABCD1234 identifies a valid page
    uint32_t page_seq;      // sequential page index
    uint32_t first_counter; // first sample counter
    uint32_t first_ts;      // first timestamp (seconds)
};
#pragma pack(pop)

#define PAGE_MAGIC      0xABCD1234UL
#define PAGE_SIZE       2048
#define RECORD_SIZE     sizeof(SensorRecord)
#define HEADER_SIZE     sizeof(PageHeader)
#define RECORDS_PER_PAGE ((PAGE_SIZE - HEADER_SIZE) / RECORD_SIZE)

class Logger {
public:
    Logger(W25N01GV& flash, BadBlockManager& badBlockManager);

    bool begin();
    bool logRecord(const SensorRecord& rec);
    void flush();
    void readAllLogs();  // debug readback
    void eraseAll();
    
    uint32_t getCurrentPage() const { return _currentPage; }
    uint32_t getNextPageSeq() const { return _pageSeq; }

private:
    W25N01GV& _flash;
    BadBlockManager& _bbm;

    uint8_t _pageBuf[PAGE_SIZE];
    uint16_t _recordCount;
    uint32_t _currentPage;
    uint16_t _currentBlock;
    uint32_t _pageSeq;
    bool _headerWritten;

    void writeHeader(uint32_t firstCounter, uint32_t firstTs);
    void eraseNextBlock();
    

};

#endif


// #ifndef LOGGER_H
// #define LOGGER_H

// #include "W25N01GV.h"
// #include "BadBlockManager.h"

// // --- Binary record format ---
// // Each record is fixed-size and aligns with NAND page boundaries
// #pragma pack(push, 1)
// struct SensorRecord {
//     uint32_t timestamp;  // seconds since epoch
//     int16_t ax, ay, az;
//     int16_t gx, gy, gz;
//     int16_t mx, my, mz;
//     uint32_t counter;
// };
// #pragma pack(pop)

// #define RECORD_SIZE sizeof(SensorRecord)
// #define PAGE_SIZE   2048
// #define RECORDS_PER_PAGE (PAGE_SIZE / RECORD_SIZE)

// class Logger {
// public:
//     Logger(W25N01GV& flash, BadBlockManager& badBlockManager);

//     bool begin();
//     bool logRecord(const SensorRecord& rec);
//     void flush();
//     void readAllLogs();  // optional: debug readback

//     uint32_t getCurrentPage() const { return _currentPage; }

// private:
//     W25N01GV& _flash;
//     BadBlockManager& _bbm;

//     uint8_t _pageBuf[PAGE_SIZE];
//     uint16_t _recordCount;
//     uint32_t _currentPage;
//     uint16_t _currentBlock;

//     void eraseNextBlock();
// };

// #endif


// #ifndef LOGGER_H
// #define LOGGER_H

// #include "W25N01GV.h"
// #include "BadBlockManager.h"

// class Logger {
// public:
//     Logger(W25N01GV& flash, BadBlockManager& badBlockManager);
    
//     bool begin();
//     void logRecord(const String& record);
//     void flush();
//     void readAllLogs();
//     uint32_t getCurrentPage() { return _currentPage; }


// private:
//     W25N01GV& _flash;
//     BadBlockManager& _bbm;
//     uint8_t _buffer[2048];
//     uint16_t _bufferPos;
//     uint32_t _currentPage;
//     uint16_t _currentBlock;
// };

// #endif
