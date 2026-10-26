// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "particles.h"

#include <cmath>

#include "../ecore/math.h"

using namespace eanim;

ParticleSystem::ParticleSystem(int inCapacity)
    : particles(static_cast<size_t>(inCapacity > 0 ? inCapacity : 1))
{
}

void ParticleSystem::tick(float deltaTime)
{
    for (Particle& particle : particles)
    {
        if (!particle.bAlive)
        {
            continue;
        }

        particle.position.x += particle.velocity.x * deltaTime;
        particle.position.y += particle.velocity.y * deltaTime;
        particle.age += deltaTime;

        if (particle.lifetime > 0.0f && particle.age >= particle.lifetime)
        {
            particle.bAlive = false;
        }
    }
}

Particle* ParticleSystem::spawn()
{
    for (Particle& particle : particles)
    {
        if (!particle.bAlive)
        {
            particle = Particle{};
            particle.bAlive = true;

            // a fresh seed every spawn, so a recycled slot does not replay
            // its previous life's noise
            particle.seed = ++spawned * 2654435761u;
            return &particle;
        }
    }
    return nullptr;
}

void ParticleSystem::clear()
{
    for (Particle& particle : particles)
    {
        particle = Particle{};
    }
}

int ParticleSystem::aliveCount() const
{
    int count = 0;
    for (const Particle& particle : particles)
    {
        count += particle.bAlive ? 1 : 0;
    }
    return count;
}

float ParticleSystem::glowAt(const Particle& particle, const ecore::Coordinate& at)
{
    if (!particle.bAlive || particle.radius <= 0.0f)
    {
        return 0.0f;
    }

    const float dx = at.x - particle.position.x;
    const float dy = at.y - particle.position.y;
    const float distance = std::sqrt(dx * dx + dy * dy);

    const float falloff = 1.0f - distance / particle.radius;
    return (falloff > 0.0f) ? falloff * falloff : 0.0f;
}
