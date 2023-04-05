import sys

import numpy as np
cimport numpy as np
import pandas as pd
import pytz

cimport pdcast.convert as convert
import pdcast.convert as convert
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve

from pdcast.util.time cimport Epoch

from .base cimport AtomicType
from .base import generic, register


# TODO: Add pyarrow boolean types?


######################
####    MIXINS    ####
######################


class BooleanMixin:

    max = 1
    min = 0

    ###########################
    ####    CONVERSIONS    ####
    ###########################

    def to_decimal(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert boolean data to decimal."""
        series = series + dtype.type_def(0)  # ~2x faster than loop
        series.element_type = dtype
        return series

    def to_datetime(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        rounding: str,
        since: Epoch,
        tz: pytz.BaseTzInfo,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert boolean data to datetime."""
        # 2-step conversion: bool -> int, int -> datetime
        transfer_type = resolve.resolve_type("int")
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
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        rounding: str,
        since: Epoch,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert boolean data to a timedelta."""
        transfer_type = resolve.resolve_type("int")
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


class NumpyBooleanMixin:
    """A mixin class that allows numpy booleans to automatically switch to
    their pandas equivalents when missing values are detected.
    """

    ##############################
    ####    MISSING VALUES    ####
    ##############################

    @property
    def is_nullable(self) -> bool:
        return False

    def make_nullable(self) -> AtomicType:
        return self.generic.instance(backend="pandas", **self.kwargs)


#######################
####    GENERIC    ####
#######################


@register
@generic
class BooleanType(BooleanMixin, NumpyBooleanMixin, AtomicType):
    """Generic boolean type.

    *   **aliases:** ``"bool"``, ``"boolean"``, ``"bool_"``, ``"bool8"``,
        ``"b1"``, ``"?"``
    *   **arguments:** [backend]

        *   **backends:** :class:`numpy <NumpyBooleanType>`,
            :class:`pandas <PandasBooleanType>`,
            :class:`python <PythonBooleanType>`

    *   **type_def:** ``bool``
    *   **dtype:** ``np.dtype(bool)``
    *   **itemsize:** 1
    *   **na_value**: ``pd.NA``
    *   **is_nullable:** False
    *   **max**: 1
    *   **min**: 0

    .. testsetup:: boolean_type_example:

        import pdcast

    .. doctest:: boolean_type_example

        >>> pdcast.resolve_type("bool")
        BooleanType()
        >>> pdcast.resolve_type("boolean[numpy]")
        NumpyBooleanType()
        >>> pdcast.resolve_type("b1[pandas]")
        PandasBooleanType()
        >>> pdcast.resolve_type("?[python]")
        PythonBooleanType()
    """

    # internal root fields - all subtypes/backends inherit these
    _conversion_func = convert.to_boolean
    _is_boolean = True
    _is_numeric = True

    # standard type definition
    name = "bool"
    aliases = {"bool", "boolean", "bool_", "bool8", "b1", "?"}
    dtype = np.dtype(np.bool_)
    itemsize = 1
    type_def = bool
    is_nullable = False


#####################
####    NUMPY    ####
#####################


@register
@BooleanType.register_backend("numpy")
class NumpyBooleanType(BooleanMixin, NumpyBooleanMixin, AtomicType):
    """Numpy boolean type.

    *   **aliases:** ``np.bool_``, ``np.dtype(bool)``
    *   **arguments:** []
    *   **type_def:** ``np.bool_``
    *   **dtype:** ``np.dtype(bool)``
    *   **itemsize:** 1
    *   **na_value**: ``pd.NA``
    *   **is_nullable:** False
    *   **max**: 1
    *   **min**: 0

    .. testsetup:: numpy_boolean_type_example:

        import numpy as np
        import pdcast

    .. doctest:: numpy_boolean_type_example

        >>> pdcast.resolve_type(np.bool_)
        NumpyBooleanType()
        >>> pdcast.resolve_type(np.dtype(bool))
        NumpyBooleanType()
    """

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
    """Pandas boolean type.

    *   **aliases:** ``"Boolean"``, ``pd.BooleanDtype``, ``pd.BooleanDtype()``
    *   **arguments:** []
    *   **type_def:** ``np.bool_``
    *   **dtype:** ``pd.BooleanDtype()``
    *   **itemsize:** 1
    *   **na_value**: ``pd.NA``
    *   **is_nullable:** True
    *   **max**: 1
    *   **min**: 0

    .. testsetup:: pandas_boolean_type_example:

        import pandas as pd
        import pdcast

    .. doctest:: pandas_boolean_type_example

        >>> pdcast.resolve_type("Boolean")
        PandasBooleanType()
        >>> pdcast.resolve_type(pd.BooleanDtype)
        PandasBooleanType()
        >>> pdcast.resolve_type(pd.BooleanDtype())
        PandasBooleanType()
    """

    aliases = {pd.BooleanDtype, "Boolean"}
    dtype = pd.BooleanDtype()
    itemsize = 1
    type_def = np.bool_


######################
####    PYTHON    ####
######################


@register
@BooleanType.register_backend("python")
class PythonBooleanType(BooleanMixin, AtomicType):
    """Python boolean type.

    *   **aliases:** ``bool``
    *   **arguments:** []
    *   **type_def:** ``bool``
    *   **dtype:** auto-generated
    *   **itemsize:** 28
    *   **na_value**: ``pd.NA``
    *   **is_nullable:** True
    *   **max**: 1
    *   **min**: 0

    .. testsetup:: python_boolean_type_example:

        import pdcast

    .. doctest:: python_boolean_type_example

        >>> pdcast.resolve_type(bool)
        PythonBooleanType()
    """

    aliases = {bool}
    itemsize = sys.getsizeof(True)
    type_def = bool
