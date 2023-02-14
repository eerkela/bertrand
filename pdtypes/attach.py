from __future__ import annotations
import json
from typing import Any, Callable

import pandas as pd

from pdtypes.types import (
    AtomicType, CompositeType, detect_type, resolvable, resolve_type
)
from pdtypes.types.cast import SeriesWrapper
from pdtypes.types.cast import cast as cast_standalone


########################
####    ADAPTERS    ####
########################


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


############################
####    MONKEY PATCH    ####
############################


orig_getattribute = pd.Series.__getattribute__


def new_getattribute(self, name: str):
    # check if attribute is a mock accessor
    dispatch_map = AtomicType.registry.dispatch_map
    if name in dispatch_map:
        return Namespace(name, self, dispatch_map[name])

    # check if attribute corresponds to a naked @dispatch method
    dispatch_map = dispatch_map.get(None, {})
    if name in dispatch_map:
        return attach(self, name, dispatch_map[name])

    # attribute is not being managed by `pdtypes` - fall back to pandas
    return orig_getattribute(self, name)


pd.Series.__getattribute__ = new_getattribute


pd.Series.check_type = check_type
pd.Series.cast = cast
pd.Series.get_type = get_type


#######################
####    PRIVATE    ####
#######################


def attach(data: pd.Series, name: str, submap: dict) -> Callable:
    """Decorate the named AtomicType method, globally attaching it to all
    pd.Series objects.
    """
    def wrapper(*args, **kwargs) -> pd.Series:
        with SeriesWrapper(data) as series:
            result = series.dispatch(name, submap, *args, **kwargs)
            series.series = result.series
            series.hasnans = result.hasnans
            series.element_type = result.element_type
        return series.series

    docmap = {str(k.type_def): v.__qualname__ for k, v in submap.items()}
    docmap = dict(sorted(docmap.items()))
    docmap["..."] = "Series.round"
    wrapper.__doc__ = (
        f"A wrapper for `pd.Series.{name}()` that applies custom logic for\n"
        f"one or more data types:\n{json.dumps(docmap, indent=4)}"
    )
    return wrapper


class Namespace:
    """"""

    def __init__(self, namespace: str, data: pd.Series, submap: dict):
        self.namespace = namespace
        self.data = data
        self.submap = submap

    def __getattribute__(self, name: str) -> Any:
        # recover instance attributes.  NOTE: this has to be done delicately
        # so as to avoid infinite recursion.
        namespace = super().__getattribute__("namespace")
        data = super().__getattribute__("data")
        submap = super().__getattribute__("submap")

        # check if attribute name is being dispatched from this namespace
        if name in submap:
            return attach(data, name, submap[name])
        return getattr(data, name)
