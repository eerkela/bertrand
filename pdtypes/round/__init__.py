"""This package contains various utilities to efficiently round both scalar
and vectorized numerics, according to customizable rounding rules.
"""
from .core import apply_tolerance, round_generic
from .integer import round_div



# TODO: update module docstrings to inventory implemented functions
# TODO: apply rounding to datetimes and timedeltas with the given unit
# -> series.round(unit="s") (possibly with step size?)
# may have to consider DST transitions when rounded time crosses a DST
# transition.
