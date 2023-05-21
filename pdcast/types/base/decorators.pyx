"""This type describes a suite of class decorators that can be applied to
``AtomicType`` subclasses in order to customize their behavior and link them
to other types.
"""
import inspect
from types import MappingProxyType
from typing import Any

from pdcast cimport resolve
from pdcast import resolve

from .adapter import AdapterType
from .atomic cimport ScalarType, AtomicType
from .composite cimport CompositeType

from pdcast.util.type_hints import type_specifier


# TODO: we probably have to adjust @subtype/@implementation decorators to
# account for unregistered types.


# TODO: @subtype should be decoupled from @generic
# -> maybe separated into @generic, @supertype?  @supertype must be
# cooperative


# TODO: @register should transform the decorated type into its base instance.




# TODO: Using GenericType for both subtypes and backends causes
# int[numpy].generic to return a circular reference rather than pointing to int



# TODO: @backend should be an independent decorator, which resolves the given
# type and checks that it is generic

# @subtype("signed[numpy]")
# @backend("int8", "numpy")

# This could be used alongside `cond` argument of @register if that
# short-circuits the class definition.  If the type specifier could not be
# interpreted, we either silently ignore or say so manually.  This could avoid
# manual TypeRegistry interactions in 


def register(class_: type | GenericType | None = None, *, cond: bool = True):
    """Validate an AtomicType definition and add it to the registry.

    Note: Any decorators above this one will be ignored during validation.
    """
    def register_decorator(cls):
        if isinstance(cls, GenericType):
            result = cls
        else:
            if not issubclass(cls, ScalarType):
                raise TypeError(
                    "`@register` can only be applied to AtomicType and "
                    "AdapterType subclasses"
                )
            result = cls.instance()

        if False:
            AtomicType.registry.add(result)
        return result

    if class_ is None:
        return register_decorator
    return register_decorator(class_)


def generic(cls: type) -> GenericType:
    """Mark a type as generic.
    """
    return GenericType(cls)



class GenericType(ScalarType):
    """Class decorator to mark generic type definitions.

    Generic types are backend-agnostic and act as wildcard containers for
    more specialized subtypes.  For instance, the generic "int" can contain
    the backend-specific "int[numpy]", "int[pandas]", and "int[python]"
    subtypes, which can be resolved as shown. 
    """

    def __init__(self, cls):
        self._validate_class(cls)

        self.__wrapped__ = cls
        self._default = cls.instance()
        self.name = cls.name
        self.aliases = cls.aliases
        self._subtypes = CompositeType()
        self._backends = {None: self._default}

    def _validate_class(self, cls) -> None:
        """Ensure that the decorated class is valid."""
        if not issubclass(cls, AtomicType):
            raise TypeError("@generic types must inherit from AtomicType")

        # NOTE: cython __init__ is not introspectable.
        if (
            cls.__init__ != AtomicType.__init__ and
            inspect.signature(cls).parameters
        ):
            raise TypeError("@generic types cannot be parametrized")

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def instance(
        self,
        backend: str | None = None,
        *args,
        **kwargs
    ) -> AtomicType:
        """Forward constructor arguments to the appropriate implementation."""
        return self._backends[backend].instance(*args, **kwargs)

    def resolve(
        self,
        backend: str | None = None,
        *args
    ) -> AtomicType:
        """Forward constructor arguments to the appropriate implementation."""
        return self._backends[backend].resolve(*args)

    ##########################
    ####    DECORATORS    ####
    ##########################

    def subtype(self, cls: type | None = None, *, **kwargs):
        """A class decorator that adds a type as a subtype of this GenericType.
        """
        def decorator(cls: type) -> type:
            """Link the decorated type to this GenericType."""
            if not issubclass(cls, AtomicType):
                raise TypeError("@generic types can only contain AtomicTypes")

            if cls._parent:
                raise TypeError(
                    f"AtomicTypes can only be registered to one @generic type "
                    f"at a time: '{cls.__qualname__}' is currently registered "
                    f"to '{cls._parent.__qualname__}'"
                )

            curr = cls
            while curr is not None:
                if curr is self:
                    raise TypeError("@generic type cannot contain itself")
                curr = curr._parent

            cls._parent = self
            self._subtypes |= cls.instance(**kwargs)
            self.registry.flush()
            return cls

        if cls is None:
            return decorator
        return decorator(cls)

    def implementation(self, backend: str = None, **kwargs):
        """A class decorator that adds a type as an implementation of this
        type.
        """
        def decorator(cls: type) -> type:
            """Link the decorated type to this GenericType."""
            if not issubclass(cls, AtomicType):
                raise TypeError("@generic types can only contain AtomicTypes")

            if not isinstance(backend, str):
                raise TypeError(
                    f"backend specifier must be a string, not {type(backend)}"
                )

            if backend in self._backends:
                raise TypeError(
                    f"backend specifier must be unique: {repr(backend)} is "
                    f"already registered to {str(self._backends[backend])}"
                )

            # ensure backend is self-consistent
            if cls._backend is None:
                cls._backend = backend
            elif backend != cls._backend:
                raise TypeError(
                    f"backend specifiers must match ({repr(backend)} != "
                    f"{repr(cls._backend)})"
                )

            cls._is_generic = False
            cls._generic = self
            cls.name = self.name
            self._backends[backend] = cls.instance(**kwargs)
            self.registry.flush()
            return cls

        return decorator

    #########################
    ####    HIERARCHY    ####
    #########################

    @property
    def is_generic(self) -> bool:
        return True

    @property
    def generic(self) -> AtomicType:
        return self

    @property
    def subtypes(self) -> CompositeType:
        return self._subtypes.copy()

    @property
    def backends(self) -> dict:
        return MappingProxyType(self._backends)

    @property
    def children(self) -> CompositeType:
        return self._subtypes | CompositeType(self._backends.values())

    #################################
    ####    COMPOSITE PATTERN    ####
    #################################

    @property
    def default(self) -> AtomicType:
        """The concrete type that this generic type defaults to.

        This will be used whenever the generic type is specified without an
        explicit backend.
        """
        return self._default

    @default.setter
    def default(self, val: type_specifier) -> None:
        if val is None:
            del self.default
        else:
            val = resolve.resolve_type(val)
            if isinstance(val, CompositeType):
                raise ValueError
            if val not in self.children:
                raise KeyError

            self._default = val

    @default.deleter
    def default(self) -> None:
        self._default = self._backends[None]

    def __getattr__(self, name: str) -> Any:
        return getattr(self.default, name)

    def __getitem__(self, key: str) -> AtomicType:
        # this should be part of the AtomicType interface
        return self._backends[key[:1]].instance(*key[1:])
