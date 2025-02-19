#include "relic.h"

using namespace eio;

uint8_t eio::getEBrightnessAsByte(EBrightness inBrightness) {
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

RelicIO::RelicIO()
{
    setGlobalBrightness(EBrightness::MED);
}

EBrightness RelicIO::getGlobalBrightness() const
{
    return currentBrightness;
}

void RelicIO::setGlobalBrightness(EBrightness newBrightness)
{
    currentBrightness = newBrightness;

    uint8_t brightnessByte = getEBrightnessAsByte(newBrightness);

    for(auto seg : strips)
    {
        seg.second->setStripBrightness(brightnessByte);
    }
}

RelicCore::RelicCore()
{
}

void RelicCore::init()
{
}

void RelicCore::preTick()
{
    lastFrameDT = std::chrono::system_clock::now() - tickStartTime;
    tickStartTime = std::chrono::system_clock::now();

    tick(lastFrameDT.count());
}
