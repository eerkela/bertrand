"""This module contains the base class for all bertrand scalar types.

The entire contents of this file are hosted in the online documentation, and serve as
a reference for defining and registering new types within the bertrand type system.
Users can refer to this file to see how to build new types.
"""
from __future__ import annotations

from collections import deque
from typing import Any

import numpy as np
import pandas as pd

from .meta import DTYPE, EMPTY, POINTER_SIZE, Base, Empty, TypeMeta


# TODO: probably just put this back in meta.py and then provide a simple example here


# pylint: disable=unused-argument


def _check_scalar(
    value: Any | Empty,
    namespace: dict[str, Any],
    processed: dict[str, Any],
) -> type:
    """Validate a scalar Python type provided in a bertrand type's namespace or infer
    it from a provided dtype.
    """
    if "scalar" in processed:  # auto-generated in check_dtype
        return processed["scalar"]

    dtype = namespace.get("dtype", EMPTY)

    if value is EMPTY:
        if dtype is EMPTY:
            raise TypeError("type must define at least one of 'dtype' and/or 'scalar'")
        if not isinstance(dtype, (np.dtype, pd.api.extensions.ExtensionDtype)):
            raise TypeError(f"dtype must be a numpy/pandas dtype, not {repr(dtype)}")

        processed["dtype"] = dtype
        return dtype.type

    if not isinstance(value, type):
        raise TypeError(f"scalar must be a Python type object, not {repr(value)}")

    if dtype is EMPTY:
        processed["dtype"] = None  # TODO: synthesize dtype
    else:
        processed["dtype"] = dtype
    # elif value == dtype.type:
    #     processed["dtype"] = dtype
    # else:
    #     raise TypeError(
    #         f"scalar must be consistent with dtype.type: {repr(value)} != "
    #         f"{repr(dtype.type)}"
    #     )

    return value


def _check_dtype(
    value: Any | Empty,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> DTYPE:
    """Validate a numpy or pandas dtype provided in a bertrand type's namespace or
    infer it from a provided scalar.
    """
    if "dtype" in processed:  # auto-generated in check_scalar
        return processed["dtype"]

    scalar = namespace.get("scalar", EMPTY)

    if value is EMPTY:
        if scalar is EMPTY:
            raise TypeError("type must define at least one of 'dtype' and/or 'scalar'")
        if not isinstance(scalar, type):
            raise TypeError(f"scalar must be a Python type object, not {repr(scalar)}")

        processed["scalar"] = scalar
        return None  # NOTE: causes dtype to be synthesized during builder.register()

    if not isinstance(value, (np.dtype, pd.api.extensions.ExtensionDtype)):
        raise TypeError(f"dtype must be a numpy/pandas dtype, not {repr(value)}")

    if scalar is EMPTY:
        processed["scalar"] = value.type
    else:
        processed["scalar"] = scalar
    # elif value.type == scalar:
    #     processed["scalar"] = scalar
    # else:
    #     raise TypeError(
    #         f"dtype.type must be consistent with scalar: {repr(value.type)} != "
    #         f"{repr(scalar)}"
    #     )

    return value


def _check_itemsize(
    value: Any | Empty,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> int:
    """Validate an itemsize provided in a bertrand type's namespace."""
    if value is EMPTY:
        if "dtype" in namespace:
            dtype = namespace["dtype"]
            if dtype is None:
                return POINTER_SIZE
            return dtype.itemsize

        if "dtype" in processed:
            dtype = processed["dtype"]
            if dtype is None:
                return POINTER_SIZE
            return dtype.itemsize

    if not isinstance(value, int) or value <= 0:
        raise TypeError(f"itemsize must be a positive integer, not {repr(value)}")

    return value


def _check_max(
    value: Any | Empty,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> int | float:
    """Validate a maximum value provided in a bertrand type's namespace."""
    if "max" in processed:  # auto-generated in check_min
        return processed["max"]

    min_val = namespace.get("min", -np.inf)

    if value is EMPTY:
        processed["min"] = min_val
        return np.inf

    if not (isinstance(value, int) or isinstance(value, float) and value == np.inf):
        raise TypeError(f"min must be an integer or infinity, not {repr(value)}")

    if value < min_val:
        raise TypeError(f"max must be greater than min: {value} < {min_val}")

    processed["min"] = min_val
    return value


def _check_min(
    value: Any | Empty,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> int | float:
    """Validate a minimum value provided in a bertrand type's namespace."""
    if "min" in processed:  # auto-generated in check_max
        return processed["min"]

    max_val = namespace.get("max", np.inf)

    if value is EMPTY:
        processed["max"] = max_val
        return -np.inf

    if not (isinstance(value, int) or isinstance(value, float) and value == -np.inf):
        raise TypeError(f"min must be an integer or infinity, not {repr(value)}")

    if value > max_val:
        raise TypeError(f"min must be less than or equal to max: {value} > {max_val}")

    processed["max"] = max_val
    return value


def _check_is_nullable(
    value: Any | Empty,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> bool:
    """Validate a nullability flag provided in a bertrand type's namespace."""
    return True if value is EMPTY else bool(value)


def _check_is_numeric(
    value: Any | Empty,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> bool:
    """Validate a numeric flag provided in a bertrand type's namespace."""
    return False if value is EMPTY else bool(value)


def _check_missing(
    value: Any | Empty,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> Any:
    """Validate a missing value provided in a bertrand type's namespace."""
    if value is EMPTY:
        return pd.NA

    if not pd.isna(value):  # type: ignore
        raise TypeError(f"missing value must pass a pandas.isna() check: {repr(value)}")

    return value


class Type(Base, metaclass=TypeMeta):
    """Parent class for all scalar types.

    Inheriting from this class triggers the metaclass machinery, which automatically
    validates and registers the inheriting type.  As such, any class that inherits from
    this type becomes the root of a new type hierarchy, which can be extended to create
    tree structures of arbitrary depth.
    """

    # NOTE: every type has a collection of strings, python types, and numpy/pandas
    # dtype objects which are used to identify the type during detect() and resolve()
    # calls.  Here's how they work
    #   1.  Python types (e.g. int, str, etc.) - used during elementwise detect() to
    #       identify scalar values.  If defined, the type's `from_scalar()` method will
    #       be called automatically to parse each element.
    #   2.  numpy/pandas dtype objects (e.g. np.dtype("i4")) - used during detect() to
    #       identify array-like containers that implement a `.dtype` attribute.  If
    #       defined, the type's `from_dtype()` method will be called automatically to
    #       parse the dtype.  The same process also occurs whenever resolve() is called
    #       with a literal dtype argument.
    #   3.  Strings - used during resolve() to identify types by their aliases.
    #       Optional arguments can be provided in square brackets after the alias, and
    #       will be tokenized and passed to the type's `from_string()` method, if it
    #       exists.  Note that a type's aliases implicitly include its class name, so
    #       that type hints containing the type can be identified when
    #       `from __future__ import annotations` is enabled.  It is good practice to
    #       follow suit with other aliases so they can be used in type hints as well.

    aliases = {"foo", int, type(np.dtype("i4"))}  # <- for example (not evaluated)

    # NOTE: Any field assigned to EMPTY is a required attribute that must be defined
    # by concretions of this type.  Each field can be inherited from a parent type
    # with normal semantics.  If a type does not define a required attribute, then the
    # validation function will receive EMPTY as its value, and must either raise an
    # error or replace it with a default value.

    scalar:         type            = EMPTY(_check_scalar)  # type: ignore
    dtype:          DTYPE           = EMPTY(_check_dtype)  # type: ignore
    itemsize:       int             = EMPTY(_check_itemsize)  # type: ignore
    max:            int | float     = EMPTY(_check_max)  # type: ignore
    min:            int | float     = EMPTY(_check_min)  # type: ignore
    is_nullable:    bool            = EMPTY(_check_is_nullable)  # type: ignore
    is_numeric:     bool            = EMPTY(_check_is_numeric)  # type: ignore
    missing:        Any             = EMPTY(_check_missing)

    # NOTE: the following methods can be used to allow parametrization of a type using
    # an integrated flyweight cache.  At minimum, a parametrized type must implement
    # `__class_getitem__`, which specifies the parameter names and default values, as
    # well as `from_string`, which parses the equivalent syntax in the
    # type-specification mini-language.  The other methods are optional, and can be
    # used to customize the behavior of the detect() and resolve() helper functions.

    # None of these methods will be inherited by subclasses, so their behavior is
    # unique for each type.  If they are not defined, they will default to the
    # identity function, which will be optimized away where possible to improve
    # performance.

    def __class_getitem__(cls, spam: str = "foo", eggs: int = 2) -> TypeMeta:
        """Example implementation showing how to create a parametrized type.

        When this method is defined, the metaclass will analyze its signature and
        extract any default values, which will be forwarded to the class's
        :attr:`params <Type.params>` attribute, as well as its base namespace.  Each
        argument must have a default value, and the signature must not contain any
        keyword or variadic arguments (i.e. *args or **kwargs).

        Positional arguments to the flyweight() helper method should be supplied in
        the same order as they appear in the signature, and will be used to form the
        string identifier for the type.  Keywords are piped directly into the resulting
        class's namespace, and will override any existing values.  In the interest of
        speed, no checks are performed on the arguments, so it is up to the user to
        ensure that they are in the expected format.
        """
        return cls.flyweight(spam, eggs, dtype=np.dtype("i4"))

    @classmethod
    def from_string(cls, spam: str, eggs: str) -> TypeMeta:
        """Example implementation showing how to parse a string in the
        type-specification mini-language.

        This method will be called automatically whenever :func:`resolve` encounters
        an alias that matches this type.  Its signature must match that of
        :meth:`__class_getitem__`, and it will be invoked with the comma-separated
        tokens parsed from the alias's argument list.  They are always provided as
        strings, with no further processing other than stripping leading/trailing
        whitespace.  It is up to the user to parse them into the appropriate type.
        """
        return cls.flyweight(spam, int(eggs), dtype=np.dtype("i4"))

    @classmethod
    def from_scalar(cls, scalar: Any) -> TypeMeta:
        """Example implementation showing how to parse a scalar value into a
        parametrized type.

        This method will be called during the main :func:`detect` loop whenever a
        scalar or unlabeled sequence of scalars is encountered.  It should be fast,
        since it will be called for every element in the array.  If left blank, it will
        """
        return cls.flyweight(scalar.spam, scalar.eggs(1, 2, 3), dtype=scalar.ham)

    @classmethod
    def from_dtype(cls, dtype: Any) -> TypeMeta:
        """Example implementation showing how to parse a numpy/pandas dtype into a
        parametrized type.
        
        This method will be called whenever :func:`detect` encounters array-like data
        with a related dtype, or when :func:`resolve` is called with a dtype argument.
        """
        return cls.flyweight(dtype.unit, dtype.step_size, dtype=dtype)

    # NOTE: the following methods are provided as default implementations inherited by
    # descendent types.  They can be overridden to customize their behavior, although
    # doing so is not recommended unless absolutely necessary.  Typically, users should
    # invoke the parent method via super() and modify the input/output, rather than
    # reimplementing the method from scratch.

    @classmethod
    def replace(cls, *args: Any, **kwargs: Any) -> TypeMeta:
        """Base implementation of the replace() method, which is inherited by all
        descendant types.
        """
        forwarded = dict(cls.params)
        positional = deque(args)
        n  = len(args)
        i = 0
        for k in forwarded:
            if i < n:
                forwarded[k] = positional.popleft()
            elif k in kwargs:
                forwarded[k] = kwargs.pop(k)

        if positional:
            raise TypeError(
                f"replace() takes at most {len(cls.params)} positional arguments but "
                f"{n} were given"
            )
        if kwargs:
            singular = len(kwargs) == 1
            raise TypeError(
                f"replace() got {'an ' if singular else ''}unexpected keyword argument"
                f"{'' if singular else 's'} [{', '.join(repr(k) for k in kwargs)}]"
            )

        return cls.base_type[*forwarded.values()]
