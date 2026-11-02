# Wheeler Renderer

Win64 / DirectX 11 real-time black hole renderer, C++17 + HLSL + Dear ImGui. By
default it provides an *Interstellar*-style gold-and-white accretion disk,
gravitational lensing, a photon ring, a starfield, and lens flare.

<img width="1583" height="890" alt="bh" src="https://github.com/user-attachments/assets/95ede5c5-0d05-4b2d-a537-8c8f226ec4a0" />

Null geodesics of photons are integrated backwards per pixel in Schwarzschild
spacetime. The deflection of the accretion disk's primary image, its upper and
lower secondary images, and the background starfield all come from the same
ray-path computation.

## Ray and rendering model

Lengths are in Schwarzschild radii `Rs = 2GM/c²`, with the black hole at the
origin. Spherical symmetry keeps every ray inside the orbital plane that passes
through the black hole. With `u = Rs/r` and `v = du/dφ`, the integration
equations are:

```text
du/dφ = v
dv/dφ = 1.5 u² - u
v² + u² - u³ = (Rs/b)²
```

`Src/Physics/GeodesicShared.h` contains the RK4 update shared by the CPU and
HLSL. The GPU uses float; CPU validation uses double. Stepping limits the radial
change and lands exactly on the angle where the orbit meets the disk plane;
thin-disk hits are not detected merely from the sign of two spatial samples.

Each pixel first generates a ray from the camera's local static orthonormal
frame. For observer radius `r₀` and local radial cosine `μ`:

```text
u₀ = Rs/r₀
b/Rs = (r₀/Rs) sqrt(1 - μ²) / sqrt(1 - u₀)
v₀ = -u₀ μ sqrt(1 - u₀) / sqrt(1 - μ²)
```

`u >= 1` means horizon capture; `u <= 0` means escape to infinity, where the
direction is used to look up the starfield. Radial rays are handled separately.
Rays that exhaust the integration budget are not mistakenly treated as escaped;
diagnostic mode marks them magenta.

The Schwarzschild photon sphere is at `1.5 Rs`, and the critical impact parameter
for a distant observer is `b_c = 3√3 Rs/2`. The inner radius of the stable
circular-orbit disk is clamped to no less than the ISCO, `3 Rs`.

The disk uses the Keplerian circular-orbit frequency for the frequency shift, and
the procedural temperature profile follows the radial form of a thin disk with
zero inner-boundary stress. For the actual future-directed photon angular
momentum `λ = L/E`, the full shift is:

```text
Ω = sqrt(0.5 / r³)                 # r in Rs, time in Rs/c
g = sqrt((1 - 1.5/r) / (1 - 1/r₀)) / (1 - Ω λ)
```

Setting both the gravitational and Doppler sliders to 1 recovers this expression.
An RGB color-temperature approximation with `g⁴` brightness modulation is used
for visualization. Disk noise, color temperature, density pattern, disk opacity,
and lens effects are adjustable visual models; full spectral radiative transfer
is not computed.

Render pipeline:

1. A fullscreen pixel shader integrates geodesics backwards, computes disk emission and escaping starlight, and writes RGBA16F.
2. Two HDR history buffers accumulate with subpixel jitter. Reset when the camera or render parameters change; at most 8 frames while animating, at most 32 while paused.
3. Five-level downsampled bloom, anamorphic lens flare, exposure, ACES-approximate tone mapping, gamma, and output dithering.
4. Draw Dear ImGui; the render target can be read before present and written as PNG via WIC.

**Model scope**: currently a non-spinning Schwarzschild black hole. The camera and
disk can rotate; Kerr spin, frame dragging, relativistic aberration for moving
observers, light-travel-time delay, MHD, and dynamic solves of the Einstein field
equations are not implemented. The mass parameter displays physical length scales;
with the same camera/disk radius in units of Rs, the image geometry does not change
with mass. The disk animation clock is artistically accelerated and does not
represent the real evolution time for the given solar mass. The starfield is a
procedural full celestial sphere; star footprint filtering and progressive
accumulation also differ from DNGR's elliptical beam tracing in the film.

Physics background: [Semerák, *Approximating light rays in the Schwarzschild
field*](https://arxiv.org/abs/1412.5650). Film visuals and shift handling:
[James et al., *Gravitational Lensing by Spinning Black Holes in Astrophysics, and
in the Movie Interstellar*](https://arxiv.org/abs/1502.03808). This project does
not reuse the DNGR implementation.
