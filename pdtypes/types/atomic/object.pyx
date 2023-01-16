import inspect
from typing import Any, Callable

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AtomicType, CompositeType
from .base import lru_cache

from pdtypes.error import shorten_list
cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve


#######################
####    GENERIC    ####
#######################


@lru_cache(64)
class ObjectType(AtomicType):

    name = "object"
    aliases = {"object", "obj", "O", "pyobject", "object_", "object0"}

    def __init__(self, base: type = object):
        super().__init__(
            type_def=base,
            dtype=np.dtype("O"),
            na_value=pd.NA,
            itemsize=None,
            base=base
        )

    #############################
    ####    CLASS METHODS    ####
    #############################

    @classmethod
    def slugify(cls, base: type = object) -> str:
        slug = cls.name
        if base is not object:
            slug += f"[{base.__module__}.{base.__name__}]"
        return slug

    @classmethod
    def resolve(cls, base: str = None) -> AtomicType:
        if base is None:
            return cls.instance()
        return cls.instance(base=from_caller(base))

    #######################
    ####    METHODS    ####
    #######################

    def contains(self, other: Any) -> bool:
        other = resolve.resolve_type(other)

        # treat base=object as wildcard
        if self.base is object:
            if isinstance(other, CompositeType):
                return all(isinstance(o, type(self)) for o in other)
            return isinstance(other, type(self))

        return super().contains(other)

    def to_boolean(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        call: Callable = None,
        errors: str = "raise",
        **unused
    ) -> pd.Series:
        """Convert unstructured objects to a boolean data type."""
        if call is None:
            call = bool  # hook into object's __bool__() magic method

        # apply `call` to each element of series (dtype=object)
        series = apply_and_wrap(
            series=series,
            call=call,
            na_value=dtype.na_value,
            errors=errors
        )

        # cast from object series to final dtype
        return cast.to_boolean(series, dtype=dtype, errors=errors, **unused)

    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        call: Callable = None,
        errors: str = "raise",
        **unused
    ) -> pd.Series:
        """Convert unstructured objects to an integer data type."""
        if call is None:
            call = int  # hook into object's __int__() magic method

        # apply `call` to each element of series (dtype=object)
        series = apply_and_wrap(
            series=series,
            call=call,
            na_value=dtype.na_value,
            errors=errors
        )

        # cast from object series to final dtype
        return cast.to_integer(series, dtype=dtype, errors=errors, **unused)

    def to_float(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        call: Callable = None,
        errors: str = "raise",
        **unused
    ) -> pd.Series:
        """Convert unstructured objects to a float data type."""
        if call is None:
            call = float  # hook into object's __float__() magic method

        # apply `call` to each element of series (dtype=object)
        series = apply_and_wrap(
            series=series,
            call=call,
            na_value=dtype.na_value,
            errors=errors
        )

        # cast from object series to final dtype
        return cast.to_float(series, dtype=dtype, errors=errors, **unused)

    def to_complex(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        call: Callable = None,
        errors: str = "raise",
        **unused
    ) -> pd.Series:
        """Convert unstructured objects to a complex data type."""
        if call is None:
            call = complex  # hook into object's __complex__() magic method

        # apply `call` to each element of series (dtype=object)
        series = apply_and_wrap(
            series=series,
            call=call,
            na_value=dtype.na_value,
            errors=errors
        )

        # cast from object series to final dtype
        return cast.to_complex(series, dtype=dtype, errors=errors, **unused)

    def to_decimal(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        call: Callable = None,
        errors: str = "raise",
        **unused
    ) -> pd.Series:
        """Convert unstructured objects to a decimal data type."""
        # TODO: this doesn't seem to be particularly efficient

        if call is None:
            call = dtype.type_def

        # apply `call` to each element of series (dtype=object)
        series = apply_and_wrap(
            series=series,
            call=call,
            na_value=dtype.na_value,
            errors=errors
        )

        # cast from object series to final dtype
        return cast.to_decimal(series, dtype=dtype, errors=errors, **unused)



#######################
####    PRIVATE    ####
#######################


cdef cast.SeriesWrapper apply_and_wrap(
    cast.SeriesWrapper series,
    object call,
    object na_value,
    str errors
):
    return cast.SeriesWrapper(
        cast.apply_with_errors(
            series.series,
            call=call,
            na_value=na_value,
            errors=errors
        ),
        hasnans=None if errors == "coerce" else series.hasnans,
        is_na=None if errors == "coerce" else False,
        # element_type=atomic.{x}
    )



cdef object from_caller(str name, int stack_index = 0):
    """Get an arbitrary object from a parent calling context."""
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
