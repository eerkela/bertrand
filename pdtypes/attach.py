from __future__ import annotations

import pandas as pd

from pdtypes.types import (
    AtomicType, CompositeType, detect_type, resolvable, resolve_type
)
from pdtypes.types.cast import SeriesWrapper
from pdtypes.types.cast import cast as cast_standalone


def cast(self, dtype: resolvable, **kwargs) -> pd.Series:
    """Convert a pd.Series object to another data type."""
    return cast_standalone(self, dtype=dtype, **kwargs)


def check_type(self, dtype: resolvable, exact: bool = False) -> bool:
    """Check the type of a pd.Series object."""
    series_type = detect_type(self.dropna())
    target_type = resolve_type(dtype)

    # enforce strict match
    if exact:
        if isinstance(target_type, CompositeType):
            target_type = set(target_type)
        else:
            target_type = {target_type}

        if isinstance(series_type, CompositeType):
            return all(t in target_type for t in series_type)
        return series_type in target_type

    # include subtypes
    return series_type in target_type


def get_type(self) -> AtomicType | CompositeType:
    """Retrieve the element type of a pd.Series object."""
    return detect_type(self.dropna())


##################################
####    BEGIN MONKEY PATCH    ####
##################################


orig_getattribute = pd.Series.__getattribute__

def new_getattribute(self, name: str):
    # check if attribute name is being dispatched
    if name in AtomicType.registry.dispatch_map:
        # wrap series and dispatch to element type
        def dispatch(*args, **kwargs):
            with SeriesWrapper(self) as series:
                result = series.dispatch(name, *args, **kwargs)
                series.series = result.series
                series.hasnans = result.hasnans
                series.element_type = result.element_type
            return series.series
        return dispatch

    # fall back to original
    return orig_getattribute(self, name)

pd.Series.__getattribute__ = new_getattribute


pd.Series.check_type = check_type
pd.Series.cast = cast
pd.Series.get_type = get_type


################################
####    END MONKEY PATCH    ####
################################
