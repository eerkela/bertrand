"""This module describes a pair of functions that can be used to convert
to and from categorical data representations.
"""
import numpy as np
import pandas as pd

from pdcast.decorators.dispatch import dispatch
from pdcast.detect import detect_type


@dispatch("series")
def categorize(
    series: pd.Series,
    levels: list | None = None
) -> pd.Series:
    """Transform a non-categorical series into a categorical series with the
    given levels.

    Parameters
    ----------
    series : pandas.Series
        The series to transform.
    levels : list, optional
        The levels to use for the categorical series.  If this is omitted, then
        levels will be automatically discovered when this method is called.

    Returns
    -------
    pandas.Series
        A categorical series with the same values as the input.

    Notes
    -----
    This function is automatically invoked whenever :func:`cast() <pdcast.cast>`
    is called with a categorical type as a target.  The result will be computed
    at the scalar level and then categorized using this function.

    Examples
    --------
    .. doctest::

        >>> import pandas as pd

        >>> categorize(pd.Series([1, 2, 3]))
        0    1
        1    2
        2    3
        dtype: category
        Categories (3, int64): [1, 2, 3]
    """
    # using a naked CategoricalDtype will automatically infer levels
    if levels is None:
        categorical_type = pd.CategoricalDtype()
    else:
        categorical_type = pd.CategoricalDtype(
            pd.Index(levels, dtype=detect_type(series).dtype)
        )

    return series.astype(categorical_type)


@dispatch("series")
def decategorize(series: pd.Series) -> pd.Series:
    """Transform a categorical series into a non-categorical series.

    Parameters
    ----------
    series : pandas.Series
        The series to transform.

    Returns
    -------
    pandas.Series
        A non-categorical series with the same values as the input.

    Notes
    -----
    This function is automatically invoked whenever :func:`cast() <pdcast.cast>`
    is called on a categorical series.  It is used to convert the series to a
    non-categorical format before casting to the target type.

    Examples
    --------
    .. doctest::

        >>> import pandas as pd

        >>> decategorize(pd.Series([1, 2, 3], dtype="category"))
        0    1
        1    2
        2    3
        dtype: int64
    """
    return series.astype(detect_type(series).wrapped.dtype, copy=False)
