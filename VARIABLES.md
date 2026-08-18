# Variable Reference

## FluidSim (`include/FluidSim.h`)

### Physics parameters

| Variable | Default | Meaning |
|----------|---------|---------|
| `smoothingRadius` | 0.20 | The SPH kernel radius `h`. Two particles only interact if they are closer than this distance. Increasing it makes each particle "feel" more neighbours, giving a smoother but less detailed fluid. |
| `targetDensity` | 1500.0 | The rest density the fluid tries to maintain. If a particle's computed density is above this value it is pushed away from its neighbours; below it, pulled toward them. |
| `pressureMultiplier` | 200.0 | Stiffness constant. Scales how strongly a density deviation turns into a pressure force. Higher values make the fluid more incompressible but can cause instability. |
| `nearPressureMultiplier` | 15.0 | Controls a short-range repulsion force (computed with `spikyPow3`) that prevents particles from overlapping at zero separation. |
| `viscosityStrength` | 0.06 | How strongly a particle's velocity is blended toward its neighbours'. Higher values make the fluid flow like honey; 0 gives a purely inviscid fluid. |
| `gravity` | -9.8 | Downward acceleration applied to every particle each step (world Y axis, negative = down). |
| `collisionDamping` | 0.4 | Fraction of the normal velocity component kept after a wall bounce. 1.0 = perfectly elastic; 0.0 = particle sticks to the wall. |
| `boundsHalfX/Y/Z` | 0.95 | Half-extents of the AABB container in each axis. Particles are clamped and reflected when they exceed `±boundsHalf*`. |

### Particle struct (`Particle`)

| Field | Meaning |
|-------|---------|
| `x, y, z` | World-space position of the particle. |
| `vx, vy, vz` | Velocity of the particle in world space. Updated each step by pressure, viscosity, and gravity forces. |
| `density` | SPH density estimate computed from neighbouring particles via the `spikyPow2` kernel. Used to derive pressure. |

### Internal simulation state

| Variable | Meaning |
|----------|---------|
| `predX, predY, predZ` | Predicted positions one timestep ahead (`pos + vel * dt`). The density and pressure solvers operate on these rather than the real positions — this predict-correct pattern improves stability. |
| `numCells` | Size of the spatial hash table. Set equal to the particle count so on average each bucket holds one particle. |
| `cellKeys` | For each particle, the hash bucket index it falls into (computed from its predicted cell). |
| `particleIndices` | Particle indices sorted by their `cellKey`, produced by counting sort. Allows contiguous iteration over all particles in a bucket. |
| `cellStart` | Prefix-sum array of size `numCells + 1`. `cellStart[k]` is the first index in `particleIndices` that belongs to bucket `k`. Together with `particleIndices` this forms a compact sorted hash map. |

---

## Camera (`include/Camera.h`)

| Variable | Meaning |
|----------|---------|
| `position` | World-space location of the camera eye point. |
| `yaw` | Horizontal rotation in degrees. −90° means the camera faces −Z (into the screen at startup). Incremented by left/right mouse movement. |
| `pitch` | Vertical rotation in degrees. Positive = looking up. Clamped to ±89° to prevent a gimbal flip at the poles. |
| `moveSpeed` | Translation speed in world units per second, applied to WASD / Space / Shift input. |
| `mouseSensitivity` | Degrees of rotation per pixel of mouse movement. |
| `fovDeg` | Vertical field-of-view in degrees. Adjusted by the scroll wheel; clamped to [1°, 90°]. |
| `front` | Unit vector pointing in the direction the camera is looking. Recomputed from `yaw` and `pitch` via spherical-to-Cartesian conversion. |
| `right` | Unit vector pointing to the camera's local right. Cross product of `front` and world-up `(0,1,0)`. |
| `up` | Unit vector pointing to the camera's local up. Cross product of `right` and `front`. Stays close to world-up unless pitch is extreme. |

---

## Renderer (`src/main.cpp`)

### Window and camera globals

| Variable | Meaning |
|----------|---------|
| `WIN_W, WIN_H` | Window dimensions in pixels (900×900). |
| `camYaw, camPitch` | Euler angles of the camera in degrees. |
| `camFov` | Vertical field-of-view in degrees. |
| `camPos` | World-space eye position. |
| `mouseLocked` | When true, the cursor is hidden and every mouse movement rotates the camera (M key toggles). |
| `rightDragging` | When true, right mouse button is held and dragging rotates the camera. |
| `lastX, lastY` | Previous cursor position, used to compute the per-frame mouse delta. |
| `firstMouse` | Guards against a large jump on the first frame after acquiring the cursor. |
| `moveSpeed` | Camera translation speed (world units / second). |
| `SENS` | Mouse sensitivity (degrees per pixel). |

### Grab state

| Variable | Meaning |
|----------|---------|
| `grabActive` | Whether the user is currently dragging a group of particles. |
| `grabIndices` | Indices into the particle array of every particle inside the grab sphere. |
| `grabOffsets` | Each grabbed particle's offset from `grabCenter` at the moment of picking. Kept constant so the group moves rigidly. |
| `grabCenter` | World-space center of the grab sphere, re-projected each frame to follow the mouse. |
| `grabViewZ` | The view-space Z of the grab center at pick time. Held constant so unprojection stays on the same depth plane while dragging. |

### Shader uniforms

| Uniform | Shader | Meaning |
|---------|--------|---------|
| `uVP` | sprite vert | View matrix (not view-projection — the vertex shader multiplies by `uProj` separately so it can access view-space depth for point sizing). |
| `uProj` | sprite vert/frag | Projection matrix. `uProj[1][1]` = `cot(fov/2)` is used to convert world-space radius to pixel size. Also used in the fragment shader to write a corrected `gl_FragDepth`. |
| `uRadius` | sprite vert | World-space radius of each particle sphere. Controls the rendered point size. |
| `uMVP` | box vert | Combined model-view-projection matrix for the glass box. |
| `uModel` | box vert | Model matrix for the box (identity — box is already in world space). |
| `uNormalMat` | box vert | `mat3` used to transform normals into world space. Equal to the transpose-inverse of the model matrix. |
| `uCamPos` | box frag | World-space camera position, used to compute the view vector `V` for specular highlights. |
| `uLightPos` | box frag | World-space position of the point light used for Blinn-Phong shading of the glass box. |
| `uVP` | floor vert | View-projection matrix for the floor quad. |

### Sprite fragment shader locals

| Variable | Meaning |
|----------|---------|
| `pc` | `gl_PointCoord` remapped from `[0,1]²` to `[−1,+1]²`. Represents the 2D position within the point sprite. |
| `r2` | Squared distance from the sprite center (`dot(pc, pc)`). Fragments where `r2 > 1` are outside the unit circle and discarded. |
| `z` | `sqrt(1 − r2)` — the Z coordinate on the unit sphere surface at this fragment. 1.0 at the center (front of sphere), 0.0 at the silhouette edge. |
| `t` | Blend factor for the color, combining `z` (surface orientation) and `vDensity` (particle density). Controls how much highlight vs. shadow color is shown. |
| `alpha` | `z³ · 0.5`, capped at 0.40. Makes the sprite fully transparent at the edges and semi-transparent at the center, giving the fluid a translucent water look. |
