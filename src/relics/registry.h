// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <eanim/registry.h>

/// Every look the relics ship, by name, into a registry. The desk lists and
/// makes them from this; a relic that wants to pick a look by name (off a
/// wire, a tag, a config) calls it once at start and asks the registry.
///
///     registerRelicGenerators(eanim::GeneratorRegistry::shared());
///
/// A look with an init() is inited here, so what comes out is ready to tick.
void registerRelicGenerators(eanim::GeneratorRegistry& registry);
