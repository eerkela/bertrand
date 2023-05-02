"""This type describes a suite of class decorators that can be applied to
``AtomicType`` subclasses in order to customize their behavior and link them
to other types.
"""
import inspect
from typing import Any

cimport pdcast.resolve as resolve
import pdcast.resolve as resolve

from .adapter import AdapterType
from .atomic import ScalarType, AtomicType


# TODO: @backend should be an independent decorator, which resolves the given
# type and checks that it is generic

# @subtype("signed[numpy]")
# @backend("int8", "numpy")



def register(class_: type = None, *, cond: bool = True):
    """Validate an AtomicType definition and add it to the registry.

    Note: Any decorators above this one will be ignored during validation.
    """
    def register_decorator(class_def):
        if not issubclass(class_def, ScalarType):
            raise TypeError(
                "`@register` can only be applied to AtomicType and "
                "AdapterType subclasses"
            )
        if cond:
            AtomicType.registry.add(class_def)

        return class_def

    if class_ is None:
        return register_decorator
    return register_decorator(class_)


def generic(class_: type):
    """Class decorator to mark generic AtomicType definitions.

    Generic types are backend-agnostic and act as wildcard containers for
    more specialized subtypes.  For instance, the generic "int" can contain
    the backend-specific "int[numpy]", "int[pandas]", and "int[python]"
    subtypes, which can be resolved as shown. 
    """
    # NOTE: something like this would normally be handled using a decorating
    # class rather than a function.  Doing it this way (patching) has the
    # advantage of preserving the original type for issubclass() checks
    if not issubclass(class_, AtomicType):
        raise TypeError(f"`@generic` can only be applied to AtomicTypes")

    # verify init is empty.  NOTE: cython __init__ is not introspectable.
    if (
        class_.__init__ != AtomicType.__init__ and
        inspect.signature(class_).parameters
    ):
        raise TypeError(
            f"To be generic, {class_.__qualname__}.__init__() cannot "
            f"have arguments other than self"
        )

    # remember original equivalents
    cdef dict orig = {k: getattr(class_, k) for k in ("instance", "resolve")}

    @classmethod
    def instance(cls, backend: str = None, *args, **kwargs) -> AtomicType:
        if backend is None:
            return orig["instance"](*args, **kwargs)
        extension = cls._backends.get(backend, None)
        if extension is None:
            raise TypeError(
                f"{cls.name} backend not recognized: {repr(backend)}"
            )
        return extension.instance(*args, **kwargs)

    @classmethod
    def resolve(cls, backend: str = None, *args: str) -> AtomicType:
        if backend is None:
            return orig["resolve"](*args)

        # if a specific backend is given, resolve from its perspective
        specific = cls._backends.get(backend, None)
        if specific is not None and specific not in cls.registry:
            specific = None
        if specific is None:
            raise TypeError(
                f"{cls.name} backend not recognized: {repr(backend)}"
            )
        return specific.resolve(*args)

    @classmethod
    def register_backend(cls, backend: str):
        # NOTE: in this context, cls is an alias for class_
        def decorator(specific: type):
            if not issubclass(specific, AtomicType):
                raise TypeError(
                    f"`generic.register_backend()` can only be applied to "
                    f"AtomicType definitions"
                )

            # ensure backend is unique
            if backend in cls._backends:
                raise TypeError(
                    f"`backend` must be unique, not one of "
                    f"{set(cls._backends)}"
                )

            # ensure backend is self-consistent
            if specific._backend is None:
                specific._backend = backend
            elif backend != specific._backend:
                raise TypeError(
                    f"backends must match ({repr(backend)} != "
                    f"{repr(specific._backend)})"
                )

            # inherit generic attributes
            specific._is_generic = False
            specific._generic = cls
            specific.name = cls.name
            cls._backends[backend] = specific
            specific.registry.flush()
            return specific

        return decorator

    # overwrite class attributes
    class_._is_generic = True
    class_._backend = None

    # patch in new methods
    loc = locals()
    for k in orig:
        setattr(class_, k, loc[k])
    class_.register_backend = register_backend
    return class_


def subtype(supertype: type):
    """Class decorator to establish type hierarchies."""
    if not issubclass(supertype, AtomicType):
        raise TypeError(f"`supertype` must be a subclass of AtomicType")

    def decorator(subtype_: type):
        """Modify a subtype, linking it to a parent type."""
        if not issubclass(subtype_, AtomicType):
            raise TypeError(
                f"`@subtype()` can only be applied to AtomicType definitions"
            )

        # break circular references
        ref = supertype
        while ref is not None:
            if ref is subtype_:
                raise TypeError(
                    "Type hierarchy cannot contain circular references"
                )
            ref = ref._parent

        # check type is not already registered
        if subtype_._parent:
            raise TypeError(
                f"AtomicTypes can only be registered to one supertype at a "
                f"time: '{subtype_.__qualname__}'' is currently registered to "
                f"'{subtype_._parent.__qualname__}'"
            )

        # overwrite class attributes
        subtype_._parent = supertype
        supertype._children.add(subtype_)
        AtomicType.registry.flush()
        return subtype_

    return decorator
