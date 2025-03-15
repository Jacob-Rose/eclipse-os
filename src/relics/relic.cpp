#include "relic.h"

#include "../lib/ecore/logging.h"

using namespace eio;
using namespace ecore::log;

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
    for(const auto& seg : strips)
    {
        #if DEBUG_LOGGING_ENABLED
        dbgLog("RelicIO::showLeds", Verbosity::Display, Category::StateInfo);
    #endif
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
    lastFrameDT = std::chrono::system_clock::now() - tickStartTime;
    tickStartTime = std::chrono::system_clock::now();
}

void RelicCore::tick(float deltaTime)
{
    if(coreState)
    {
        // TODO make this take in the delta time and not self calculate somehow
        coreState->tick(deltaTime);

        if(coreIO)
        {
            for(const auto& seg : coreIO->strip_segments)
            {
                for(const auto& node : seg.second->getNodes())
                {
                    coreState->render(seg.second.get(), node.get());
                }
            }
        }
    }
}

void RelicCore::postTick()
{
    if(coreIO)
    {
        coreIO->showLeds();
    }
}

void RelicCore::runTick()
{
    dbgLog("RelicCore::runTick", Verbosity::Display, Category::OnTick);
    preTick();
    tick(lastFrameDT.count());
    postTick();
}
