#include "relic.h"

#include "../ecore/logging.h"

using namespace eio;
using namespace ecore::log;

#define RELIC_DEBUG_ENABLED DEBUG_LOGGING_ENABLED && 1

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

void RelicIO::init()
{
}

void RelicIO::tick(float deltaTime)
{
}

void RelicIO::showLeds()
{
#if DEBUG_LOGGING_ENABLED
    dbgLog("RelicIO::showLeds", Verbosity::Verbose, Category::OnTick | Category::Relic);
#endif
    for(const auto& seg : strips)
    {
        seg.second.get()->show();
    }
}

EBrightness RelicIO::getGlobalBrightness() const
{
    return currentBrightness;
}

void RelicIO::setGlobalBrightness(EBrightness newBrightness)
{
    currentBrightness = newBrightness;

    uint8_t brightnessByte = getEBrightnessAsByte(newBrightness);

    for(const auto& seg : strips)
    {
        seg.second->setStripBrightness(brightnessByte);
    }
}

RelicCore::RelicCore()
{
}

void RelicCore::preTick()
{
    lastFrameDT = std::chrono::steady_clock::now() - tickStartTime;
    tickStartTime = std::chrono::steady_clock::now();
}

void RelicCore::tick(float deltaTime)
{
}

void RelicCore::postTick()
{
#if RELIC_DEBUG_ENABLED
    dbgLog("RelicCore::postTick", Verbosity::Verbose, Category::OnTick);
#endif
    if(coreIO)
    {
        coreIO->showLeds();
    }
}

void RelicCore::runTick()
{
#if RELIC_DEBUG_ENABLED
    dbgLog("RelicCore::runTick", Verbosity::Display, Category::OnTick);
#endif
    preTick();
    tick(lastFrameDT.count());
    postTick();
}
