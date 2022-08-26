"""This package contains various utilities to efficiently round both scalar
and vectorized numerics, according to customizable rounding rules.
"""
from .core import apply_tolerance, round_numeric
from .integer import round_div
