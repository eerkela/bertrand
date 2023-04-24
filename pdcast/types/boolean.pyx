"""This module contains all the prepackaged boolean types for the ``pdcast``
type system.
"""
import sys

import numpy as np
cimport numpy as np
import pandas as pd
import pytz

from pdcast import convert
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve

from pdcast.util.time cimport Epoch
from pdcast.util cimport wrapper

from .base cimport AtomicType
from .base import generic, register


def test(data: wrapper.SeriesWrapper):
    return data



######################
####    MIXINS    ####
######################


class BooleanMixin:
    """A mixin class that packages together the essential basic functionality
    for boolean types.
    """

    conversion_func = convert.to_boolean

    max = 1
    min = 0

    ###########################
    ####    CONVERSIONS    ####
    ###########################

    def to_decimal(
        self,
        series: wrapper.SeriesWrapper,
        dtype: AtomicType,
        **unused
    ) -> wrapper.SeriesWrapper:
        """Convert boolean data to a decimal format."""
        series = series + dtype.type_def(0)  # ~2x faster than loop
        series.element_type = dtype
        return series

    def to_datetime(
        self,
        series: wrapper.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        rounding: str,
        since: Epoch,
        tz: pytz.BaseTzInfo,
        errors: str,
        **unused
    ) -> wrapper.SeriesWrapper:
        """Convert boolean data to a datetime format."""
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
        series: wrapper.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        rounding: str,
        since: Epoch,
        errors: str,
        **unused
    ) -> wrapper.SeriesWrapper:
        """Convert boolean data to a timedelta format."""
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

    *   **type_def:** :class:`bool <python:bool>`
    *   **dtype:** ``numpy.dtype(bool)``
    *   **itemsize:** 1
    *   **na_value**: :class:`pandas.NA`
    *   **is_nullable:** False
    *   **max**: 1
    *   **min**: 0

    .. testsetup::

        import pdcast

    .. doctest::

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
    _family = "boolean"
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

    *   **aliases:** :class:`numpy.bool_`, ``numpy.dtype(bool)``
    *   **arguments:** []
    *   **type_def:** :class:`numpy.bool_`
    *   **dtype:** ``numpy.dtype(bool)``
    *   **itemsize:** 1
    *   **na_value**: :class:`pandas.NA`
    *   **is_nullable:** False
    *   **max**: 1
    *   **min**: 0

    .. testsetup::

        import numpy as np
        import pdcast

    .. doctest::

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

    *   **aliases:** ``"Boolean"``, :class:`pandas.BooleanDtype`
    *   **arguments:** []
    *   **type_def:** :class:`numpy.bool_`
    *   **dtype:** :class:`pandas.BooleanDtype() <pandas.BooleanDtype>`
    *   **itemsize:** 1
    *   **na_value**: :class:`pandas.NA`
    *   **is_nullable:** True
    *   **max**: 1
    *   **min**: 0

    .. testsetup::

        import pandas as pd
        import pdcast

    .. doctest::

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

    *   **aliases:** :class:`bool <python:bool>`
    *   **arguments:** []
    *   **type_def:** :class:`bool <python:bool>`
    *   **dtype:** auto-generated
    *   **itemsize:** 28
    *   **na_value**: :class:`pandas.NA`
    *   **is_nullable:** True
    *   **max**: 1
    *   **min**: 0

    .. testsetup::

        import pdcast

    .. doctest::

        >>> pdcast.resolve_type(bool)
        PythonBooleanType()
    """

    aliases = {bool}
    itemsize = sys.getsizeof(True)
    type_def = bool
