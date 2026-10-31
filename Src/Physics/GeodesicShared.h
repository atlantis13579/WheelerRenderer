// Shared by C++ validation and Shader Model 5.0. Distances are measured in Rs.
#ifndef WHEELER_GEODESIC_SHARED
#define WHEELER_GEODESIC_SHARED

#ifdef __cplusplus
namespace Wheeler
{
#define ORBIT_REAL double
#define ORBIT_INLINE inline
#else
#define ORBIT_REAL float
#define ORBIT_INLINE
#endif

struct OrbitState
{
    ORBIT_REAL u;
    ORBIT_REAL v;
};

// Exact Schwarzschild null orbit equation: u'' = 3u^2/2 - u, u = Rs/r.
// The invariant v^2 + u^2 - u^3 equals (Rs/b)^2.
ORBIT_INLINE ORBIT_REAL OrbitAcceleration(ORBIT_REAL u)
{
    return u * (1.5 * u - 1.0);
}

ORBIT_INLINE OrbitState StepOrbit(OrbitState s, ORBIT_REAL h)
{
    ORBIT_REAL a1 = OrbitAcceleration(s.u);
    ORBIT_REAL v2 = s.v + 0.5 * h * a1;
    ORBIT_REAL a2 = OrbitAcceleration(s.u + 0.5 * h * s.v);
    ORBIT_REAL v3 = s.v + 0.5 * h * a2;
    ORBIT_REAL a3 = OrbitAcceleration(s.u + 0.5 * h * v2);
    ORBIT_REAL v4 = s.v + h * a3;
    ORBIT_REAL a4 = OrbitAcceleration(s.u + h * v3);
    OrbitState result;
    result.u = s.u + (h / 6.0) * (s.v + 2.0 * v2 + 2.0 * v3 + v4);
    result.v = s.v + (h / 6.0) * (a1 + 2.0 * a2 + 2.0 * a3 + a4);
    return result;
}

#ifdef __cplusplus
}
#endif
#undef ORBIT_REAL
#undef ORBIT_INLINE
#endif
