"""This module describes an ``ObjectType`` object, which can be used to
represent arbitrary Python data.
"""
import inspect

import numpy as np
cimport numpy as np
import pandas as pd

from pdcast cimport resolve
from pdcast import resolve
from pdcast.util.type_hints import type_specifier

from .base cimport AtomicType, CompositeType
from .base import register


#######################
####    GENERIC    ####
#######################


@register
class ObjectType(AtomicType, cache_size=64):

    name = "object"
    aliases = {
        "object", "obj", "O", "pyobject", "object_", "object0", np.dtype("O")
    }

    def __init__(self, type_def: type = object):
        super().__init__(type_def=type_def)

    @property
    def dtype(self) -> np.dtype | pd.api.extensions.ExtensionDtype:
        if self.type_def is object:
            return np.dtype("O")
        return super().dtype

    @property
    def type_def(self) -> type:
        return self.kwargs["type_def"]

    ############################
    ####    TYPE METHODS    ####
    ############################

    def contains(
        self,
        other: type_specifier,
        include_subtypes: bool = True
    ) -> bool:
        """Test whether a type is contained within this type's subtype
        hierarchy.
        """
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(
                self.contains(o, include_subtypes=include_subtypes)
                for o in other
            )

        # treat `object` type_def as wildcard
        if self.type_def is object:
            return isinstance(other, type(self))
        return super().contains(other, include_subtypes=include_subtypes)

    @classmethod
    def slugify(cls, type_def: type = object) -> str:
        if type_def is object:
            return cls.name
        if type_def.__module__ in {None, "builtins", "__main__"}:
            return f"{cls.name}[{type_def.__qualname__}]"
        return f"{cls.name}[{type_def.__module__}.{type_def.__qualname__}]"

    @classmethod
    def resolve(cls, type_def: str = None) -> AtomicType:
        if type_def is None:
            return cls.instance()
        return cls.instance(type_def=from_caller(type_def))


#######################
####    PRIVATE    ####
#######################


cdef object from_caller(str name, int stack_index = 0):
    """Get an arbitrary object from a parent calling context by name."""
    # NOTE: cython does not yield proper inspect.frame objects. In normal
    # python, a stack_index of 0 would reference the `from_caller` frame
    # itself, but in cython (as implemented), it refers to the first python
    # frame that is encountered (which is always the calling context).

    # split name into '.'-separated object path
    cdef list full_path = name.split(".")
    cdef object frame = inspect.stack()[stack_index].frame

    while "IGNORE_FRAME_OBJECTS" in frame.f_globals:
        frame = frame.f_back

    # get first component of full path from calling context
    cdef str first = full_path[0]
    if first in frame.f_locals:
        result = frame.f_locals[first]
    elif first in frame.f_globals:
        result = frame.f_globals[first]
    elif first in frame.f_builtins:
        result = frame.f_builtins[first]
    else:
        raise ValueError(
            f"could not find {name} in calling frame at position {stack_index}"
        )

    # walk through any remaining path components
    cdef str component
    for component in full_path[1:]:
        result = getattr(result, component)

    # return fully resolved object
    return result
