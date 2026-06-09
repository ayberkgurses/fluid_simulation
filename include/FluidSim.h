#pragma once

#include <vector>

class FluidSim {
public:
    FluidSim(int width, int height);
    ~FluidSim();

    void reset();
    void step(float dt);
    void addDensity(int x, int y, float amount);
    void addVelocity(int x, int y, float amountX, float amountY);
    const std::vector<float>& getDensity() const;
    int getWidth() const;
    int getHeight() const;

private:
    int width;
    int height;
    int size;

    std::vector<float> density;
    std::vector<float> densityPrev;
    std::vector<float> velX;
    std::vector<float> velY;
    std::vector<float> velXPrev;
    std::vector<float> velYPrev;

    int idx(int x, int y) const;

    void addSource(std::vector<float>& field, const std::vector<float>& source, float dt);
    void diffuse(int b, std::vector<float>& field, const std::vector<float>& prevField, float diff, float dt);
    void advect(int b, std::vector<float>& field, const std::vector<float>& prevField, const std::vector<float>& velX, const std::vector<float>& velY, float dt);
    void project(std::vector<float>& velX, std::vector<float>& velY, std::vector<float>& p, std::vector<float>& div);
    void setBoundary(int b, std::vector<float>& field);
};
