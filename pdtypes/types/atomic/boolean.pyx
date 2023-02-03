import decimal
from typing import Union, Sequence

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd
import pytz

from .base cimport AtomicType
from .base import dispatch, generic

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
    min = 0
    max = 1

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

    @dispatch
    def to_decimal(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert boolean data to a decimal data type."""
        result = series + dtype.type_def(0)  # ~2x faster than loop
        result.element_type = dtype
        return result

    @dispatch
    def to_datetime(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tz: pytz.BaseTzInfo,
        unit: str,
        since: np.datetime64
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
