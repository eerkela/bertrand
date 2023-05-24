"""This module contains all the prepackaged boolean types for the ``pdcast``
type system.
"""
import sys

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AtomicType
from .base import generic, register


######################
####    MIXINS    ####
######################


class BooleanMixin:
    """A mixin class that packages together the essential basic functionality
    for boolean types.
    """

    is_numeric = True
    max = 1
    min = 0


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
        return self.generic("pandas", **self.kwargs)


#######################
####    GENERIC    ####
#######################


@register
@generic
class BooleanType(AtomicType):
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

    # standard type definition
    name = "bool"
    aliases = {"bool", "boolean", "bool_", "bool8", "b1", "?"}


#####################
####    NUMPY    ####
#####################


@register
@BooleanType.implementation("numpy", default=True)
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
@BooleanType.implementation("pandas")
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
@BooleanType.implementation("python")
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
