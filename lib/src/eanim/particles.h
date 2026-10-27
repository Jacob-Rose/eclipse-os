// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstdint>
#include <vector>

#include <ecore/coord.h>

namespace eanim
{
    /* @brief One moving point in the 2d stage space.
    *
    * Colour and shape are the owning look's business; this is only
    * position, motion and life.
    */
    struct Particle
    {
        bool bAlive{false};

        ecore::Coordinate position;
        ecore::Coordinate velocity;

        /// how far the glow reaches, in stage units
        float radius{1.0f};

        float age{0.0f};
        /// seconds to live; zero or less means until the look kills it
        float lifetime{0.0f};

        /// per-particle noise seed, fixed at spawn
        uint32_t seed{0};
    };


    /* @brief A fixed pool of particles.
    *
    * tick() moves and ages them and expires the mortal; the look reads all()
    * to draw them, usually through glowAt().
    */
    class ParticleSystem
    {
    public:
        explicit ParticleSystem(int inCapacity);

        /// Integrates velocity, ages everyone, expires the mortal.
        void tick(float deltaTime);

        /// A dead slot brought to life, zeroed except for a fresh seed, or
        /// nullptr when the pool is full.
        Particle* spawn();

        void clear();

        std::vector<Particle>& all() { return particles; }
        const std::vector<Particle>& all() const { return particles; }

        int aliveCount() const;

        /// The particle's glow at a point: 1 at the centre falling to 0 at
        /// the radius, squared for a soft edge. Zero for a dead particle.
        static float glowAt(const Particle& particle, const ecore::Coordinate& at);

    private:
        std::vector<Particle> particles;
        uint32_t spawned{0};
    };
}
