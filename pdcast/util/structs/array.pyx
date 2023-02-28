from typing import Any

import numpy as np
import pandas as pd


def as_series(data: Any) -> pd.Series:
    """Convert arbitrary data into a corresponding pd.Series object."""
    if isinstance(data, pd.Series):
        return data.copy()

    if isinstance(data, np.ndarray):
        return pd.Series(np.atleast_1d(data))

    return pd.Series(data, dtype="O")
