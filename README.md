# 3D SPH Fluid Simulation

A real-time 3D fluid simulation built with OpenGL 3.3, GLFW, GLAD, and GLM. The fluid is simulated using Smoothed Particle Hydrodynamics (SPH) and rendered as depth-corrected sphere impostors inside a transparent glass container.

---

## Demo Video!

https://github.com/user-attachments/assets/61c9430f-aa3b-45e7-a0e1-ad6662865bda


---

## How to Build and Run

### Prerequisites
- CMake 3.16 or newer
- A C++17 compiler (Clang, GCC, or MSVC)
- An internet connection on first build (CMake fetches GLFW, GLAD, and GLM automatically)
- On macOS: Xcode command-line tools (`xcode-select --install`)

### Build

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Run

From inside the `build/` directory:

```bash
./fluid_simulation          # macOS / Linux
fluid_simulation.exe        # Windows
```

---

## Controls

| Input | Action |
|---|---|
| `Right-click drag` | Rotate camera |
| `M` | Toggle mouse-look (locks/releases cursor) |
| `W A S D` | Move camera forward / left / back / right |
| `Space` | Move camera up |
| `Left Shift` | Move camera down |
| Scroll wheel | Zoom in / out (adjusts field of view) |
| `Left-click drag` | Grab and drag a sphere of fluid particles |
| `F` | Inject a new 8×8×8 block of particles at the centre |
| `ESC` | Quit |

---

## Project Structure

```
fluid_simulation/
├── CMakeLists.txt       # Build system — fetches GLFW / GLAD / GLM via FetchContent
├── include/
│   └── FluidSim.h       # SPH simulation interface and Particle struct
└── src/
    ├── FluidSim.cpp     # SPH simulation implementation
    └── main.cpp         # Render loop, shaders, input, 3D scene
```

---

## Implementation

### Fluid Simulation — `FluidSim.h` / `FluidSim.cpp`

The simulation uses **Smoothed Particle Hydrodynamics (SPH)** on a set of 3D particles. Each particle stores position `(x, y, z)`, velocity `(vx, vy, vz)`, and a scalar `density`. One call to `step(dt)` runs a full physics tick. `main.cpp` calls `step` three times per frame (substeps) to improve stability.

#### Particle layout and initial conditions

`spawnBlock(cx, cy, cz, cols, rows, layers, spacing)` places a regular grid of particles centred on `(cx, cy, cz)`. The simulation starts with a 16×16×16 block (4 096 particles) near the top of the container and falls under gravity.

#### SPH kernels

All kernels are 3D-normalised so that integrating the kernel over all space yields 1. `h` denotes the smoothing radius.

| Kernel | Formula | Used for |
|---|---|---|
| `spikyPow2(r)` | `(h−r)² · 15 / (2π h⁵)` | density |
| `spikyPow2Deriv(r)` | `−(h−r) · 15 / (π h⁵)` | pressure gradient |
| `spikyPow3(r)` | `(h−r)³ · 15 / (π h⁶)` | near-density |
| `spikyPow3Deriv(r)` | `−(h−r)² · 45 / (π h⁶)` | near-pressure gradient |
| `poly6(r)` | `(h²−r²)³ · 315 / (64π h⁹)` | viscosity weight |

All kernels return 0 for `r ≥ h`.

#### Spatial hash

Finding neighbours naively is O(n²). Instead, particles are bucketed into a uniform grid of cell size `h` using a hash table of size `n` (one entry per particle).

1. Each particle's predicted position is mapped to a cell `(cx, cy, cz) = floor(pos / h)`.
2. The cell is hashed: `hash = (cx·15823 + cy·9737333 + cz·440817757) % n`. The three large primes spread cells across the table uniformly and minimise collisions between adjacent cells.
3. Particles are sorted into the table using a **counting sort** — O(n), no allocations beyond fixed-size arrays.
4. When iterating neighbours of particle `i`, the 27 surrounding cells `(cx±1, cy±1, cz±1)` are probed. All particles whose hash falls in a given cell bucket are tested for distance.

Because the hash table size equals the particle count, on average each bucket holds one particle and the total neighbour-search work is O(n · average neighbours).

#### Simulation step

Each call to `step(dt)` runs the following sequence:

**1. Predict positions**

Gravity is applied to velocity first, then a forward-Euler position prediction is stored in `predX/Y/Z`. The density and pressure passes use these predicted positions rather than the current ones — this is the **predict-correct** pattern that gives SPH its stability at larger timesteps.

```
vy += gravity * dt
pred = pos + vel * dt
```

**2. Build spatial hash**

The hash table is rebuilt from `predX/Y/Z` each step.

**3. Compute density**

For each particle `i`, density is the sum of `spikyPow2(|pred_i − pred_j|)` over all neighbours `j` within radius `h`.

**4. Compute pressure and viscosity**

For each pair `(i, j)` the following forces are accumulated into particle `i`'s velocity:

- **Pressure force** — repels overlapping particles. Pressure at particle `i` is `(density_i − targetDensity) · pressureMultiplier`. The shared pressure between `i` and `j` is their average. The force magnitude is `sharedPressure · spikyPow2Deriv(r) / density_j`, directed along the unit vector from `j` to `i`.

- **Near-pressure force** — a short-range repulsion that prevents particles from clumping at zero separation. It uses a second density estimate (`spikyPow3`) and a separate `nearPressureMultiplier`. The gradient is `spikyPow3Deriv`.

- **Viscosity** — smooths the velocity field. The force is `viscosityStrength · (vel_j − vel_i) · poly6(r)`, which pulls `i`'s velocity toward its neighbours', proportional to how close they are.

**5. Integrate and resolve collisions**

Positions are updated with the final velocities. `resolveCollision` then clamps each axis against the AABB bounds `±boundsHalfX/Y/Z` and reflects the velocity component with damping (`collisionDamping = 0.4`).

---

### Rendering — `src/main.cpp`

The scene is drawn in three back-to-front passes each frame to respect alpha blending.

#### Pass 1 — Dark floor quad

A single quad at `y = −0.97` spanning the full container floor. Drawn first so it appears beneath the fluid. The fragment shader outputs a fixed near-opaque dark navy colour (`0.08, 0.10, 0.14, 0.85`).

#### Pass 2 — Depth sprite impostors

Each particle is uploaded as a `vec4 (x, y, z, normalised_density)` to a `GL_DYNAMIC_DRAW` VBO and drawn as a `GL_POINTS` primitive.

**Vertex shader** — transforms the particle into view space, then projects it. Point size is computed as:

```
gl_PointSize = uRadius · proj[1][1] · 1600 / viewDepth
```

`proj[1][1]` is `cot(fov/2)`, so the expression converts a world-space radius into a correct perspective-scaled pixel size. Particles further away appear smaller.

**Fragment shader — sphere impostor** — each `GL_POINTS` fragment covers a square of pixels. For each fragment:

1. `gl_PointCoord` is remapped from `[0,1]²` to `[−1,+1]²`. Fragments outside the unit circle are discarded, turning the square into a circle.
2. The z-offset of the sphere surface at this screen-space position is `z = sqrt(1 − r²)`.
3. This z-offset is re-projected through the projection matrix and written to `gl_FragDepth`, giving each sprite a physically correct depth shell. Overlapping sprites occlude each other exactly as solid spheres would.
4. The fragment colour is a linear interpolation between a deep teal shadow (`0.00, 0.45, 0.55`) and a bright cyan-teal highlight (`0.20, 0.85, 0.80`), weighted by the sphere's surface `z` and the particle's normalised density. Alpha follows a cubic falloff (`z³ · 0.5`) capped at 0.40, giving the fluid a translucent water appearance.

#### Pass 3 — Glass container box

A procedural unit cube built in `buildBox()` — 6 faces × 4 vertices, each storing position and outward normal. Drawn last over the fluid.

The box is rendered with `glDepthMask(GL_FALSE)` so it does not write to the depth buffer (the fluid sprites behind it remain visible), and with `glDisable(GL_CULL_FACE)` so both inner and outer face surfaces are shaded through the glass.

**Fragment shader — Blinn-Phong** computes `ambient + diffuse + specular` using a half-vector `H = normalise(L + V)` for the specular term. The glass tint is near-white (`0.90, 0.95, 1.00`) with a fixed alpha of `0.07`.

---

### Camera and input

The camera is a free-fly Euler-angle camera stored as global state (`camPos`, `camYaw`, `camPitch`, `camFov`).

- **Right-click drag** and **M key** (cursor-lock mode) both rotate the camera via `cursor_callback`, which increments `camYaw` and clamps `camPitch` to `±89°`.
- **WASD / Space / Shift** translate `camPos` along the camera's local axes each frame in `processInput`.
- **Scroll** adjusts `camFov` between 10° and 90°.

### Left-click fluid grab

On left mouse button press, a ray is cast from `camPos` through the cursor's NDC position into world space. The particle nearest to the ray (within 0.25 world units) defines the grab anchor. All particles within a 0.30-unit sphere around that anchor are recorded with their offsets.

While the button is held, the grab centre is re-projected each frame: the mouse NDC position is unprojected back to a world-space point on the same constant view-space Z plane as the initial pick. Grabbed particles are teleported to their recorded offsets from the new centre, and their velocities are set to `Δcentre / dt` so they carry momentum when released.

---

## Simulation parameters

| Parameter | Value | Effect |
|---|---|---|
| `smoothingRadius` | 0.20 | Neighbour search radius and kernel support |
| `targetDensity` | 1000.0 | Rest density; fluid is pressureless at this value |
| `pressureMultiplier` | 200.0 | Stiffness — how strongly density deviation creates pressure |
| `nearPressureMultiplier` | 15.0 | Short-range repulsion strength |
| `viscosityStrength` | 0.06 | Velocity smoothing between neighbours |
| `gravity` | −9.8 | Downward acceleration (world Y axis) |
| `collisionDamping` | 0.4 | Fraction of normal velocity retained after a wall bounce |
| Substeps per frame | 8 | `dt` is divided by 8 and `step()` called eight times |
