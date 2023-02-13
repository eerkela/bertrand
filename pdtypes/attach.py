from __future__ import annotations

import pandas as pd

from pdtypes.types import (
    AtomicType, CompositeType, detect_type, resolvable, resolve_type
)
from pdtypes.types.cast import cast as cast_standalone


def cast(self, dtype: resolvable, **kwargs) -> pd.Series:
    """Convert a pd.Series object to another data type."""
    return cast_standalone(self, dtype=dtype, **kwargs)


def check_type(self, dtype: resolvable, exact: bool = False) -> bool:
    """Check the type of a pd.Series object."""
    series_type = detect_type(self.dropna())
    target_type = resolve_type(dtype)
    if exact:
        return series_type == target_type
    return series_type in target_type


def get_type(self) -> AtomicType | CompositeType:
    """Retrieve the element type of a pd.Series object."""
    return detect_type(self.dropna())


##################################
####    BEGIN MONKEY PATCH    ####
##################################


pd.Series.check_type = check_type
pd.Series.cast = cast
pd.Series.get_type = get_type


################################
####    END MONKEY PATCH    ####
################################
