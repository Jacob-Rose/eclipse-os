// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/alsa_seq.h"

#if !defined(_WIN32)

#include <dlfcn.h>
#include <type_traits>

using namespace edmx;

namespace
{
    /// The soname, not the linker name. `libasound.so` only exists when the dev
    /// package is installed; `libasound.so.2` is what ships with the runtime,
    /// which is the machine we are actually trying to run on.
    const char* kLibrary = "libasound.so.2";

    /// Every symbol must resolve or the table is refused wholesale. A partly
    /// loaded API is worse than none: it fails somewhere further in, on a
    /// machine we are not standing at.
    bool bindAll(void* library, AlsaSeq& api)
    {
        bool ok = true;

        const auto bind = [&library, &ok](auto& slot, const char* name) {
            void* symbol = dlsym(library, name);
            if (symbol == nullptr)
            {
                ok = false;
                return;
            }
            // The cast alsa's own callers make implicitly by including a
            // header. dlsym hands back void*, so it is spelled out here.
            slot = reinterpret_cast<typename std::remove_reference<decltype(slot)>::type>(symbol);
        };

        bind(api.open,                 "snd_seq_open");
        bind(api.close,                "snd_seq_close");
        bind(api.setClientName,        "snd_seq_set_client_name");
        bind(api.clientId,             "snd_seq_client_id");
        bind(api.nonblock,             "snd_seq_nonblock");

        bind(api.createSimplePort,     "snd_seq_create_simple_port");
        bind(api.deleteSimplePort,     "snd_seq_delete_simple_port");
        bind(api.connectFrom,          "snd_seq_connect_from");

        bind(api.pollDescriptorsCount, "snd_seq_poll_descriptors_count");
        bind(api.pollDescriptors,      "snd_seq_poll_descriptors");
        bind(api.eventInput,           "snd_seq_event_input");

        bind(api.clientInfoMalloc,     "snd_seq_client_info_malloc");
        bind(api.clientInfoFree,       "snd_seq_client_info_free");
        bind(api.clientInfoSetClient,  "snd_seq_client_info_set_client");
        bind(api.clientInfoGetClient,  "snd_seq_client_info_get_client");
        bind(api.clientInfoGetName,    "snd_seq_client_info_get_name");
        bind(api.queryNextClient,      "snd_seq_query_next_client");

        bind(api.portInfoMalloc,       "snd_seq_port_info_malloc");
        bind(api.portInfoFree,         "snd_seq_port_info_free");
        bind(api.portInfoSetClient,    "snd_seq_port_info_set_client");
        bind(api.portInfoSetPort,      "snd_seq_port_info_set_port");
        bind(api.portInfoGetPort,      "snd_seq_port_info_get_port");
        bind(api.portInfoGetName,      "snd_seq_port_info_get_name");
        bind(api.portInfoGetCapability,"snd_seq_port_info_get_capability");
        bind(api.queryNextPort,        "snd_seq_query_next_port");

        bind(api.midiEventNew,         "snd_midi_event_new");
        bind(api.midiEventFree,        "snd_midi_event_free");
        bind(api.midiEventNoStatus,    "snd_midi_event_no_status");
        bind(api.midiEventDecode,      "snd_midi_event_decode");

        return ok;
    }
}

const AlsaSeq* AlsaSeq::get()
{
    // Function-local statics initialise once, thread-safely, on first use. The
    // handle is deliberately never dlclose'd: unloading a library a reader
    // thread might still be parked inside is a crash, and the process is going
    // away anyway.
    static const AlsaSeq* loaded = []() -> const AlsaSeq* {
        void* library = dlopen(kLibrary, RTLD_LAZY | RTLD_LOCAL);
        if (library == nullptr)
        {
            return nullptr;
        }

        static AlsaSeq api{};
        if (!bindAll(library, api))
        {
            return nullptr;
        }
        return &api;
    }();

    return loaded;
}

#endif // !_WIN32
