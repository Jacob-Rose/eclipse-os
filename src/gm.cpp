// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "gm.h"

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

GameManager* GameManager::singleton = nullptr;

void GameManager::initSingleton()
{
    if(singleton == nullptr)
    {
        singleton = new GameManager();
        singleton->init();
    } 
}

void GameManager::cleanupSingleton()
{
    if(singleton != nullptr)
    {
        singleton->cleanup();
        delete singleton;
        singleton = nullptr;
    }
}

GameManager::GameManager()
{

}


void GameManager::init()
{
    GreenButton = std::make_unique<Button>();
    GreenButton->init(GREEN_BUTTON_PIN);

    RedButton = std::make_unique<Button>();
    RedButton->init(RED_BUTTON_PIN);

    BlueButton = std::make_unique<Button>();
    BlueButton->init(BLUE_BUTTON_PIN);

    WhiteButton = std::make_unique<Button>();
    WhiteButton->init(WHITE_BUTTON_PIN);

    RemoteWhiteButton = std::make_unique<Button>();
    RemoteWhiteButton->init(REMOTE_WHITE_BUTTON_PIN);

    RemoteBlackButton = std::make_unique<Button>();
    RemoteBlackButton->init(REMOTE_BLACK_BUTTON_PIN);

    Screen = std::make_shared<Adafruit_GC9A01A>(SCREEN_CS, SCREEN_DC, SCREEN_SDA, SCREEN_SCL, SCREEN_RST);
    Screen->begin();
    Screen->setRotation(0);

    screenDrawer.setScreenRef(Screen);

    RingLEDs = std::make_unique<HSVStrip>(RING_LED_LENGTH, RING_LED_PIN, NEO_GRB + NEO_KHZ800);
    OutfitLEDs = std::make_unique<HSVStrip>(OUTFIT_LED_LENGTH, OUTFIT_LED_PIN, NEO_GRB + NEO_KHZ800);
    GlassesLEDs = std::make_unique<HSVStrip>(GLASSES_LED_LENGTH, GLASSES_LED_PIN, NEO_GRB + NEO_KHZ800);
    BoardLED = std::make_unique<HSVStrip>(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

    setGlobalBrightness(EBrightness::HIGH);
}

void GameManager::tick(float deltaTime)
{
    GreenButton->tick(deltaTime);
    RedButton->tick(deltaTime);
    BlueButton->tick(deltaTime);
    WhiteButton->tick(deltaTime);

    RemoteWhiteButton->tick(deltaTime);
    RemoteBlackButton->tick(deltaTime);
}

void GameManager::showLeds()
{
    RingLEDs->show();
    BoardLED->show();

    if(bHasOutfitConnected)
    {
        OutfitLEDs->show();
        GlassesLEDs->show();
    }
}

EBrightness GameManager::getGlobalBrightness() const
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

void GameManager::cleanup()
{
    GreenButton.reset();
    RedButton.reset();
    BlueButton.reset();
    WhiteButton.reset();

    RemoteWhiteButton.reset();
    RemoteBlackButton.reset();

    RingLEDs.reset();
    OutfitLEDs.reset();
    GlassesLEDs.reset();
    BoardLED.reset();

    Screen.reset();
}