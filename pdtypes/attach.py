from __future__ import annotations

import pandas as pd

from pdtypes.check import check_dtype, get_dtype
from pdtypes.util.type_hints import array_like, dtype_like


def series_check_dtype(
    self: pd.Series,
    dtype: dtype_like | array_like,
    exact: bool = False
) -> bool:
    """A modified calling signature for `pdtypes.check.check_dtype`, which
    allows it to be attached directly to pandas Series objects.
    """
    return check_dtype(self, dtype=dtype, exact=exact)


def series_get_dtype(self: pd.Series) -> type | tuple[type, ...]:
    """A modified calling signature for `pdtypes.check.get_dtype`, which
    allows it to be attached directly to pandas Series objects.
    """
    return get_dtype(self)


##################################
####    BEGIN MONKEY PATCH    ####
##################################


pd.Series.check_dtype = series_check_dtype
pd.Series.get_dtype = series_get_dtype


################################
####    END MONKEY PATCH    ####
################################

