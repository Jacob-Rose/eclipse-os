#pragma once

// Forward declare CRGB if FastLED is available
#ifdef FASTLED_VERSION
#include <FastLED.h>
#else
// Fallback RGB struct if FastLED not included
struct CRGB {
    unsigned char r, g, b;
    CRGB() : r(0), g(0), b(0) {}
    CRGB(unsigned char red, unsigned char green, unsigned char blue) : r(red), g(green), b(blue) {}
};
#endif

#include "event_manager.h"

namespace ecore
{
    /**
     * ColorEvent - Event with color data for LED/visual feedback
     */
    struct ColorEvent : public Event {
        CRGB color;

        ColorEvent(const Name& eventName, CRGB eventColor, void* data = nullptr)
            : Event(eventName, data), color(eventColor) {}

        // Overload for const char*
        ColorEvent(const char* eventName, CRGB eventColor, void* data = nullptr)
            : Event(eventName, data), color(eventColor) {}
    };
}