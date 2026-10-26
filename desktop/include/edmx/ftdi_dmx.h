// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#if !defined(_WIN32)

#include <string>

///
/// libftdi, loaded at runtime, for talking to an Open DMX widget.
///
/// ### why not just use the serial port
///
/// Because it does not work, and it does not fail either — which is worse.
///
/// An Open DMX USB is a bare FTDI wired to an RS485 driver. There is no
/// firmware in it: the host clocks DMX's own 250000 8N2 and shapes the frame by
/// hand, break and all. Through the kernel's tty layer every one of those
/// pieces can be verified correct — the measured line rate is 249400, the
/// framing is 8N2, `TIOCSBRK` returns success — and the fixtures stay dark.
///
/// The reason is that the tty layer will not tell you when a frame has actually
/// left the chip. `tcdrain()` returns once the kernel has handed the bytes to
/// USB, not once the FTDI has clocked them onto the wire, and 513 bytes at
/// 250000 is 22.6ms of real transmission. Assert the next break before that
/// finishes — which any sane-looking frame rate will — and the break lands
/// *inside* the previous frame. A receiver reads that as a packet starting
/// mid-packet and discards it. Every frame is quietly wrong, forever, and the
/// rig is dark while every diagnostic says the port is fine.
///
/// Driving the chip directly is what every program that does this successfully
/// does: QLC+ and OLA both reach for libftdi, and this rig now sends the same
/// sequence they do. Measured against a real truss: the tty path is dark at
/// every frame rate tried, and this path lights it.
///
/// ### two things it fixes for free
///
///   - **Permissions.** `/dev/ttyUSB0` belongs to group `uucp` or `dialout`,
///     which the user is usually not in, and udev recreates the node on every
///     replug so any `chown` is undone. libftdi talks to the USB device node,
///     which `69-libftdi.rules` already tags `uaccess` — the logged-in user
///     gets an ACL automatically. No group, no sudo, nothing to redo.
///   - **Naming.** A widget is found by what it *is* rather than by which
///     `/dev/ttyUSBn` it happened to land on this boot.
///
/// The library is dlopen'd rather than linked, for the same reason libasound is
/// in edmx/alsa_seq.h: this binary must build and run on a machine that has
/// neither. Without it, the serial path is still there — see
/// EnttecOpenOutput::open, which says so out loud when it falls back.
///

namespace edmx
{
    /// The slice of libftdi1 this needs, as function pointers. Named after the
    /// library's own symbols with the `ftdi_` prefix dropped.
    ///
    /// Every handle is `void*`. `struct ftdi_context` is opaque in practice and
    /// we only ever pass it back, so there is nothing to gain from declaring
    /// it and a real ABI risk in getting it wrong.
    struct FtdiApi
    {
        void* (*newContext)();
        void  (*freeContext)(void* ftdi);

        int   (*usbOpen)(void* ftdi, int vendor, int product);
        int   (*usbOpenDesc)(void* ftdi, int vendor, int product,
                             const char* description, const char* serial);
        int   (*usbClose)(void* ftdi);
        int   (*usbReset)(void* ftdi);

        int   (*setBaudRate)(void* ftdi, int baudrate);
        int   (*setLineProperty)(void* ftdi, int bits, int stopBits, int parity);
        int   (*setLineProperty2)(void* ftdi, int bits, int stopBits, int parity, int breakType);
        int   (*setFlowControl)(void* ftdi, int flowControl);
        int   (*setRts)(void* ftdi, int state);
        int   (*purgeBuffers)(void* ftdi);

        int   (*writeData)(void* ftdi, const unsigned char* buffer, int size);
        const char* (*errorString)(void* ftdi);

        /// The library, or null on a machine without it. Loaded once and never
        /// unloaded, exactly as AlsaSeq::get() does it.
        static const FtdiApi* get();
    };

    namespace ftdi
    {
        /// FTDI's own constants. ABI, not preference.
        constexpr int kBits8      = 8;
        constexpr int kStopBits2  = 2;
        constexpr int kParityNone = 0;
        constexpr int kBreakOff   = 0;
        constexpr int kBreakOn    = 1;
        constexpr int kNoFlowControl = 0;

        /// FTDI's vendor id, and the product id every Open DMX widget seen so
        /// far reports. Both are the stock values - these widgets do not
        /// bother programming an identity of their own, which is exactly why
        /// they cannot be told apart from a plain USB-serial cable by USB id.
        constexpr int kVendorFtdi = 0x0403;
        constexpr int kProductFt232 = 0x6001;

        /// The serial number of the device behind a tty path, read out of
        /// sysfs — `/dev/ttyUSB0` to `"BG00UIPI"`. Empty when it cannot be
        /// worked out, which is not fatal: with one widget attached, opening
        /// the first FTDI is the same answer.
        ///
        /// This is what keeps `midi.port`-style explicitness working. Two
        /// widgets on one machine are told apart by serial, and the config can
        /// still name a `/dev/ttyUSBn` it knows.
        std::string serialForTtyPath(const std::string& path);
    }
}

#endif // !_WIN32
