"""This module contains all the prepackaged boolean types for the ``pdcast``
type system.
"""
import sys

import numpy as np
import pandas as pd

from .base cimport ScalarType, AbstractType
from .base import register


#######################
####    GENERIC    ####
#######################


@register
class BooleanType(AbstractType):
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

    name = "bool"
    aliases = {"bool", "boolean", "bool_", "bool8", "b1", "?"}


#####################
####    NUMPY    ####
#####################


@register
@BooleanType.default
@BooleanType.implementation("numpy")
class NumpyBooleanType(ScalarType):
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
    is_numeric = True
    max = 1
    min = 0
    is_nullable = False

    def make_nullable(self) -> ScalarType:
        """Convert this type to a nullable equivalent."""
        return self.registry[PandasBooleanType]


######################
####    PANDAS    ####
######################


@register
@BooleanType.implementation("pandas")
class PandasBooleanType(ScalarType):
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
    is_numeric = True
    max = 1
    min = 0


######################
####    PYTHON    ####
######################


@register
@BooleanType.implementation("python")
class PythonBooleanType(ScalarType):
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
    is_numeric = True
    max = 1
    min = 0
