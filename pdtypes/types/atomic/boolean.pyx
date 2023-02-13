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

from pdtypes.util.time cimport Epoch
from pdtypes.util.time import convert_unit


# TODO: add datetime_like `since` hint to to_datetime, to_timedelta


# TODO: fully implement boolean to_x conversions


# TODO: to_timedelta([True, False, None, 1, 2, 3], "m8[s]", unit="s")
# yields NAs for integer part rather than converting as expected.


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

    def to_decimal(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert boolean data into an equivalent decimal representation."""
        series = series + dtype.type_def(0)  # ~2x faster than loop
        series.element_type = dtype
        return series

    def to_datetime(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        rounding: str,
        epoch: Epoch,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert boolean data into an equivalent datetime representation."""
        # 2-step conversion: bool -> int, int -> datetime
        transfer_type = resolve.resolve_type(int)
        series = self.to_integer(
            series,
            dtype=transfer_type,
            downcast=None,
            errors="raise"
        )
        return transfer_type.to_datetime(
            series,
            dtype=dtype,
            unit=unit,
            step_size=step_size,
            rounding=rounding,
            epoch=epoch,
            errors=errors,
            **unused
        )

    def to_timedelta(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        rounding: str,
        epoch: Epoch,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert integer data to a timedelta data type."""
        transfer_type = resolve.resolve_type(int)
        series = self.to_integer(
            series,
            dtype=transfer_type,
            downcast=None,
            errors="raise"
        )
        return transfer_type.to_timedelta(
            series,
            dtype=dtype,
            unit=unit,
            step_size=step_size,
            rounding=rounding,
            epoch=epoch,
            errors=errors,
            **unused
        )


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
