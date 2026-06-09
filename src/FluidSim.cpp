#include "FluidSim.h"
#include <algorithm>
#include <cmath>

FluidSim::FluidSim(int width, int height)
    : width(width)
    , height(height)
    , size(width * height)
    , density(size, 0.0f)
    , densityPrev(size, 0.0f)
    , velX(size, 0.0f)
    , velY(size, 0.0f)
    , velXPrev(size, 0.0f)
    , velYPrev(size, 0.0f)
{
}

FluidSim::~FluidSim() = default;

void FluidSim::reset() {
    std::fill(density.begin(), density.end(), 0.0f);
    std::fill(densityPrev.begin(), densityPrev.end(), 0.0f);
    std::fill(velX.begin(), velX.end(), 0.0f);
    std::fill(velY.begin(), velY.end(), 0.0f);
    std::fill(velXPrev.begin(), velXPrev.end(), 0.0f);
    std::fill(velYPrev.begin(), velYPrev.end(), 0.0f);
}

void FluidSim::step(float dt) {
    // 1) Add sources from previous frame
    addSource(density, densityPrev, dt);
    addSource(velX, velXPrev, dt);
    addSource(velY, velYPrev, dt);

    // 2) Diffuse velocity
    diffuse(1, velXPrev, velX, 0.0001f, dt);
    diffuse(2, velYPrev, velY, 0.0001f, dt);

    // 3) Project velocity to make it divergence-free
    project(velXPrev, velYPrev, densityPrev, density);

    // 4) Advect velocity
    advect(1, velX, velXPrev, velXPrev, velYPrev, dt);
    advect(2, velY, velYPrev, velXPrev, velYPrev, dt);

    // 5) Project again after advection
    project(velX, velY, densityPrev, density);

    // 6) Diffuse density
    diffuse(0, densityPrev, density, 0.0001f, dt);
    advect(0, density, densityPrev, velX, velY, dt);

    // Reset sources for the next frame
    std::fill(densityPrev.begin(), densityPrev.end(), 0.0f);
    std::fill(velXPrev.begin(), velXPrev.end(), 0.0f);
    std::fill(velYPrev.begin(), velYPrev.end(), 0.0f);
}

void FluidSim::addDensity(int x, int y, float amount) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    density[idx(x, y)] += amount;
}

void FluidSim::addVelocity(int x, int y, float amountX, float amountY) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    velX[idx(x, y)] += amountX;
    velY[idx(x, y)] += amountY;
}

const std::vector<float>& FluidSim::getDensity() const {
    return density;
}

int FluidSim::getWidth() const {
    return width;
}

int FluidSim::getHeight() const {
    return height;
}

int FluidSim::idx(int x, int y) const {
    return x + y * width;
}

void FluidSim::addSource(std::vector<float>& field, const std::vector<float>& source, float dt) {
    // TODO: Add source values to the field for each grid cell.
    // This is typically: field[i] += dt * source[i]
    for (int i = 0; i < size; ++i) {
        field[i] += dt * source[i];
    }
}

void FluidSim::diffuse(int b, std::vector<float>& field, const std::vector<float>& prevField, float diff, float dt) {
    // TODO: Implement diffusion step using Gauss-Seidel relaxation.
    // Use the boundary parameter `b` to set the correct fields on edges.
    // The algorithm smooths out the field values over the grid.
    field = prevField;
    setBoundary(b, field);
}

void FluidSim::advect(int b, std::vector<float>& field, const std::vector<float>& prevField, const std::vector<float>& velX, const std::vector<float>& velY, float dt) {
    // TODO: Implement advection of field using the velocity field.
    // Trace each cell backward through the velocity field to sample `prevField`.
    field = prevField;
    setBoundary(b, field);
}

void FluidSim::project(std::vector<float>& velX, std::vector<float>& velY, std::vector<float>& p, std::vector<float>& div) {
    // TODO: Project the velocity field to make it incompressible.
    // Calculate divergence, solve for pressure, and subtract gradient.
    std::fill(p.begin(), p.end(), 0.0f);
    std::fill(div.begin(), div.end(), 0.0f);
}

void FluidSim::setBoundary(int b, std::vector<float>& field) {
    // TODO: Apply boundary conditions to `field` depending on `b`.
    // b == 0: scalar field (density)
    // b == 1: horizontal velocity
    // b == 2: vertical velocity
    for (int x = 1; x < width - 1; ++x) {
        field[idx(x, 0)] = field[idx(x, 1)];
        field[idx(x, height - 1)] = field[idx(x, height - 2)];
    }
    for (int y = 1; y < height - 1; ++y) {
        field[idx(0, y)] = field[idx(1, y)];
        field[idx(width - 1, y)] = field[idx(width - 2, y)];
    }
    field[idx(0, 0)] = 0.5f * (field[idx(1, 0)] + field[idx(0, 1)]);
    field[idx(0, height - 1)] = 0.5f * (field[idx(1, height - 1)] + field[idx(0, height - 2)]);
    field[idx(width - 1, 0)] = 0.5f * (field[idx(width - 2, 0)] + field[idx(width - 1, 1)]);
    field[idx(width - 1, height - 1)] = 0.5f * (field[idx(width - 2, height - 1)] + field[idx(width - 1, height - 2)]);
}
