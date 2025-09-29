#ifndef LOGGER_H
#define LOGGER_H

#include "W25N01GV.h"
#include "BadBlockManager.h"

class Logger {
public:
    Logger(W25N01GV& flash, BadBlockManager& badBlockManager);
    
    bool begin();
    void logRecord(const String& record);
    void flush();
    void readAllLogs();
    uint32_t getCurrentPage() { return _currentPage; }


private:
    W25N01GV& _flash;
    BadBlockManager& _bbm;
    uint8_t _buffer[2048];
    uint16_t _bufferPos;
    uint32_t _currentPage;
    uint16_t _currentBlock;
};

#endif
