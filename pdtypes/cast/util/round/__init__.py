"""This package contains various utilities to efficiently round both scalar
and vectorized numerics, according to customizable rounding rules.
"""
from .core import apply_tolerance, round_generic, snap_round
from .integer import round_div


# TODO: update module docstrings to inventory implemented functions
# TODO: rename round_generic() -> round(), apply_tolerance() -> snap()
