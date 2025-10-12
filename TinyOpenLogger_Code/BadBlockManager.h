#ifndef BADBLOCKMANAGER_H
#define BADBLOCKMANAGER_H

#include <Arduino.h>
#include "W25N01GV.h"

class BadBlockManager {
public:
    BadBlockManager(W25N01GV& flash);

    void scan();
    bool isBlockGood(uint16_t block);
    int16_t getNextGoodBlock();

private:
    W25N01GV& _flash;
    bool _goodBlocks[65536/64]; // 1024 blocks (for 65536 pages, 64 per block)
    uint8_t _nextBlockIndex;

    bool isFactoryBad(uint16_t blockBasePage);
};

#endif
