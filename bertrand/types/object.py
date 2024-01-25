"""This module describes an ``ObjectType`` object, which can be used to
represent arbitrary Python data.
"""
import numpy as np

from .base import Type, TypeMeta
from .base.meta import get_from_calling_context, synthesize_dtype


class Object(Type, backend="object", cache_size=128):
    """Generic object type for arbitrary python values.
    """

    aliases = {
        object, np.dtype(object), "object", "obj", "O", "pyobject", "object_",
        "object0",
    }
    scalar = object
    dtype = np.dtype(object)

    def __class_getitem__(cls, scalar: type = object) -> TypeMeta:
        if not isinstance(scalar, type):
            raise TypeError(f"scalar must be a Python type, not {repr(scalar)}")

        result = cls.flyweight(scalar, scalar=scalar)
        result._fields["dtype"] = None  # workaround to enable synthesize_dtype()
        result._fields["dtype"] = synthesize_dtype(result)
        return result

    @classmethod
    def from_string(cls, scalar: str) -> TypeMeta:
        """Resolve a string in the type specification mini-language.

        Notes
        -----
        This method can resolve type definitions (and even dotted variables!)
        from the calling context by inspecting the stack frame.

        >>> resolve_type("object[int]")
        ObjectType(type_def=<class 'int'>)
        """
        return cls[get_from_calling_context(scalar, skip=1)]
