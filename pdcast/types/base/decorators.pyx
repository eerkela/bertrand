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


def register(class_: type = None, *, cond: bool = True):
    """Validate an AtomicType definition and add it to the registry.

    Note: Any decorators above this one will be ignored during validation.
    """
    def register_decorator(_class_):
        if not issubclass(_class_, ScalarType):
            raise TypeError(
                "`@register` can only be applied to AtomicType and "
                "AdapterType subclasses"
            )
        if cond:
            AtomicType.registry.add(_class_)

        return _class_

    if class_ is None:
        return register_decorator
    return register_decorator(class_)


def generic(_class: type):
    """Class decorator to mark generic AtomicType definitions.

    Generic types are backend-agnostic and act as wildcard containers for
    more specialized subtypes.  For instance, the generic "int" can contain
    the backend-specific "int[numpy]", "int[pandas]", and "int[python]"
    subtypes, which can be resolved as shown. 
    """
    # NOTE: something like this would normally be handled using a decorating
    # class rather than a function.  Doing it this way (patching) has the
    # advantage of preserving the original type for issubclass() checks
    if not issubclass(_class, AtomicType):
        raise TypeError(f"`@generic` can only be applied to AtomicTypes")

    # verify init is empty.  NOTE: cython __init__ is not introspectable.
    if (
        _class.__init__ != AtomicType.__init__ and
        inspect.signature(_class).parameters
    ):
        raise TypeError(
            f"To be generic, {_class.__name__}.__init__() cannot "
            f"have arguments other than self"
        )

    # remember original equivalents
    cdef dict orig = {k: getattr(_class, k) for k in ("instance", "resolve")}

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
        # NOTE: in this context, cls is an alias for _class
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
            specific._family = cls._family  # TODO: check if this is already given?
            specific._is_generic = False
            specific.name = cls.name
            specific._generic = cls
            cls._backends[backend] = specific
            specific._is_boolean = specific._is_boolean or cls._is_boolean
            specific._is_numeric = specific._is_numeric or cls._is_numeric
            specific.registry.flush()
            return specific

        return decorator

    # overwrite class attributes
    _class._is_generic = True
    _class._backend = None

    # patch in new methods
    loc = locals()
    for k in orig:
        setattr(_class, k, loc[k])
    _class.register_backend = register_backend
    return _class





def subtype(supertype: type):
    """Class decorator to establish type hierarchies."""
    if not issubclass(supertype, AtomicType):
        raise TypeError(f"`supertype` must be a subclass of AtomicType")

    def decorator(class_def: type):
        if not issubclass(class_def, AtomicType):
            raise TypeError(
                f"`@subtype()` can only be applied to AtomicType definitions"
            )

        # break circular references
        ref = supertype
        while ref is not None:
            if ref is class_def:
                raise TypeError(
                    "Type hierarchy cannot contain circular references"
                )
            ref = ref._parent

        # check type is not already registered
        if class_def._parent:
            raise TypeError(
                f"AtomicTypes can only be registered to one supertype at a "
                f"time (`{class_def.__name__}` is currently registered to "
                f"`{class_def._parent.__name__}`)"
            )

        # inherit supertype attributes
        class_def._family = supertype._family  # TODO: check if this is already given?

        # overwrite class attributes
        class_def._parent = supertype
        supertype._children.add(class_def)
        class_def._is_boolean = class_def._is_boolean or supertype._is_boolean
        class_def._is_numeric = class_def._is_numeric or supertype._is_numeric
        AtomicType.registry.flush()
        return class_def

    return decorator
