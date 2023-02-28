from __future__ import annotations
from functools import partial
import json
import sys
from typing import Any, Callable

import pandas as pd

from pdcast.types import AdapterType, AtomicType, CompositeType
from pdcast.cast import SeriesWrapper
from pdcast.cast import cast as cast_standalone
from pdcast.detect import detect_type
from pdcast.resolve import resolve_type

from pdcast.util.type_hints import type_specifier


########################
####    ADAPTERS    ####
########################


def cast(self, dtype: type_specifier = None, **kwargs) -> pd.Series:
    """Convert a pd.Series object to another data type."""
    return cast_standalone(self, dtype=dtype, **kwargs)


def check_type(self, dtype: type_specifier, exact: bool = False) -> bool:
    """Do a schema validation check on a pandas Series object."""
    series_type = detect_type(self)
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


def element_type(self) -> AdapterType | AtomicType | CompositeType:
    """Retrieve the element type of a pd.Series object."""
    return detect_type(self)


############################
####    MONKEY PATCH    ####
############################


orig_getattribute = pd.Series.__getattribute__


def new_getattribute(self, name: str):
    # check if attribute is a mock accessor
    dispatch_map = AtomicType.registry.dispatch_map
    if name in dispatch_map:
        original = getattr(pd.Series, name, None)
        return Namespace(self, dispatch_map[name], original)

    # check if attribute corresponds to a naked @dispatch method
    dispatch_map = dispatch_map.get(None, {})
    if name in dispatch_map:
        original = getattr(pd.Series, name, None)
        if original is not None:
            original = partial(original, self)
        return attach(self, name, dispatch_map[name], original)

    # attribute is not being managed by `pdcast` - fall back to pandas
    return orig_getattribute(self, name)


pd.Series.__getattribute__ = new_getattribute


pd.Series.cast = cast
pd.Series.check_type = check_type
pd.Series.element_type = property(element_type)


#######################
####    UNPATCH    ####
#######################


def detach() -> None:
    """Return `pd.Series` objects back to their original state, before the
    patch was applied.
    """
    pd.Series.__getattribute__ = orig_getattribute
    del pd.Series.cast
    del pd.Series.check_type
    del pd.Series.element_type

    # prepare to reimport
    sys.modules.pop("pdcast.attach.series")


#######################
####    PRIVATE    ####
#######################


def attach(
    data: pd.Series,
    name: str,
    submap: dict,
    original: Callable
) -> Callable:
    """Decorate the named AtomicType method, globally attaching it to all
    pd.Series objects.

    This function adds some important attributes to the wrapper that it
    returns.  These include:
        - `original` (Callable): a reference to the original pandas
            implementation of the masked method, if one exists.
        - `dispatched` (dict): a mapping 
    """
    def dispatch_method(*args, **kwargs) -> pd.Series:
        with SeriesWrapper(data) as series:
            result = series.dispatch(name, submap, original, *args, **kwargs)
            series.series = result.series
            series.hasnans = result.hasnans
            series.element_type = result.element_type
        return series.series

    dispatch_method.original = original
    docmap = {k.type_def: v for k, v in submap.items()}
    dispatch_method.dispatched = docmap.copy()
    docmap = {str(k): v.__qualname__ for k, v in docmap.items()}
    docmap = dict(sorted(docmap.items()))
    docmap["..."] = "Series.round"
    dispatch_method.__doc__ = (
        f"A wrapper that applies custom logic for one or more data types:\n"
        f"{json.dumps(docmap, indent=4)}\n\n"
        f"The above map is available under this method's `.dispatched`\n"
        f"attribute, and the original pandas implementation (if one exists)\n"
        f"can be recovered under its `.original` attribute."
    )
    return dispatch_method


class Namespace:
    """A mock accessor in the style of `pd.Series.dt`/`pd.Series.str` that
    holds @dispatch methods that are being applied to pd.Series objects.

    These objects are created dynamically through the optional `namespace`
    keyword argument of the @dispatch decorator.  
    """

    def __init__(self, data: pd.Series, submap: dict, original: Any):
        self.data = data
        self.dispatched = submap
        self.original = original

    def __getattribute__(self, name: str) -> Any:
        # recover instance attributes.  NOTE: this has to be done delicately
        # so as to avoid infinite recursion and enable pass-through access.
        data = super().__getattribute__("data")
        dispatched = super().__getattribute__("dispatched")
        original = super().__getattribute__("original")
        if name == "dispatched":
            return dispatched
        if name == "original":
            return original

        # check if attribute name is being dispatched from this namespace
        if name in dispatched:
            def bind_original(*args, **kwargs):
                return getattr(original(data), name, None)(*args, **kwargs)
            return attach(data, name, dispatched[name], bind_original)

        # if namespace masks a built-in pandas namespace, delegate to it
        if original is not None:
            return getattr(original(data), name)

        # delegate back to pd.Series
        return getattr(data, name)
