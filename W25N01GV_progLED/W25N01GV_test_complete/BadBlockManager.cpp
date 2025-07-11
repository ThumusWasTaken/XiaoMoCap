#include "BadBlockManager.h"

BadBlockManager::BadBlockManager(W25N01GV& flash)
: _flash(flash), _nextBlockIndex(0) {
    for (uint8_t i = 0; i < sizeof(_goodBlocks); i++) {
        _goodBlocks[i] = false;
    }
}

bool BadBlockManager::isFactoryBad(uint16_t blockBasePage) {
    uint8_t marker = 0xFF;
    _flash.readSpareArea(blockBasePage, &marker, 1);
    return marker != 0xFF;
}

void BadBlockManager::scan() {
    Serial.println("Scanning for good blocks...");
    for (uint8_t block = 0; block < 16; block++) {
        uint16_t page = block * 64;
        Serial.print("Block ");
        Serial.print(block);
        Serial.print(" (page ");
        Serial.print(page);
        Serial.print("): ");

        if (isFactoryBad(page)) {
            Serial.println("Factory BAD");
            _goodBlocks[block] = false;
            continue;
        }

        // Try erasing the block
        _flash.writeEnable();
        if (!_flash.blockErase(page)) {
            Serial.println("Erase FAIL - marking bad");
            _goodBlocks[block] = false;
            continue;
        }

        Serial.println("GOOD");
        _goodBlocks[block] = true;
    }
}

bool BadBlockManager::isBlockGood(uint16_t block) {
    if (block >= 16) return false;
    return _goodBlocks[block];
}

int16_t BadBlockManager::getNextGoodBlock() {
    for (uint8_t i = _nextBlockIndex; i < 16; i++) {
        if (_goodBlocks[i]) {
            _nextBlockIndex = i + 1;
            return i;
        }
    }
    return -1; // No more good blocks
}
