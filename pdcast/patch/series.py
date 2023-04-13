"""This module describes the logic necessary to patch ``pdcast`` functionality
into ``pandas.Series`` objects.
"""
from __future__ import annotations

import pandas as pd

from pdcast.check import typecheck as typecheck_standalone
from pdcast.convert import cast
from pdcast.detect import detect_type
from pdcast.types import AdapterType, AtomicType, CompositeType

from .virtual import Namespace, DispatchMethod


# ignore this file when doing string-based object lookups in resolve_type()
_ignore_frame_objects = True


######################
####    PUBLIC    ####
######################


def attach() -> None:
    """Attach all dispatched methods to pd.Series objects"""
    pd.Series.__getattribute__ = new_getattribute
    cast.attach_to(pd.Series)
    pd.Series.typecheck = typecheck
    pd.Series.element_type = property(element_type)


def detach() -> None:
    """Return `pd.Series` objects back to their original state, before the
    patch was applied.
    """
    pd.Series.__getattribute__ = orig_getattribute
    del pd.Series.cast
    del pd.Series.typecheck
    del pd.Series.element_type


#######################
####    PRIVATE    ####
#######################


orig_getattribute = pd.Series.__getattribute__


def new_getattribute(self, name: str):
    """An overloaded __getattribute__ method for pd.Series objects that
    dynamically intercepts attribute lookups based on ``pdcast`` configuration
    and dispatches to the inferred element_type in case of a match.

    If no match is found, this simply passes through to the original pandas
    implementation.
    """
    # check if attribute is a namespace
    dispatch_map = AtomicType.registry.dispatch_map
    if name in dispatch_map:
        return Namespace(self, name, dispatch_map[name])

    # check if attribute is an @dispatch method
    submap = dispatch_map.get(None, {})
    if name in submap:
        return DispatchMethod(self, name, submap[name])

    # fall back to pandas
    return orig_getattribute(self, name)


def typecheck(self, *args, **kwargs) -> bool:
    """Do a schema validation check on a pandas Series object."""
    return typecheck_standalone(self, *args, **kwargs)


def element_type(self) -> AdapterType | AtomicType | CompositeType:
    """Retrieve the element type of a pd.Series object."""
    return detect_type(self)
