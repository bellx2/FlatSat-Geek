/*  - orbit_sim.h
 *
 * Circular-orbit ground-track model for the simulated GPS (FLATSAT_SIM_SENSORS).
 * Pure C++ (no Arduino), so it can be compiled and tested on a PC.
 *
 * Two-body circular orbit, no perturbations:
 *   period      T = 2*pi*sqrt(a^3/mu)             (~92.6 min at 420 km)
 *   argument of latitude  u(t) = u0 + n*t
 *   latitude    asin(sin(i) * sin(u))
 *   longitude   lambda0 + atan2(cos(i)*sin(u), cos(u)) - wE*t
 * Defaults are ISS-like (420 km, 51.6 deg). The orbit is phased so the
 * sub-satellite point is at START_LAT/START_LON, heading north-east, at t=0.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FIRMWARE_ORBIT_SIM_H
#define FIRMWARE_ORBIT_SIM_H
#include <math.h>

#ifndef FLATSAT_SIM_ORBIT_ALT_KM
#define FLATSAT_SIM_ORBIT_ALT_KM 420.0
#endif
#ifndef FLATSAT_SIM_ORBIT_INCL_DEG
#define FLATSAT_SIM_ORBIT_INCL_DEG 51.6
#endif
#ifndef FLATSAT_SIM_ORBIT_START_LAT
#define FLATSAT_SIM_ORBIT_START_LAT 35.6812 /* Tokyo */
#endif
#ifndef FLATSAT_SIM_ORBIT_START_LON
#define FLATSAT_SIM_ORBIT_START_LON 139.7671
#endif

typedef struct {
  double lat_deg;
  double lon_deg;
  double alt_m;
} orbit_state_t;

static inline void orbitState(double t_s, orbit_state_t *out) {
  const double kPi = 3.14159265358979323846;
  const double kMu = 398600.4418;         /* km^3/s^2 */
  const double kEarthR = 6371.0;          /* km, mean radius */
  const double kEarthRate = 7.2921159e-5; /* rad/s */
  const double a = kEarthR + FLATSAT_SIM_ORBIT_ALT_KM;
  const double n = sqrt(kMu / (a * a * a)); /* rad/s */
  const double inc = FLATSAT_SIM_ORBIT_INCL_DEG * kPi / 180.0;
  const double lat0 = FLATSAT_SIM_ORBIT_START_LAT * kPi / 180.0;
  const double lon0 = FLATSAT_SIM_ORBIT_START_LON * kPi / 180.0;

  /* Phase the orbit: ascending node reached so that t=0 is over (lat0, lon0). */
  double s0 = sin(lat0) / sin(inc);
  if (s0 > 1.0) s0 = 1.0;
  if (s0 < -1.0) s0 = -1.0;
  const double u0 = asin(s0);
  const double lambda0 = lon0 - atan2(cos(inc) * sin(u0), cos(u0));

  const double u = u0 + n * t_s;
  const double lat = asin(sin(inc) * sin(u));
  double lon = lambda0 + atan2(cos(inc) * sin(u), cos(u)) - kEarthRate * t_s;
  lon = fmod(lon + kPi, 2.0 * kPi);
  if (lon < 0) lon += 2.0 * kPi;
  lon -= kPi;

  out->lat_deg = lat * 180.0 / kPi;
  out->lon_deg = lon * 180.0 / kPi;
  out->alt_m = FLATSAT_SIM_ORBIT_ALT_KM * 1000.0;
}

/* ---- Thermal model: sunlight / eclipse cycle -------------------------------
 * The orbit starts just after leaving Earth's shadow. About 63 % of each orbit
 * is sunlit and 37 % is eclipse (circular orbit, simplified geometry). The
 * skin temperature is a first-order lag towards a hot (sun) or cold (shadow)
 * equilibrium; the start value is the periodic steady state, so there is no
 * warm-up transient. Defaults give roughly -28 .. +65 C, inside the BME280's
 * -40 .. +85 C range.
 */
#ifndef FLATSAT_SIM_T_HOT_C
#define FLATSAT_SIM_T_HOT_C 75.0
#endif
#ifndef FLATSAT_SIM_T_COLD_C
#define FLATSAT_SIM_T_COLD_C (-60.0)
#endif
#ifndef FLATSAT_SIM_T_TAU_S
#define FLATSAT_SIM_T_TAU_S 1500.0
#endif
#ifndef FLATSAT_SIM_ECLIPSE_FRACTION
#define FLATSAT_SIM_ECLIPSE_FRACTION 0.37
#endif

static inline double orbitPeriodS(void) {
  const double kPi = 3.14159265358979323846;
  const double kMu = 398600.4418; /* km^3/s^2 */
  const double a = 6371.0 + FLATSAT_SIM_ORBIT_ALT_KM;
  return 2.0 * kPi * sqrt(a * a * a / kMu);
}

/* Returns true while in Earth's shadow; *temp_c is the skin temperature. */
static inline bool orbitThermal(double t_s, double *temp_c) {
  const double T = orbitPeriodS();
  const double de = T * FLATSAT_SIM_ECLIPSE_FRACTION;
  const double ds = T - de;
  const double th = FLATSAT_SIM_T_HOT_C, tc = FLATSAT_SIM_T_COLD_C;
  const double a = exp(-ds / FLATSAT_SIM_T_TAU_S);
  const double b = exp(-de / FLATSAT_SIM_T_TAU_S);
  const double t0 = (tc * (1.0 - b) + th * b * (1.0 - a)) / (1.0 - a * b);
  double ph = fmod(t_s, T);
  if (ph < 0) ph += T;
  if (ph < ds) {
    *temp_c = th + (t0 - th) * exp(-ph / FLATSAT_SIM_T_TAU_S);
    return false;
  }
  const double ts = th + (t0 - th) * a;
  *temp_c = tc + (ts - tc) * exp(-(ph - ds) / FLATSAT_SIM_T_TAU_S);
  return true;
}

#endif // FIRMWARE_ORBIT_SIM_H
