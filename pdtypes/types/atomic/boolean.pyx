import decimal
from typing import Union, Sequence

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AtomicType
from .base import generic

cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve


# TODO: add datetime_like `since` hint to to_datetime, to_timedelta


# TODO: fully implement boolean to_x conversions


######################
####    MIXINS    ####
######################


class BooleanMixin:

    # is_boolean = True

    ############################
    ####    TYPE METHODS    ####
    ############################

    def force_nullable(self) -> AtomicType:
        """Create an equivalent boolean type that can accept missing values."""
        if self.is_nullable:
            return self
        return self.generic.instance(backend="pandas")

    @property
    def is_nullable(self) -> bool:
        """Check if a boolean type supports missing values."""
        if isinstance(self.dtype, np.dtype):
            return np.issubdtype(self.dtype, "O")
        return True

    def parse(self, input_str: str):
        if input_str in resolve.na_strings:
            return resolve.na_strings[input_str]
        if input_str not in ("True", "False"):
            raise TypeError(
                f"could not interpret boolean string: {input_str}"
            )
        return self.type_def(input_str == "True")

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    def to_decimal(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert boolean data to a decimal data type."""
        return cast.SeriesWrapper(
            series + dtype.type_def(0),  # ~2x faster than loop
            hasnans=series.hasnans,
            element_type=dtype
        )

    def to_datetime(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str = "ns",
        since: str = "UTC"
    ) -> cast.SeriesWrapper:
        """Convert boolean data to a datetime data type."""
        raise NotImplementedError()


#######################
####    GENERIC    ####
#######################


@generic
class BooleanType(BooleanMixin, AtomicType):
    """Boolean supertype."""

    conversion_func = cast.to_boolean  # all subtypes/backends inherit this
    name = "bool"
    aliases = {bool, "bool", "boolean", "bool_", "bool8", "b1", "?"}
    is_nullable = False

    def __init__(self):
        super().__init__(
            type_def=bool,
            dtype=np.dtype(np.bool_),
            na_value=pd.NA,
            itemsize=1
        )


#####################
####    NUMPY    ####
#####################


@BooleanType.register_backend("numpy")
class NumpyBooleanType(BooleanMixin, AtomicType):

    aliases = {np.bool_, np.dtype(np.bool_)}
    is_nullable = False

    def __init__(self):
        super().__init__(
            type_def=np.bool_,
            dtype=np.dtype(np.bool_),
            na_value=pd.NA,
            itemsize=1
        )


######################
####    PANDAS    ####
######################


@BooleanType.register_backend("pandas")
class PandasBooleanType(BooleanMixin, AtomicType):

    aliases = {pd.BooleanDtype(), "Boolean"}

    def __init__(self):
        super().__init__(
            type_def=np.bool_,
            dtype=pd.BooleanDtype(),
            na_value=pd.NA,
            itemsize=1
        )


######################
####    PYTHON    ####
######################


@BooleanType.register_backend("python")
class PythonBooleanType(BooleanMixin, AtomicType):

    aliases = set()

    def __init__(self):
        super().__init__(
            type_def=bool,
            dtype=np.dtype("O"),
            na_value=pd.NA,
            itemsize=1
        )


#######################
####    PRIVATE    ####
#######################


# StringType.to_boolean(
#     self,
#     series: cast.SeriesWrapper,
#     dtype: AtomicType,
#     true: Sequence[str] = ("true", "t", "yes", "y", "on", "1"),
#     false: Sequence[str] = ("false", "f", "no", "n", "off", "0"),
#     errors: str = "raise"
# ):
#     fill_value, true, false = parse_true_false_strings(dtype, true, false)
#     if not pd.isna(fill_value):
#         errors = "coerce"
# 
#     lookup = dict.fromkeys(true, True)
#     lookup |= dict.fromkeys(false, False)
# 
#     result, hasnans = string_to_boolean(
#         series.to_numpy(),
#         lookup=lookup,
#         errors=errors,
#         fill_value=fill_value
#     )
# 
#     if (series.hasnans or hasnans) and not dtype.is_nullable:
#         dtype = dtype.replace(backend="pandas")
# 
#     return pd.Series(result, index=series.index, dtype=dtype.dtype)


cdef tuple parse_true_false_strings(AtomicType dtype, tuple true, tuple false):
    """Remove wildcards from `true`/`false` and find an appropriate
    `fill_value` for BooleanType.parse()
    """
    fill_value = dtype.na_value

    # check for '*' in `true`
    if "*" in true:
        fill_value = dtype.type_def(True)
        while "*" in true:
            true.remove("*")

    # check for '*' in `false`
    if "*" in false:
        if fill_value == True:
            raise ValueError(
                "wildcard '*' can be in either `true` or `false`, not both"
            )
        fill_value = dtype.type_def(False)
        while "*" in false:
            false.remove("*")

    # unescape '\*' literals
    true = tuple(s.replace(r"\*", "*") for s in true)
    false = tuple(s.replace(r"\*", "*") for s in false)

    # return
    return (fill_value, true, false)


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] string_to_boolean_vector(
    np.ndarray[str] arr,
    tuple true,
    tuple false,
    str errors,
    object fill_value
):
    """Convert boolean strings ('True', 'False', 'yes', 'no', 'on', 'off',
    etc.) into their equivalent boolean value.
    """
    cdef unsigned int arr_length = arr.shape[0]
    cdef unsigned int i
    cdef dict lookup = {}
    cdef np.ndarray[object] result = np.full(arr_length, fill_value, dtype="O")

    lookup = dict.fromkeys(true, True)
    lookup |= dict.fromkeys(false, False)
    lookup |= {k.lower(): v for k, v in resolve.na_strings.items()}

    # step through array
    for i in range(arr_length):
        try:
            result[i] = lookup[arr[i].strip().lower()]
        except KeyError as err:
            if errors == "coerce":
                continue  # np.full(...) implicitly fills with `fill_value`
            err_msg = (f"non-boolean value encountered at index {i}: "
                       f"{repr(arr[i])}")
            raise ValueError(err_msg) from err

    return result
