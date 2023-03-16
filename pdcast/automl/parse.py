from __future__ import annotations
from typing import Any, Union

import numpy as np
import pandas as pd
import psutil
import tzlocal

import pdcast.convert as convert
import pdcast.detect as detect

from pdcast.util.type_hints import datetime_like, timedelta_like


column_specifier = Union[
    pd.Series,
    pd.DataFrame,
    list,
    tuple,
    pd.Index,
    np.ndarray,
    Any
]


def extract_columns(df: pd.DataFrame, cols: column_specifier) -> pd.DataFrame:
    """Extract columns from a ``pandas.DataFrame`` for fitting."""
    # DataFrame
    if isinstance(cols, pd.DataFrame):
        if not df[cols.columns].equals(cols):
            raise ValueError(f"DataFrame columns do not match parent:\n{cols}")

    # Series
    elif isinstance(cols, pd.Series):
        if cols.name is None:
           raise ValueError(
               f"Series specifier must have a '.name' attribute:\n{cols}"
            )
        if not df[cols.name].equals(cols):
            raise ValueError(f"Series does not match parent")
        cols = pd.DataFrame(cols)

    # sequence
    elif isinstance(cols, (list, tuple, pd.Index, np.ndarray)):
        cols = df[cols]

    # scalar
    else:
        cols = df[[cols]]

    return cols


def parse_memory_limit(memory_limit: int | float) -> int:
    """Allows users to specify a memory limit as a fraction of total system
    resources.

    This function is used to parse the ``'memory_limit'`` argument of automl
    model fits.
    """
    # parse memory_limit
    if isinstance(memory_limit, float):
        total_memory = psutil.virtual_memory().total // (1024**2)
        result = int(memory_limit * total_memory)
    else:
        result = memory_limit

    # ensure positive
    if memory_limit < 0:
        raise ValueError(
            f"'memory_limit' must be positive, not {memory_limit}"
        )

    return result


def parse_n_jobs(n_jobs: int) -> int:
    """Allows users to specify a memory limit as a fraction of total system
    resources.

    This function is used to parse the ``'n_jobs'`` argument of automl model
    fits.
    """
    # trivial case: default to single thread
    if n_jobs is None:
        return 1

    # parse fraction of system resources
    if isinstance(n_jobs, float):
        if not 0 < n_jobs < 1:
            raise ValueError(
                f"If 'n_jobs' is a fraction, it must be between 0 and 1, not "
                f"{n_jobs}"
            )
        return int(n_jobs * psutil.cpu_count())

    # parse integer
    if n_jobs == -1:
        return psutil.cpu_count()
    if n_jobs < 1:
        raise ValueError(f"'n_jobs' must be positive, not {n_jobs}")
    return n_jobs


def parse_time_limit(
    time_limit: int | str | datetime_like | timedelta_like
) -> int:
    """Convert an arbitrary time limit into an integer number of seconds from
    runtime.

    This function is used to parse the ``'time_limit'`` argument of automl
    model fits.
    """
    # trivial case: integer seconds
    if isinstance(time_limit, int):
        result = time_limit

    # parse datetime/timedelta
    else:
        if isinstance(time_limit, str):
            result = convert.cast(time_limit, "datetime", tz="local")[0]
        else:
            result = time_limit

        result = convert.cast(
            result,
            "int[python]",
            unit="s",
            since=pd.Timestamp.now(tz=tzlocal.get_localzone_name())
        )

        # ensure scalar
        if len(result) != 1:
            raise ValueError(f"'time_limit' must be scalar, not {time_limit}")
        result = result[0]

    # ensure positive
    if result < 0:
        raise ValueError(f"'time_limit' must be positive, not {time_limit}")

    return result
