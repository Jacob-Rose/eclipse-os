#include "relic.h"

#include "../lib/ecore/logging.h"

using namespace eio;
using namespace ecore::log;

#define RELIC_DEBUG_ENABLED 1

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
    dbgLog("RelicIO::showLeds", Verbosity::Verbose, Category::OnTick | Category::Relic);
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
    lastFrameDT = std::chrono::system_clock::now() - tickStartTime;
    tickStartTime = std::chrono::system_clock::now();
}

void RelicCore::tick(float deltaTime)
{
    if(coreState)
    {
        // TODO make this take in the delta time and not self calculate somehow
        coreState->tick(deltaTime);
#if RELIC_DEBUG_ENABLED
        dbgLog("coreState is valid");
#endif

        if(coreIO)
        {
#if RELIC_DEBUG_ENABLED
            dbgLog("coreIO is valid");
#endif
            for(const auto& seg : coreIO->strip_segments)
            {
#if RELIC_DEBUG_ENABLED
                if(seg.second == nullptr)
                {
                    dbgLog("seg is invalid");
                    continue; // skip if segment is null
                }
                else
                {
                    dbgLog("seg is valid");
                }
#endif
                for(const std::shared_ptr<HSVStripNode>& node : seg.second->getNodes())
                {
#if RELIC_DEBUG_ENABLED
                    if(node == nullptr)
                    {
                        dbgLog("node is invalid");
                        continue; // skip if segment is null
                    }
                    else
                    {
                        dbgLog("node is valid");
                    }
#endif
                    HSVStripNode* nodePtr = node.get();
                    if(nodePtr == nullptr)
                    {
                        dbgLog("nodePtr is null, skipping render");
                        continue; // skip if nodePtr is null
                    }
                    HSVStripSegment* segPtr = seg.second.get();
                    if(segPtr == nullptr)
                    {
                        dbgLog("segPtr is null, skipping render");
                        continue; // skip if segPtr is null
                    }
                    if(coreState == nullptr)
                    {
                        dbgLog("coreState is null, skipping render");
                        continue; // skip if coreState is null
                    }
                    coreState->render(segPtr, nodePtr);
                }
            }
        }
    }
}

void RelicCore::postTick()
{
    dbgLog("RelicCore::postTick");
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
