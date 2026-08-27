// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "obelisk.h"

#include <cstdlib>
#include <string>

#include "../../lib/eio/strip_projection.h"
#include "../../lib/ecore/logging.h"

#include "state_obelisk.h"

using namespace obelisk;
using namespace ecore;
using namespace ecore::log;
using namespace eio;

ObeliskIO::ObeliskIO() : RelicIO()
{
}

void ObeliskIO::init()
{
    RelicIO::init();

    auto [mainStripIt, stripInserted] = strips.emplace(static_cast<uint8_t>(0), make_unique<HSVStrip>(WALL_SIDE_LENGTH * 8, StripLEDPin));

    HSVStrip* mainStrip = mainStripIt->second.get();

    // Eight vertical runs: four sides, up and down each. Every run is
    // WALL_SIDE_LENGTH pixels and every run's coordinates have to line up with
    // its neighbour's, because the pixels physically do.
    //
    // The down runs start at WALL_SIDE_LENGTH - 1, not WALL_SIDE_LENGTH. They
    // used to start at WALL_SIDE_LENGTH, which put them one whole unit above
    // the up runs: an up run covered y 0..42 while the down run beside it
    // covered 43..1. Anything reading y - which is every look here, that is
    // what the coordinate is *for* - saw half the sculpture shifted by a pixel
    // against the other half. Subtle on a noise field and obvious on a gradient.

    // Side A

    int id1 = static_cast<uint8_t>(StripSegmentID::SideA_Up);
    auto [it1, inserted1] = strip_segments.emplace(id1, make_unique<HSVStripSegment>(mainStrip, id1));
    if (inserted1) HSVStripNodeFactory::GenerateAxisRow(it1->second.get(), 0                , WALL_SIDE_LENGTH, Coord(0,0), Coord(0, 1.f));

    int id2 = static_cast<uint8_t>(StripSegmentID::SideA_Down);
    auto [it2, inserted2] = strip_segments.emplace(id2, make_unique<HSVStripSegment>(mainStrip, id2));
    if (inserted2) HSVStripNodeFactory::GenerateAxisRow(it2->second.get(), WALL_SIDE_LENGTH , WALL_SIDE_LENGTH, Coord(1, WALL_SIDE_LENGTH - 1), Coord(0, -1.f));
    
    // Side B

    int id3 = static_cast<uint8_t>(StripSegmentID::SideB_Up);
    auto [it3, inserted3] = strip_segments.emplace(id3, make_unique<HSVStripSegment>(mainStrip, id3));
    if (inserted3) HSVStripNodeFactory::GenerateAxisRow(it3->second.get(), WALL_SIDE_LENGTH * 2, WALL_SIDE_LENGTH, Coord(2,0), Coord(0, 1.f));

    int id4 = static_cast<uint8_t>(StripSegmentID::SideB_Down);
    auto [it4, inserted4] = strip_segments.emplace(id4, make_unique<HSVStripSegment>(mainStrip, id4));
    if (inserted4) HSVStripNodeFactory::GenerateAxisRow(it4->second.get(), WALL_SIDE_LENGTH * 3, WALL_SIDE_LENGTH, Coord(3, WALL_SIDE_LENGTH - 1), Coord(0, -1.f));
    
    // Side C

    int id5 = static_cast<uint8_t>(StripSegmentID::SideC_Up);
    auto [it5, inserted5] = strip_segments.emplace(id5, make_unique<HSVStripSegment>(mainStrip, id5));
    if (inserted5) HSVStripNodeFactory::GenerateAxisRow(it5->second.get(), WALL_SIDE_LENGTH * 4, WALL_SIDE_LENGTH, Coord(4,0), Coord(0, 1.f));

    int id6 = static_cast<uint8_t>(StripSegmentID::SideC_Down);
    auto [it6, inserted6] = strip_segments.emplace(id6, make_unique<HSVStripSegment>(mainStrip, id6));
    if (inserted6) HSVStripNodeFactory::GenerateAxisRow(it6->second.get(), WALL_SIDE_LENGTH * 5, WALL_SIDE_LENGTH, Coord(5, WALL_SIDE_LENGTH - 1), Coord(0, -1.f));
    
    // Side D

    int id7 = static_cast<uint8_t>(StripSegmentID::SideD_Up);
    auto [it7, inserted7] = strip_segments.emplace(id7, make_unique<HSVStripSegment>(mainStrip, id7));
    if (inserted7) HSVStripNodeFactory::GenerateAxisRow(it7->second.get(), WALL_SIDE_LENGTH * 6, WALL_SIDE_LENGTH, Coord(6,0), Coord(0, 1.f));

    int id8 = static_cast<uint8_t>(StripSegmentID::SideD_Down);
    auto [it8, inserted8] = strip_segments.emplace(id8, make_unique<HSVStripSegment>(mainStrip, id8));
    if (inserted8) HSVStripNodeFactory::GenerateAxisRow(it8->second.get(), WALL_SIDE_LENGTH * 7, WALL_SIDE_LENGTH, Coord(7, WALL_SIDE_LENGTH - 1), Coord(0, -1.f));
    
    dbgLog("ObeliskIO::init", Verbosity::Verbose, Category::Relic);

    setGlobalBrightness(EBrightness::HIGH);
}

ObeliskCore::ObeliskCore() : RelicCore()
{
    coreIO = std::make_unique<ObeliskIO>();
    coreIO->init();

    stateMachine = std::make_unique<StateMachine_GenericHSV>();
    stateMachine->setRelicIO(coreIO.get());
    stateManager = std::make_unique<StateManager>();

    mainPatternState = std::make_shared<State_GenericHSV>("mainState", coreIO.get());
    mainPatternState->setGenerator(make_shared<Pattern_Obelisk_FourSeasons>());
    mainPatternState->init();

    theaterPatternState = std::make_shared<State_GenericHSV>("theaterState", coreIO.get());
    theaterPatternState->setGenerator(make_shared<Pattern_Obelisk_Theater>());
    theaterPatternState->init();

    testPatternState = std::make_shared<State_GenericHSV>("testState", coreIO.get());
    testPatternState->setGenerator(std::make_shared<Pattern_Obelisk_Monocolor>());
    testPatternState->init();

    mainPatternId = stateManager->addState(mainPatternState);
    theaterPatternId = stateManager->addState(theaterPatternState);
    testPatternId = stateManager->addState(testPatternState);

    // The scanner's looks, one state per afterglow state tag. The variants the
    // python picks with save-file flags (emergency lock, broken device, newly
    // discovered mushroom, seed verdict) are their own tags here, because only
    // the scanner knows the save - it just names the look it wants.
    auto addScannerState = [this](const char* name, shared_ptr<scanner::PatternScanner> pattern)
    {
        auto state = make_shared<scanner::State_ScannerHSV>(name, coreIO.get(), pattern);
        state->init();
        stateManager->addState(state);
        scannerStates[name] = state;
    };

    using namespace scanner;

    addScannerState("power_up",                        make_shared<Pattern_Scanner_PowerUp>());
    addScannerState("boot",                            make_shared<Pattern_Scanner_Boot>());
    addScannerState("scan_idle",                       make_shared<Pattern_Scanner_ScanIdle>());
    addScannerState("scan_idle_emergency",             make_shared<Pattern_Scanner_Emergency>());
    addScannerState("scan_idle_broken",                make_shared<Pattern_Scanner_Solid>(HSV(0.0f, 0.0f, 0.0f)));
    addScannerState("scan_item_detected_filter",       make_shared<Pattern_Scanner_DetectedWave>());
    addScannerState("scan_item_detected_generic",      make_shared<Pattern_Scanner_DetectedShimmer>());
    addScannerState("scan_item_detected_seed",         make_shared<Pattern_Scanner_DetectedShimmer>());
    addScannerState("scan_item_detected_mushroom",     make_shared<Pattern_Scanner_DetectedMushroom>());
    addScannerState("scan_item_detected_mushroom_new", make_shared<Pattern_Scanner_DetectedMushroomNew>());
    addScannerState("scan_item_success",               make_shared<Pattern_Scanner_Solid>(HSV(120.0f, 1.0f, 0.392f))); // rgb(0,100,0)
    addScannerState("scan_item_success_mushroom",      make_shared<Pattern_Scanner_SuccessMushroom>());
    addScannerState("scan_item_success_secret",        make_shared<Pattern_Scanner_Solid>(HSV(120.0f, 1.0f, 0.392f)));
    addScannerState("scan_item_failure",               make_shared<Pattern_Scanner_Solid>(HSV(0.0f, 1.0f, 0.471f)));   // rgb(120,0,0)
    addScannerState("audio_playback_mushroom",         make_shared<Pattern_Scanner_PlaybackMushroom>());
    addScannerState("audio_playback_generic",          make_shared<Pattern_Scanner_PlaybackGeneric>());
    addScannerState("audio_playback_seed",             make_shared<Pattern_Scanner_Solid>(HSV(120.0f, 1.0f, 0.392f)));
    addScannerState("audio_playback_seed_bad",         make_shared<Pattern_Scanner_Solid>(HSV(0.0f, 1.0f, 0.392f)));   // rgb(100,0,0)
    addScannerState("audio_playback_rest",             make_shared<Pattern_Scanner_Solid>(HSV(0.0f, 0.0f, 0.0f)));

    // The recording flow and the void stone - same table as
    // makeScannerStateMachine() in the desktop build, kept in step so a cue
    // works wherever it lands.
    addScannerState("record_arm",                      make_shared<Pattern_Scanner_SinePulse>(HSV(45.0f, 1.0f, 1.0f), 4.0f, 0.15f, 0.5f));   // CRGB(1.0, 0.75, 0.0)
    addScannerState("record_countdown",                make_shared<Pattern_Scanner_RecordCountdown>());
    addScannerState("record_active",                   make_shared<Pattern_Scanner_RecordComet>());
    addScannerState("record_saved",                    make_shared<Pattern_Scanner_SinePulse>(HSV(132.0f, 1.0f, 1.0f), 6.0f, 0.4f, 0.6f));   // CRGB(0.0, 1.0, 0.2)
    // both amber-wave: one recording look, unified with the desktop table
    addScannerState("scan_item_detected_recording",    make_shared<Pattern_Scanner_DetectedWave>(HSV(33.3f, 0.9f, 1.0f)));                   // CRGB(1.0, 0.6, 0.1)
    addScannerState("audio_playback_recording",        make_shared<Pattern_Scanner_DetectedWave>(HSV(33.3f, 0.9f, 1.0f)));
    addScannerState("void",                            make_shared<Pattern_Scanner_SinePulse>(HSV(282.0f, 1.0f, 0.5f), 2.0f, 0.0f, 0.6f));   // CRGB(0.35, 0.0, 0.5)

    // Start State Machine
    stateMachine->setActiveState(mainPatternState);
    stateMachine->init();

#if 0
    stateChangeTimer.onTimerEvent.add([this]() {


        dbgLog("ObeliskCore::stateChangeTimer - switching patterns", Verbosity::Display, Category::Relic);
        // Switch to the next state after the timer expires
        if (stateMachine->getActiveState() == mainPatternState) {
            stateMachine->setNextState(theaterPatternState);
        } else if (stateMachine->getActiveState() == theaterPatternState) {
            stateMachine->setNextState(testPatternState);
        } else {
            stateMachine->setNextState(mainPatternState);
        }

        stateChangeTimer.startTimer(5.0f);  // Reset the timer for the next state change
    });
#endif

    stateChangeTimer.startTimer(5.0f);  // Start the timer for the first state change
}


void obelisk::ObeliskCore::tick(float deltaTime)
{
    RelicCore::tick(deltaTime);

    stateMachine->tick(deltaTime);

    stateChangeTimer.tick(deltaTime);
}

void obelisk::ObeliskCore::say(const string& line)
{
    // Back up whatever cable the desk is on, rather than to Serial directly,
    // so this still reaches a caller that plugged in a different transport.
    if (elink::LinkTransport* transport = getLink().getTransport())
    {
        transport->writeLine(line.c_str());
    }
}

bool obelisk::ObeliskCore::handleCommand(string msg)
{
    // Trim: these arrive from a serial monitor as often as from a desk, and a
    // trailing \r has cost more debugging time than it has any right to.
    while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n' || msg.back() == ' '))
    {
        msg.pop_back();
    }

    // Was strcmp, which only ever resolved through whatever <cstring> Arduino.h
    // drags in - msg is a std::string and has always been one.
    if (msg == "switch")
    {
        dbgLog("ObeliskCore::handleCommand - switching patterns", Verbosity::Display, Category::Relic);
        stateMachine->setNextState(mainPatternState);
        return true;
    }

    // Cue mode: the desk names a look and the obelisk renders it itself. The
    // names are the ones the desk's own buttons send, so a cue list works over
    // the link without either end knowing about the other's spelling.
    //
    // `state <name> [seconds]` - the optional seconds set the blend time for
    // this change, which is how the scanner's transitionTo(state, time) pairs
    // arrive as a single line.
    if (msg.rfind("state ", 0) == 0)
    {
        string wanted = msg.substr(6);

        const size_t space = wanted.find(' ');
        if (space != string::npos)
        {
            const string seconds = wanted.substr(space + 1);
            wanted = wanted.substr(0, space);

            const float requested = static_cast<float>(atof(seconds.c_str()));
            if (requested >= 0.0f)
            {
                stateMachine->transitionTime = requested;
            }
        }

        shared_ptr<State> target{nullptr};
        if (wanted == "seasons" || wanted == "main")   target = mainPatternState;
        else if (wanted == "theater")                  target = theaterPatternState;
        else if (wanted == "mono" || wanted == "test") target = testPatternState;
        else
        {
            auto it = scannerStates.find(wanted);
            if (it != scannerStates.end())
            {
                target = it->second;
            }
        }

        if (target)
        {
            // The scanner resends its current state on reconnects; arriving
            // where we already are is success, not an error log.
            if (target != stateMachine->getActiveState() && target != stateMachine->getNextState())
            {
                stateMachine->setNextState(target);
            }
            say("EOSLINK state " + wanted);
            return true;
        }

        say("EOSLINK unknown state " + wanted);
        return true;
    }

    if (msg == "states")
    {
        string reply = "EOSLINK states seasons theater mono";
        for (const auto& entry : scannerStates)
        {
            reply += " " + entry.first;
        }
        say(reply);
        return true;
    }

    return RelicCore::handleCommand(msg); // Call the base class method to handle any other commands
}
