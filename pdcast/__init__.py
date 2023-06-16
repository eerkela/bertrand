"""Flexible type extensions for pandas.

Subpackages
-----------
convert
    Extendable conversions between types in the ``pdcast`` type system.

patch
    Direct ``pdcast`` integration and type-aware attribute dispatch for
    ``pandas.Series`` and ``pandas.DataFrame`` objects.

types
    Defines the structure and contents of the ``pdcast`` type system.

util
    Utilities for ``pdcast``-related functionality.

Modules
-------
check
    Fast type checks within the ``pdcast`` type system.

detect
    Type inference for arbitrary, vectorized data.

resolve
    Easy construction of data types from type specifiers, including a
    domain-specific mini-language for referring to types.
"""
# pylint: disable=undefined-variable, redefined-builtin
from .types import *
from .convert import (
    cast, categorize, decategorize, densify, sparsify, to_boolean, to_integer,
    to_float, to_complex, to_decimal, to_datetime, to_timedelta, to_string
)
from .decorators.attachable import (
    attachable, Attachable, ClassMethod, InstanceMethod, Namespace, Property,
    StaticMethod, VirtualAttribute
)
from .decorators.extension import extension_func, ExtensionFunc
from .decorators.dispatch import dispatch, DispatchFunc
from .resolve import resolve_type
from .detect import detect_type
from .patch.base import attach, detach
from .check import typecheck


# importing * from types also masks module names, which can be troublesome
del array
del base
del boolean
del categorical
del complex
del datetime
del decimal
del float
del integer
del object
del sparse
del string
del timedelta
