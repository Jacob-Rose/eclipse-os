// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/ftdi_dmx.h"

#if !defined(_WIN32)

#include <cstdio>
#include <dlfcn.h>
#include <fstream>
#include <type_traits>

using namespace edmx;

namespace
{
    /// The soname. `libftdi1.so` only exists with the dev package installed;
    /// `.so.2` is what ships with the runtime, and the runtime is the machine
    /// we are trying to light a rig from.
    const char* kLibrary = "libftdi1.so.2";

    /// All or nothing, for the reason given in alsa_seq.cpp: a partly bound
    /// table fails somewhere further in, on a machine nobody is standing at.
    bool bindAll(void* library, FtdiApi& api)
    {
        bool ok = true;

        const auto bind = [&library, &ok](auto& slot, const char* name) {
            void* symbol = dlsym(library, name);
            if (symbol == nullptr)
            {
                ok = false;
                return;
            }
            slot = reinterpret_cast<typename std::remove_reference<decltype(slot)>::type>(symbol);
        };

        bind(api.newContext,       "ftdi_new");
        bind(api.freeContext,      "ftdi_free");

        bind(api.usbOpen,          "ftdi_usb_open");
        bind(api.usbOpenDesc,      "ftdi_usb_open_desc");
        bind(api.usbClose,         "ftdi_usb_close");
        bind(api.usbReset,         "ftdi_usb_reset");

        bind(api.setBaudRate,      "ftdi_set_baudrate");
        bind(api.setLineProperty,  "ftdi_set_line_property");
        bind(api.setLineProperty2, "ftdi_set_line_property2");
        bind(api.setFlowControl,   "ftdi_setflowctrl");
        bind(api.setRts,           "ftdi_setrts");

        // ftdi_tcioflush is the 1.5 name; ftdi_usb_purge_buffers is the older
        // one and is still exported (deprecated). Either will do, so take
        // whichever this copy has rather than refusing over a rename.
        api.purgeBuffers = reinterpret_cast<int (*)(void*)>(dlsym(library, "ftdi_tcioflush"));
        if (api.purgeBuffers == nullptr)
        {
            bind(api.purgeBuffers, "ftdi_usb_purge_buffers");
        }

        bind(api.writeData,        "ftdi_write_data");
        bind(api.errorString,      "ftdi_get_error_string");

        return ok;
    }
}

const FtdiApi* FtdiApi::get()
{
    static const FtdiApi* loaded = []() -> const FtdiApi* {
        void* library = dlopen(kLibrary, RTLD_LAZY | RTLD_LOCAL);
        if (library == nullptr)
        {
            return nullptr;
        }

        static FtdiApi api{};
        if (!bindAll(library, api))
        {
            return nullptr;
        }
        return &api;
    }();

    return loaded;
}

std::string edmx::ftdi::serialForTtyPath(const std::string& path)
{
    // /dev/ttyUSB0 -> /sys/class/tty/ttyUSB0/device, which is the *interface*.
    // The serial number lives on the USB device two levels up. Walking there
    // with "../.." rather than resolving symlinks keeps this to one open().
    const size_t slash = path.find_last_of('/');
    const std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    if (name.empty())
    {
        return std::string();
    }

    const std::string serialPath = "/sys/class/tty/" + name + "/device/../../serial";

    std::ifstream file(serialPath);
    if (!file)
    {
        return std::string();
    }

    std::string serial;
    std::getline(file, serial);
    return serial;
}

#endif // !_WIN32
