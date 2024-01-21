"""This module contains all the prepackaged boolean types for the ``pdcast``
type system.
"""
from sys import getsizeof

import numpy as np
import pandas as pd

from .base import Type


class Bool(Type):
    """Abstract boolean type.

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

    .. doctest::

        >>> resolve_type("bool")
        BooleanType()
        >>> resolve_type("boolean[numpy]")
        NumpyBooleanType()
        >>> resolve_type("b1[pandas]")
        PandasBooleanType()
        >>> resolve_type("?[python]")
        PythonBooleanType()
    """

    aliases = {"bool", "boolean", "bool_", "bool8", "b1", "?"}


@Bool.default
class NumpyBool(Bool, backend="numpy"):
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

        >>> resolve_type(np.bool_)
        NumpyBooleanType()
        >>> resolve_type(np.dtype(bool))
        NumpyBooleanType()
    """

    aliases = {np.bool_, np.dtype(np.bool_)}
    dtype = np.dtype(np.bool_)
    itemsize = 1
    max = 1
    min = 0
    is_nullable = False


@NumpyBool.nullable
class PandasBool(Bool, backend="pandas"):
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

        >>> resolve_type("Boolean")
        PandasBooleanType()
        >>> resolve_type(pd.BooleanDtype)
        PandasBooleanType()
        >>> resolve_type(pd.BooleanDtype())
        PandasBooleanType()
    """

    aliases = {pd.BooleanDtype}
    dtype = pd.BooleanDtype()
    itemsize = 1
    max = 1
    min = 0


class PythonBool(Bool, backend="python"):
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

        >>> resolve_type(bool)
        PythonBooleanType()
    """

    aliases = {bool}
    scalar = bool
    itemsize = getsizeof(True)
    max = 1
    min = 0
