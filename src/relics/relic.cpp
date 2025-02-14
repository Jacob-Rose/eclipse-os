#include "relic.h"

uint8_t getEBrightnessAsByte(EBrightness inBrightness){
    switch(inBrightness)
    {
        case EBrightness::NIGHTTRIP:
            return 7;
        case EBrightness::MIN:
            return 15;
        case EBrightness::MED:
            return 31;
        case EBrightness::HIGH:
            return 63;
        case EBrightness::BLINDING:
            return 127;
        case EBrightness::MAX:
            return 255;
        default:
            return 0;
    }
    return 0;
}

EBrightness RelicIO::getGlobalBrightness() const
{
    return currentBrightness;
}

void GameManager::setGlobalBrightness(EBrightness newBrightness)
{
    currentBrightness = newBrightness;

    uint8_t brightnessByte = getEBrightnessAsByte(newBrightness);

    RingLEDs->setStripBrightness(brightnessByte);
    OutfitLEDs->setStripBrightness(brightnessByte);
    GlassesLEDs->setStripBrightness(brightnessByte);
}