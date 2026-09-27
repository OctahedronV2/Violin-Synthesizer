"""Bow-string friction junction.

Friction model
--------------
The friction coefficient follows the hyperbolic curve used by Smith (PASP,
"Bowed strings") and Serafin (2004):

    mu(dv) = sign(dv) * (mu_d + (mu_s - mu_d) * v0 / (v0 + |dv|))

where dv = v_bow - v_string is the sliding speed of the bow relative to the
string. mu_s is the static coefficient and mu_d the limit at high sliding
speed.

Junction
--------
With travelling velocity waves arriving at the bow point from the nut and the
bridge, the string velocity the bow would see if it exerted no force is

    v_h = v_in_nut + v_in_bridge

A force f applied at the bow point sends f / (2 Z) of velocity into each
direction (Z = string wave impedance), so the string velocity becomes

    v = v_h + f / (2 Z)

Writing d = v_bow - v and dh = v_bow - v_h, force balance with friction gives

    2 Z (dh - d) = F_bow * mu(d)

Sticking (d = 0) is possible whenever |2 Z dh| <= mu_s * F_bow. Otherwise the
bow slips, and for the hyperbolic curve the slip speed is the largest positive
root of

    a d^2 + (a v0 + F mu_d - a |dh|) d + (F mu_s - a |dh|) v0 = 0,   a = 2 Z

When both sticking and slipping are possible (the hysteresis region), the
previous state is kept, following McIntyre, Schumacher & Woodhouse (1983).

The numba functions here are the reference for the C++ FrictionJunction.
"""

from __future__ import annotations

import math

import numpy as np
from numba import njit


@njit(cache=True)
def friction_coefficient(dv: float, mu_s: float, mu_d: float, v0: float) -> float:
    """Hyperbolic friction coefficient for sliding speed dv (m/s)."""
    if dv == 0.0:
        return 0.0
    mag = mu_d + (mu_s - mu_d) * v0 / (v0 + abs(dv))
    return mag if dv > 0.0 else -mag


@njit(cache=True)
def _largest_slip_root(abs_dh: float, force: float, z: float, mu_s: float, mu_d: float, v0: float) -> float:
    """Largest positive slip speed, or -1.0 if there is no positive root."""
    a = 2.0 * z
    b = a * v0 + force * mu_d - a * abs_dh
    c = (force * mu_s - a * abs_dh) * v0
    disc = b * b - 4.0 * a * c
    if disc < 0.0:
        return -1.0
    root = (-b + math.sqrt(disc)) / (2.0 * a)
    return root if root > 0.0 else -1.0


@njit(cache=True)
def solve_junction(
    v_bow: float,
    v_h: float,
    force: float,
    z: float,
    mu_s: float,
    mu_d: float,
    v0: float,
    was_sticking: bool,
):
    """Solve the bow-string junction for one sample.

    Returns (v, sticking): the string velocity at the bow point and whether
    the bow is sticking.
    """
    dh = v_bow - v_h
    abs_dh = abs(dh)

    if force <= 0.0:
        return v_h, False

    can_stick = 2.0 * z * abs_dh <= mu_s * force

    if can_stick:
        if was_sticking:
            return v_bow, True
        slip = _largest_slip_root(abs_dh, force, z, mu_s, mu_d, v0)
        if slip < 0.0:
            return v_bow, True
    else:
        slip = _largest_slip_root(abs_dh, force, z, mu_s, mu_d, v0)
        # Always a positive root when sticking is impossible (c < 0).

    d = slip if dh > 0.0 else -slip
    return v_bow - d, False


def friction_curve(dv: np.ndarray, mu_s: float = 0.8, mu_d: float = 0.3, v0: float = 0.1) -> np.ndarray:
    """Vectorised friction coefficient, for plotting."""
    return np.array([friction_coefficient(float(x), mu_s, mu_d, v0) for x in np.asarray(dv)])
