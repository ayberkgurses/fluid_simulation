#include "FluidSim.h"
#include <algorithm>
#include <cmath>

static constexpr float PI = 3.14159265358979323846f;

FluidSim::FluidSim() : numCells(0) {}
FluidSim::~FluidSim() = default;

void FluidSim::reset() {
    particles.clear();
    predX.clear(); predY.clear(); predZ.clear();
}

void FluidSim::spawnBlock(float cx, float cy, float cz,
                          int cols, int rows, int layers, float spacing) {
    float ox = cx - (cols  - 1) * spacing * 0.5f;
    float oy = cy - (rows  - 1) * spacing * 0.5f;
    float oz = cz - (layers - 1) * spacing * 0.5f;
    for (int l = 0; l < layers; ++l)
    for (int r = 0; r < rows;   ++r)
    for (int c = 0; c < cols;   ++c) {
        Particle p{};
        p.x = ox + c * spacing;
        p.y = oy + r * spacing;
        p.z = oz + l * spacing;
        particles.push_back(p);
    }
    int n = (int)particles.size();
    predX.resize(n); predY.resize(n); predZ.resize(n);
    cellKeys.resize(n); particleIndices.resize(n);
}

// ---- Kernels (3D normalized) ----
float FluidSim::spikyPow2(float dst) const {
    if (dst >= smoothingRadius) return 0.0f;
    float v = smoothingRadius - dst;
    return v * v * (15.0f / (2.0f * PI * std::pow(smoothingRadius, 5)));
}

float FluidSim::spikyPow2Deriv(float dst) const {
    if (dst <= 1e-8f || dst >= smoothingRadius) return 0.0f;
    float v = smoothingRadius - dst;
    return -v * (15.0f / (PI * std::pow(smoothingRadius, 5)));
}

float FluidSim::spikyPow3(float dst) const {
    if (dst >= smoothingRadius) return 0.0f;
    float v = smoothingRadius - dst;
    return v * v * v * (15.0f / (PI * std::pow(smoothingRadius, 6)));
}

float FluidSim::spikyPow3Deriv(float dst) const {
    if (dst <= 1e-8f || dst >= smoothingRadius) return 0.0f;
    float v = smoothingRadius - dst;
    return -v * v * (45.0f / (PI * std::pow(smoothingRadius, 6)));
}

float FluidSim::poly6(float dst) const {
    if (dst >= smoothingRadius) return 0.0f;
    float v = smoothingRadius * smoothingRadius - dst * dst;
    return v * v * v * (315.0f / (64.0f * PI * std::pow(smoothingRadius, 9)));
}

// ---- Spatial hash ----

void FluidSim::posToCell(float x, float y, float z, int& cx, int& cy, int& cz) const {
    cx = (int)std::floor(x / smoothingRadius);
    cy = (int)std::floor(y / smoothingRadius);
    cz = (int)std::floor(z / smoothingRadius);
}

uint32_t FluidSim::cellHash(int cx, int cy, int cz) const {
    uint32_t a = (uint32_t)cx * 15823u;
    uint32_t b = (uint32_t)cy * 9737333u;
    uint32_t c = (uint32_t)cz * 440817757u;
    return (a + b + c) % (uint32_t)numCells;
}

void FluidSim::buildSpatialHash() {
    int n = (int)particles.size();
    numCells = n;
    cellKeys.resize(n);
    particleIndices.resize(n);
    cellStart.assign(numCells + 1, 0);

    for (int i = 0; i < n; ++i) {
        int cx, cy, cz;
        posToCell(predX[i], predY[i], predZ[i], cx, cy, cz);
        cellKeys[i] = cellHash(cx, cy, cz);
        particleIndices[i] = (uint32_t)i;
    }

    // Counting sort
    for (int i = 0; i < n; ++i) cellStart[cellKeys[i] + 1]++;
    for (int i = 1; i <= numCells; ++i) cellStart[i] += cellStart[i-1];

    std::vector<uint32_t> sorted(n);
    std::vector<uint32_t> offset = cellStart;
    for (int i = 0; i < n; ++i)
        sorted[offset[cellKeys[i]]++] = (uint32_t)i;
    particleIndices = sorted;
}

void FluidSim::computeDensities() {
    int n = (int)particles.size();
    for (int i = 0; i < n; ++i) {
        float density = 0.0f;
        int cx0, cy0, cz0;
        posToCell(predX[i], predY[i], predZ[i], cx0, cy0, cz0);
        for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            uint32_t key = cellHash(cx0+dx, cy0+dy, cz0+dz);
            for (uint32_t k = cellStart[key]; k < cellStart[key+1]; ++k) {
                int j = (int)particleIndices[k];
                float ox = predX[j]-predX[i], oy = predY[j]-predY[i], oz = predZ[j]-predZ[i];
                float dst = std::sqrt(ox*ox + oy*oy + oz*oz);
                density += spikyPow2(dst);
            }
        }
        particles[i].density = density;
    }
}

static float densityToPressure(float density, float targetDensity, float pressureMultiplier) {
    return (density - targetDensity) * pressureMultiplier;
}

void FluidSim::computePressureAndViscosity(float dt) {
    int n = (int)particles.size();

    // We need near-densities; compute both in one pass via a temp array
    std::vector<float> nearDens(n, 0.0f);
    for (int i = 0; i < n; ++i) {
        float nd = 0.0f;
        int cx0, cy0, cz0;
        posToCell(predX[i], predY[i], predZ[i], cx0, cy0, cz0);
        for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            uint32_t key = cellHash(cx0+dx, cy0+dy, cz0+dz);
            for (uint32_t k = cellStart[key]; k < cellStart[key+1]; ++k) {
                int j = (int)particleIndices[k];
                float ox = predX[j]-predX[i], oy = predY[j]-predY[i], oz = predZ[j]-predZ[i];
                float dst = std::sqrt(ox*ox + oy*oy + oz*oz);
                nd += spikyPow3(dst);
            }
        }
        nearDens[i] = nd;
    }

    for (int i = 0; i < n; ++i) {
        float pressure_i     = densityToPressure(particles[i].density, targetDensity, pressureMultiplier);
        float nearPressure_i = nearDens[i] * nearPressureMultiplier;

        float fx = 0.0f, fy = 0.0f, fz = 0.0f;

        int cx0, cy0, cz0;
        posToCell(predX[i], predY[i], predZ[i], cx0, cy0, cz0);

        for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            uint32_t key = cellHash(cx0+dx, cy0+dy, cz0+dz);
            for (uint32_t k = cellStart[key]; k < cellStart[key+1]; ++k) {
                int j = (int)particleIndices[k];
                if (j == i) continue;

                float ox = predX[j]-predX[i], oy = predY[j]-predY[i], oz = predZ[j]-predZ[i];
                float dst = std::sqrt(ox*ox + oy*oy + oz*oz);
                if (dst < 1e-8f) continue;

                float nx = ox/dst, ny = oy/dst, nz = oz/dst;

                // Pressure
                float pressure_j     = densityToPressure(particles[j].density, targetDensity, pressureMultiplier);
                float nearPressure_j = nearDens[j] * nearPressureMultiplier;

                float sharedPressure     = (pressure_i + pressure_j) * 0.5f;
                float sharedNearPressure = (nearPressure_i + nearPressure_j) * 0.5f;

                float dj = std::max(particles[j].density, 1e-6f);
                float pf = sharedPressure     * spikyPow2Deriv(dst) / dj;
                float nf = sharedNearPressure * spikyPow3Deriv(dst) / dj;
                fx += (pf + nf) * nx;
                fy += (pf + nf) * ny;
                fz += (pf + nf) * nz;

                // Viscosity
                float w = poly6(dst);
                fx += viscosityStrength * (particles[j].vx - particles[i].vx) * w;
                fy += viscosityStrength * (particles[j].vy - particles[i].vy) * w;
                fz += viscosityStrength * (particles[j].vz - particles[i].vz) * w;
            }
        }

        float di = std::max(particles[i].density, 1e-6f);
        particles[i].vx += fx / di * dt;
        particles[i].vy += fy / di * dt;
        particles[i].vz += fz / di * dt;
    }
}

void FluidSim::resolveCollision(Particle& p) {
    auto bounce = [&](float pos, float vel, float half, float& outPos, float& outVel) {
        if (pos < -half) { outPos = -half; outVel = std::abs(vel) * collisionDamping; }
        else if (pos > half) { outPos = half; outVel = -std::abs(vel) * collisionDamping; }
        else { outPos = pos; outVel = vel; }
    };
    bounce(p.x, p.vx, boundsHalfX, p.x, p.vx);
    bounce(p.y, p.vy, boundsHalfY, p.y, p.vy);
    bounce(p.z, p.vz, boundsHalfZ, p.z, p.vz);
}

void FluidSim::step(float dt) {
    int n = (int)particles.size();
    if (n == 0) return;

    predX.resize(n); predY.resize(n); predZ.resize(n);

    // Gravity + predict
    for (int i = 0; i < n; ++i) {
        particles[i].vy += gravity * dt;
        predX[i] = particles[i].x + particles[i].vx * dt;
        predY[i] = particles[i].y + particles[i].vy * dt;
        predZ[i] = particles[i].z + particles[i].vz * dt;
    }

    buildSpatialHash();
    computeDensities();
    computePressureAndViscosity(dt);

    // Integrate
    for (int i = 0; i < n; ++i) {
        particles[i].x += particles[i].vx * dt;
        particles[i].y += particles[i].vy * dt;
        particles[i].z += particles[i].vz * dt;
        resolveCollision(particles[i]);
    }
}
