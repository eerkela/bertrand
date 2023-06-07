"""This module describes an ``ObjectType`` object, which can be used to
represent arbitrary Python data.
"""
import inspect

import numpy as np
import pandas as pd

from pdcast.resolve import resolve_type
from pdcast.util.type_hints import dtype_like, type_specifier

from .base cimport ScalarType, CompositeType
from .base import register


@register
class ObjectType(ScalarType):
    """Generic object type for arbitrary python values.
    """

    _cache_size = 64
    name = "object"
    aliases = {
        "object", "obj", "O", "pyobject", "object_", "object0",
        np.dtype(object)
    }

    def __init__(self, type_def: type = object):
        super(type(self), self).__init__(type_def=type_def)

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def from_string(self, type_def: str = None) -> ScalarType:
        """Resolve a string in the type specification mini-language.

        Notes
        -----
        This method can resolve type definitions (and even dotted variables!)
        from the calling context by inspecting the stack frame.

        >>> pdcast.resolve_type("object[int]")
        ObjectType(type_def=<class 'int'>)

        """
        if type_def is None:
            return self()
        return self(from_caller(type_def))

    #############################
    ####    CONFIGURATION    ####
    #############################

    @property
    def dtype(self) -> dtype_like:
        """Get a dtype for objects of this type."""
        # root object -> use numpy
        if self.type_def is object:
            return np.dtype("O")

        # auto-generate
        return super(type(self), self).dtype

    @property
    def type_def(self) -> type:
        """Forward `type_def` lookups to parametrized kwargs"""
        return self.kwargs["type_def"]

    @property
    def is_generic(self) -> bool:
        """Treat `type_def=object` as generic."""
        return self.type_def is object


#######################
####    PRIVATE    ####
#######################


cdef object from_caller(str name, int stack_index = 0):
    """Get an object by name from the calling context at the given index."""
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
        raise ValueError(f"could not find {repr(name)} in calling frame")

    # walk through any remaining path components
    cdef str component
    for component in full_path[1:]:
        result = getattr(result, component)

    # return fully resolved object
    return result
