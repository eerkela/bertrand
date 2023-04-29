"""This package contains vectorized rounding operations for numeric data,
with customizable rounding rules and tolerances.

Modules
-------
decimal
    Customizable rounding for ``Decimal`` objects.

float
    Customizable rounding for floating point numbers.

integer
    Vectorized integer division with customizable rounding.

tolerance
    A ``Tolerance`` object for ``snap()`` operations.

Constants
---------
valid_rules
    A tuple listing the various rounding rules that are accepted by this
    package.
"""
from .decimal import round_decimal
from .float import round_float
from .integer import round_div
from .tolerance import Tolerance
