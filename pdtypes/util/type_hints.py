from typing import Any, Union

import numpy as np
import pandas as pd


# array-like
array_like = Union[list, np.ndarray, pd.Series]

# dtype-like
atomic_type = Union[type, pd.api.extensions.ExtensionDtype]
dtype_like = Union[type, str, np.dtype, pd.api.extensions.ExtensionDtype]
scalar = Any
