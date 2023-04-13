"""This package contains various utilities related to ``pdcast`` functionality,
including rounding, datetime manipulations, type hints, and basic data
structures.

Subpackages
-----------
array
    Automatic generation of ``ExtensionDtypes`` for arbitrary data, as well as
    specialized extensions for specific data types.

round
    Customizable rounding for numeric data, with a wider range of options than
    numpy or pandas.

structs
    Basic data structures that are generally applicable and can be used as
    recipes for other projects.

time
    Calendar-accurate datetime manipulations and string conversions with
    infinite range.

Modules
-------
error
    utilities for handling errors raised by ``pdcast`` functions and methods.

type_hints
    type hints for mypy and other static type checkers.
"""