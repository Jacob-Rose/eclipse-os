// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

// The relics a desk can shadow, one profile each. This is the only file on
// the desk that knows a sculpture's IO class by name: the shadow itself is
// generic, and a new relic is a new entry here plus its looks in
// makeXLook(). Kept out of relic_shadow.cpp so that file stays free of any
// particular sculpture.

#include "edmx/relic_shadow.h"

#include "edmx/state_machine.h"
#include "relics/obelisk/obelisk.h"

namespace
{
    std::unique_ptr<eio::RelicIO> makeObeliskIO()
    {
        std::unique_ptr<obelisk::ObeliskIO> io(new obelisk::ObeliskIO());
        io->init();
        return std::unique_ptr<eio::RelicIO>(io.release());
    }

    const edmx::RelicShadowProfile kObelisk = {
        "obelisk",
        eio::NodeSpace::Obelisk,
        &makeObeliskIO,
        &edmx::makeObeliskLook,
    };

    const edmx::RelicShadowProfile* const kProfiles[] = {
        &kObelisk,
    };
}

const edmx::RelicShadowProfile* edmx::findShadowProfile(const std::string& relic)
{
    for (const RelicShadowProfile* profile : kProfiles)
    {
        if (profile->relic == relic)
        {
            return profile;
        }
    }
    return nullptr;
}
