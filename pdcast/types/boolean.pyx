import numpy as np
cimport numpy as np
import pandas as pd
import pytz

cimport pdcast.cast as cast
import pdcast.cast as cast
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve

from pdcast.util.time cimport Epoch

from .base cimport AtomicType
from .base import generic, register


######################
####    MIXINS    ####
######################


class BooleanMixin:

    max = 1
    min = 0

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
        since: Epoch,
        tz: pytz.BaseTzInfo,
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
            since=since,
            tz=tz,
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
        since: Epoch,
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
            since=since,
            errors=errors,
            **unused
        )


#######################
####    GENERIC    ####
#######################


@register
@generic
class BooleanType(BooleanMixin, AtomicType):
    """Boolean supertype."""

    conversion_func = cast.to_boolean  # all subtypes/backends inherit this
    name = "bool"
    aliases = {bool, "bool", "boolean", "bool_", "bool8", "b1", "?"}
    dtype = np.dtype(np.bool_)
    itemsize = 1
    type_def = bool
    is_nullable = False


#####################
####    NUMPY    ####
#####################


@register
@BooleanType.register_backend("numpy")
class NumpyBooleanType(BooleanMixin, AtomicType):

    aliases = {np.bool_, np.dtype(np.bool_)}
    dtype = np.dtype(np.bool_)
    itemsize = 1
    type_def = np.bool_
    is_nullable = False


######################
####    PANDAS    ####
######################


@register
@BooleanType.register_backend("pandas")
class PandasBooleanType(BooleanMixin, AtomicType):

    aliases = {pd.BooleanDtype(), "Boolean"}
    dtype = pd.BooleanDtype()
    itemsize = 1
    type_def = np.bool_


######################
####    PYTHON    ####
######################


@register
@BooleanType.register_backend("python")
class PythonBooleanType(BooleanMixin, AtomicType):

    aliases = set()
    dtype = np.dtype("O")
    itemsize = 1
    type_def = bool
