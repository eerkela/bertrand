"""This module describes a pair of functions that can be used to convert
to and from sparse data representations.
"""
from typing import Any

import pandas as pd

from pdcast.decorators.dispatch import dispatch
from pdcast.detect import detect_type


@dispatch("series")
def sparsify(series: pd.Series, fill_value: Any = None) -> pd.Series:
    """Transform a dense series into a sparse series with the given fill value.

    Parameters
    ----------
    series : pd.Series
        The series to transform.
    fill_value : Any, optional
        The value to hold out of the sparse series.  If not given, the
        :attr:`na_value <pdcast.ScalarType.na_value>` of the inferred series
        type will be used instead.

    Returns
    -------
    pd.Series
        A sparse series with the same values as the input.

    Notes
    -----
    This function is automatically invoked whenever :func:`cast() <pdcast.cast>`
    is called with a sparse type as a target.  The result will be computed
    at the scalar level and then made sparse using this function.

    Examples
    --------
    .. doctest::

        >>> import pandas as pd

        >>> pdcast.sparsify(pd.Series([1, 2, 3]), fill_value=1)
        0    1
        1    2
        2    3
        dtype: Sparse[int64, <NA>]
        >>> _.sparse.density
        0.6666666666666666
    """
    # if no explicit fill value is given, use the series type's na_value
    if fill_value is None:
        fill_value = detect_type(series).na_value

    # astype() to a sparse dtype
    return series.astype(pd.SparseDtype(series.dtype, fill_value))


@dispatch("series")
def densify(series: pd.Series) -> pd.Series:
    """Transform a sparse series into a dense series.

    Parameters
    ----------
    series : pd.Series
        The series to transform.

    Returns
    -------
    pd.Series
        A dense series with the same values as the input.

    Notes
    -----
    This function is automatically invoked whenever :func:`cast() <pdcast.cast>`
    is called on a sparse series.  It is used to densify the input before
    converting to the target type.

    Examples
    --------
    .. doctest::

        >>> import pandas as pd

        >>> pdcast.densify(pd.Series([1, 2, 3]).astype("Sparse[int]"))
        0    1
        1    2
        2    3
        dtype: int64
    """
    return series.sparse.to_dense()
