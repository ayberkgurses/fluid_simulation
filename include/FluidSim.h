#pragma once

#include <vector>
#include <cstdint>

struct Particle {
    float x, y, z;
    float vx, vy, vz;
    float density;
};

class FluidSim {
public:
    FluidSim();
    ~FluidSim();

    void reset();
    void step(float dt);
    void spawnBlock(float cx, float cy, float cz, int cols, int rows, int layers, float spacing);

    const std::vector<Particle>& getParticles()    const { return particles; }
    std::vector<Particle>&       getParticlesMut()       { return particles; }

    float boundsHalfX = 0.95f;
    float boundsHalfY = 0.95f;
    float boundsHalfZ = 0.95f;

    float smoothingRadius      = 0.20f;
    float targetDensity        = 1500.0f;
    float pressureMultiplier   = 200.0f;
    float nearPressureMultiplier = 15.0f;
    float viscosityStrength    = 0.2f;
    float gravity              = -9.8f;
    float collisionDamping     = 1.0f;

private:
    std::vector<Particle> particles;

    // Spatial hash
    int   numCells;
    std::vector<uint32_t> cellKeys;
    std::vector<uint32_t> particleIndices;
    std::vector<uint32_t> cellStart;

    // Predicted positions
    std::vector<float> predX, predY, predZ;

    void buildSpatialHash();
    void computeDensities();
    void computePressureAndViscosity(float dt);
    void resolveCollision(Particle& p);

    uint32_t cellHash(int cx, int cy, int cz) const;
    void posToCell(float x, float y, float z, int& cx, int& cy, int& cz) const;

    // Kernels
    float spikyPow2(float dst) const;
    float spikyPow2Deriv(float dst) const;
    float spikyPow3(float dst) const;
    float spikyPow3Deriv(float dst) const;
    float poly6(float dst) const;
};
