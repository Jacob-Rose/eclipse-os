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

void RelicIO::tick(float deltaTime)
{
}

void RelicIO::showLeds()
{
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

    tick(lastFrameDT.count());
}

void RelicCore::tick(float deltaTime)
{
    if(coreState)
    {
        // TODO make this take in the delta time and not self calculate somehow
        coreState->tick(deltaTime);
    }
}

void RelicCore::postTick()
{
    if(io)
    {
        io->showLeds();
    }
}

void RelicCore::runTick()
{
    preTick();
    tick(lastFrameDT.count());
    postTick();
}
