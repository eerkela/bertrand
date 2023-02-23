"""This package contains vectorized rounding operations for numeric data,
with customizable rounding rules and tolerances.
"""
from .decimal import round_decimal
from .float import round_float
from .integer import round_div
from .tolerance import Tolerance


valid_rules = (
    "floor", "ceiling", "down", "up", "half_floor", "half_ceiling",
    "half_down", "half_up", "half_even"
)
