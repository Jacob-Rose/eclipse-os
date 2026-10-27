// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.
//
// Bench for the four firmware transports on a bare Pico, no transceivers:
// DMX out, DMX in, MIDI over a UART, MIDI over USB. Two jumper wires:
//
//   GP0 -> GP5   DMX:  uart0 TX feeds uart1 RX
//   GP0 -> GP1   MIDI: uart0 TX feeds uart0 RX
//
// GP0 is the only driver, so both jumpers can stay on. Build and flash:
//
//   arduino-cli compile -b rp2040:rp2040:rpipicow:usbstack=tinyusb --library lib tools/bench-transports
//   arduino-cli upload  -b rp2040:rp2040:rpipicow:usbstack=tinyusb -p COMx tools/bench-transports
//
// then open the serial monitor at 115200. Phases 1 and 2 print PASS or FAIL
// on their own; phase 3 needs a MIDI monitor on the PC: the Pico shows up as a
// USB MIDI device, sends a note every two seconds, and echoes back whatever it
// is sent. Without the tinyusb stack option phase 3 is skipped.

#include <eclipse-os.h>

#include <edmx/uart_input.h>
#include <edmx/uart_output.h>
#include <emidi/message.h>
#include <emidi/transport.h>
#include <emidi/uart_transport.h>
#include <emidi/usb_transport.h>

using namespace ecore;

namespace
{
    constexpr edmx::DmxWiring kDmxOut{0, 0, -1};        // uart0, TX GP0, no enable pin
    constexpr edmx::DmxInputWiring kDmxIn{1, 5};        // uart1, RX GP5
    constexpr emidi::MidiWiring kMidiUart{0, 1, 0};     // uart0, RX GP1, TX GP0
    constexpr int kDmxFrames = 100;                     // ~2.5 s at 40 Hz
    constexpr int kDmxChannels = 512;

    struct Recorder : public emidi::MessageSink
    {
        std::vector<emidi::Message> seen;
        void onMessage(const emidi::Message& message, double) override { seen.push_back(message); }
    };

    void say(const char* text) { Serial.println(text); }

    bool benchDmx()
    {
        say("--- phase 1: DMX out (uart0 GP0) -> DMX in (uart1 GP5)");
        edmx::UartDmxOutput out(kDmxOut);
        edmx::UartDmxInput in(kDmxIn);
        std::string error;
        if (!out.open(error)) { Serial.printf("FAIL dmx out open: %s\n", error.c_str()); return false; }
        if (!in.open(error)) { Serial.printf("FAIL dmx in open: %s\n", error.c_str()); out.close(); return false; }
        out.setUniverseLength(kDmxChannels);

        edmx::ChannelBuffer frame(kDmxChannels);
        int matched = 0;
        int mismatched = 0;
        for (int n = 0; n < kDmxFrames; ++n)
        {
            for (int channel = 1; channel <= kDmxChannels; ++channel)
            {
                frame.setChannel(channel, static_cast<uint8_t>((channel + n) & 0xFF));
            }
            if (!out.sendFrame(frame, error)) { Serial.printf("FAIL sendFrame: %s\n", error.c_str()); }

            // a frame on the wire takes ~23 ms; drain the input meanwhile
            const uint32_t until = millis() + 25;
            while (millis() < until)
            {
                if (in.tick())
                {
                    // the completed frame is the previous one sent (the break
                    // that ends it opens the next), so compare against n - 1
                    int bad = 0;
                    for (int channel = 1; channel <= kDmxChannels; ++channel)
                    {
                        const uint8_t expected = static_cast<uint8_t>((channel + n - 1) & 0xFF);
                        if (in.channels().getChannel(channel) != expected) ++bad;
                    }
                    if (bad == 0) ++matched; else ++mismatched;
                }
            }
        }
        Serial.printf("sent %lu, dropped %lu, received %lu, matched %d, mismatched %d\n",
                      static_cast<unsigned long>(out.framesSent()),
                      static_cast<unsigned long>(out.framesDropped()),
                      static_cast<unsigned long>(in.framesReceived()), matched, mismatched);
        in.close();
        out.close();
        const bool pass = matched >= kDmxFrames - 2 && mismatched == 0;
        say(pass ? "PASS dmx loopback" : "FAIL dmx loopback");
        return pass;
    }

    bool benchMidiUart()
    {
        say("--- phase 2: MIDI uart0 TX GP0 -> RX GP1");
        emidi::UartMidiTransport wire(kMidiUart);
        wire.begin();
        emidi::MidiPort port;
        port.setTransport(&wire);
        Recorder sink;

        const emidi::Message sent[] = {
            emidi::Message(0x90, 60, 100),
            emidi::Message(0xB0, 7, 127),
            emidi::Message(emidi::status::Clock),
            emidi::Message(0xC0, 5),
            emidi::Message(0x80, 60, 0),
        };
        constexpr int kCount = sizeof(sent) / sizeof(sent[0]);
        for (const auto& message : sent) port.send(message);

        const uint32_t until = millis() + 200;
        while (millis() < until) port.tick(millis() / 1000.0, sink);

        bool pass = sink.seen.size() == static_cast<size_t>(kCount);
        for (int i = 0; pass && i < kCount; ++i)
        {
            pass = sink.seen[i].status == sent[i].status
                && sink.seen[i].data1 == sent[i].data1
                && sink.seen[i].data2 == sent[i].data2;
        }
        Serial.printf("sent %d messages, read %lu bytes, parsed %u messages\n",
                      kCount, static_cast<unsigned long>(port.getBytesRead()),
                      static_cast<unsigned>(sink.seen.size()));
        wire.end();
        say(pass ? "PASS midi uart loopback" : "FAIL midi uart loopback");
        return pass;
    }

#if defined(USE_TINYUSB)
    emidi::UsbMidiTransport usbWire;
    emidi::MidiPort usbPort;
    struct Echo : public emidi::MessageSink
    {
        void onMessage(const emidi::Message& message, double) override
        {
            Serial.printf("usb rx %02X %02X %02X\n", message.status, message.data1, message.data2);
            usbPort.send(message);
        }
    } echo;
    uint32_t nextNote = 0;
    bool noteOn = false;

    void benchUsbMidiTick()
    {
        usbPort.tick(millis() / 1000.0, echo);
        if (millis() >= nextNote)
        {
            nextNote = millis() + 2000;
            noteOn = !noteOn;
            usbPort.send(emidi::Message(noteOn ? 0x90 : 0x80, 60, noteOn ? 100 : 0));
        }
    }
#endif
}

void setup()
{
#if defined(USE_TINYUSB)
    usbWire.begin();          // before Serial so the descriptor carries MIDI + CDC
    usbPort.setTransport(&usbWire);
#endif
    Serial.begin(115200);
    while (!Serial && millis() < 5000) {}
    say("eclipse-os transport bench");

    const bool dmx = benchDmx();
    const bool midi = benchMidiUart();
    Serial.printf("=== %s: dmx %s, midi uart %s\n", (dmx && midi) ? "PASS" : "FAIL",
                  dmx ? "ok" : "FAIL", midi ? "ok" : "FAIL");

#if defined(USE_TINYUSB)
    say("--- phase 3: USB MIDI; watch a MIDI monitor, send it anything to hear it back");
#else
    say("--- phase 3 skipped: build with usbstack=tinyusb for USB MIDI");
#endif
}

void loop()
{
#if defined(USE_TINYUSB)
    benchUsbMidiTick();
#endif
    delay(1);
}
