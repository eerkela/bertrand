import inspect
from typing import Any, Callable

import numpy as np
cimport numpy as np
import pandas as pd

cimport pdcast.convert as convert
import pdcast.convert as convert
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve

from pdcast.util.type_hints import type_specifier

from .base cimport AtomicType, CompositeType
from .base import register


# TODO: datetime/timedelta conversions must account for datetime/timedelta
# supertype type_def=None


#######################
####    GENERIC    ####
#######################


@register
class ObjectType(AtomicType, cache_size=64):

    # internal root fields - all subtypes/backends inherit these
    conversion_func = convert.to_object

    name = "object"
    aliases = {
        "object", "obj", "O", "pyobject", "object_", "object0", np.dtype("O")
    }
    dtype = np.dtype("O")

    def __init__(self, type_def: type = object):
        self.type_def = type_def
        super().__init__(type_def=type_def)

    ############################
    ####    TYPE METHODS    ####
    ############################

    def contains(self, other: type_specifier) -> bool:
        """Test whether a type is contained within this type's subtype
        hierarchy.
        """
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(self.contains(o) for o in other)

        # treat `object` type_def as wildcard
        if self.type_def is object:
            return isinstance(other, type(self))
        return super().contains(other)

    @classmethod
    def slugify(cls, type_def: type = object) -> str:
        if type_def is object:
            return cls.name
        return f"{cls.name}[{type_def.__module__}.{type_def.__name__}]"

    @classmethod
    def resolve(cls, type_def: str = None) -> AtomicType:
        if type_def is None:
            return cls.instance()
        return cls.instance(type_def=from_caller(type_def))

    #######################
    ####    METHODS    ####
    #######################

    def contains(self, other: Any) -> bool:
        other = resolve.resolve_type(other)

        # treat type_def=object as wildcard
        if self.type_def is object:
            if isinstance(other, CompositeType):
                return all(isinstance(o, type(self)) for o in other)
            return isinstance(other, type(self))

        return super().contains(other)

    def to_boolean(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        call: Callable,
        errors: str,
        **unused
    ) -> pd.Series:
        """Convert unstructured objects to a boolean data type."""
        return two_step_conversion(
            series=series,
            dtype=dtype,
            call=dtype.type_def if call is None else call,
            errors=errors,
            conv_func=dtype.to_boolean,
            **unused
        )

    def to_integer(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        call: Callable,
        errors: str,
        **unused
    ) -> pd.Series:
        """Convert unstructured objects to an integer data type."""
        return two_step_conversion(
            series=series,
            dtype=dtype,
            call=dtype.type_def if call is None else call,
            errors=errors,
            conv_func=dtype.to_integer,
            **unused
        )

    def to_float(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        call: Callable,
        errors: str,
        **unused
    ) -> pd.Series:
        """Convert unstructured objects to a float data type."""
        return two_step_conversion(
            series=series,
            dtype=dtype,
            call=dtype.type_def if call is None else call,
            errors=errors,
            conv_func=convert.to_float,
            **unused
        )

    def to_complex(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        call: Callable,
        errors: str,
        **unused
    ) -> pd.Series:
        """Convert unstructured objects to a complex data type."""
        return two_step_conversion(
            series=series,
            dtype=dtype,
            call=dtype.type_def if call is None else call,
            errors=errors,
            conv_func=dtype.to_complex,
            **unused
        )

    def to_decimal(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        call: Callable,
        errors: str,
        **unused
    ) -> pd.Series:
        """Convert unstructured objects to a decimal data type."""
        return two_step_conversion(
            series=series,
            dtype=dtype,
            call=dtype.type_def if call is None else call,
            errors=errors,
            conv_func=dtype.to_decimal,
            **unused
        )

    def to_datetime(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        call: Callable,
        errors: str,
        **unused
    ) -> pd.Series:
        """Convert unstructured objects to a datetime data type."""
        return two_step_conversion(
            series=series,
            dtype=dtype,
            call=dtype.type_def if call is None else call,
            errors=errors,
            conv_func=dtype.to_datetime,
            **unused
        )

    def to_timedelta(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        call: Callable,
        errors: str,
        **unused
    ) -> pd.Series:
        """Convert unstructured objects to a timedelta data type."""
        return two_step_conversion(
            series=series,
            dtype=dtype,
            call=dtype.type_def if call is None else call,
            errors=errors,
            conv_func=dtype.to_timedelta,
            **unused
        )

    def to_string(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        call: Callable,
        errors: str,
        **unused
    ) -> pd.Series:
        """Convert unstructured objects to a string data type."""
        return two_step_conversion(
            series=series,
            dtype=dtype,
            call=dtype.type_def if call is None else call,
            errors=errors,
            conv_func=dtype.to_string,
            **unused
        )


#######################
####    PRIVATE    ####
#######################


class Test:

    def __init__(self, x: str = "foo"):
        self.x = x

    def as_boolean(self):
        print("Test.as_boolean called!")
        return bool(self)

    def __bool__(self) -> bool:
        return np.random.rand() > 0.5

    def __int__(self) -> int:
        return np.random.randint(10)

    def __float__(self) -> float:
        return np.random.rand()

    def __complex__(self) -> complex:
        return complex(np.random.rand(), np.random.rand())

    def __str__(self) -> str:
        return self.x


class Test2:

    def __init__(self, x: Test):
        self.x = f"{x} bar"

    def as_boolean(self):
        print("Test2.as_boolean called!")
        return bool(self)

    def __bool__(self) -> bool:
        return np.random.rand() > 0.5

    def __int__(self) -> int:
        return np.random.randint(10)

    def __float__(self) -> float:
        return np.random.rand()

    def __complex__(self) -> complex:
        return complex(np.random.rand(), np.random.rand())

    def __str__(self) -> str:
        return self.x


def two_step_conversion(
    series: convert.SeriesWrapper,
    dtype: AtomicType,
    call: Callable,
    errors: str,
    conv_func: Callable,
    **unused
) -> pd.Series:
    """A conversion in two parts."""
    def safe_call(object val):
        cdef object result = call(val)
        cdef type output_type = type(result)

        if output_type != dtype.type_def:
            raise TypeError(
                f"`call` must return an object of type {dtype.type_def}"
            )
        return result

    # apply `safe_call` over series and pass to delegated conversion
    series = series.apply_with_errors(
        call=safe_call,
        errors=errors,
        dtype=dtype
    )
    return conv_func(series, dtype=dtype, errors=errors, **unused)


cdef object from_caller(str name, int stack_index = 0):
    """Get an arbitrary object from a parent calling context."""
    # NOTE: cython does not yield proper inspect.frame objects. In normal
    # python, a stack_index of 0 would reference the `from_caller` frame
    # itself, but in cython (as implemented), it refers to the first python
    # frame that is encountered (which is always the calling context).

    # split name into '.'-separated object path
    cdef list full_path = name.split(".")
    frame = inspect.stack()[stack_index].frame

    # get first component of full path from calling context
    cdef str first = full_path[0]
    if first in frame.f_locals:
        result = frame.f_locals[first]
    elif first in frame.f_globals:
        result = frame.f_globals[first]
    elif first in frame.f_builtins:
        result = frame.f_builtins[first]
    else:
        raise ValueError(
            f"could not find {name} in calling frame at position {stack_index}"
        )

    # walk through any remaining path components
    cdef str component
    for component in full_path[1:]:
        result = getattr(result, component)

    # return fully resolved object
    return result
