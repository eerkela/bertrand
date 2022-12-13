cimport cython
cimport numpy as np
import numpy as np
import pandas as pd
from typing import Any, Iterator

from pdtypes import DEFAULT_STRING_DTYPE
from pdtypes.util.structs cimport LRUDict


#########################
####    CONSTANTS    ####
#########################


# size (in items) to use for LRU cache registries
cdef unsigned short cache_size = 64


# Flyweight registries
cdef dict shared_registry = {}


# TODO: these go in individual AtomicType files
cdef LRUDict datetime64_registry = LRUDict(maxsize=cache_size)
cdef LRUDict decimal_registry = LRUDict(maxsize=cache_size)
cdef LRUDict object_registry = LRUDict(maxsize=cache_size)
cdef LRUDict timedelta64_registry = LRUDict(maxsize=cache_size)


#######################
####    HELPERS    ####
#######################


cdef void _traverse_subtypes(type atomic_type, set result):
    """Recursive helper for traverse_subtypes()"""
    result.add(atomic_type)
    for subtype in atomic_type._subtypes:
        _traverse_subtypes(subtype, result=result)


cdef set traverse_subtypes(type atomic_type):
    """Traverse through an AtomicType's subtype tree, recursively gathering
    every subtype definition that contained within it or any of its children.
    """
    cdef set result = set()
    _traverse_subtypes(atomic_type, result)  # in-place
    return result


#######################
####    CLASSES    ####
#######################


cdef class AtomicType:

    type_registry: list[type] = []

    def __init__(
        self,
        backend: str,
        object_type: type,
        dtype: object,
        na_value: Any,
        itemsize: int,
        slug: str
    ):
        self.backend = backend
        self.object_type = object_type
        self.dtype = dtype
        self.na_value = na_value
        self.itemsize = itemsize
        self.slug = slug
        self.hash = hash(slug)

    @classmethod
    def instance(cls, backend: str = None) -> AtomicType:
        """Base flyweight constructor."""
        # generate slug
        cdef str slug = f"{cls._name}"
        if backend is not None:
            slug += f"[{backend}]"

        # compute hash
        cdef long long _hash = hash(slug)

        # get previous flyweight, if one exists
        cdef AtomicType result = shared_registry.get(_hash, None)
        if result is None:
            result = cls(backend=backend)
            shared_registry[_hash] = result

        # return flyweight
        return result

    @classmethod
    def from_typespec(cls, backend: str = None) -> AtomicType:
        """An alias for `AtomicType.instance()` that gets called during
        typespec resolution.  Override this if your type accepts more arguments
        than the standard `backend`.

        .. Note: The inputs to each argument will always be strings.
        """
        return cls.instance(backend=backend)

    @classmethod
    def register_supertype(cls, supertype: type) -> None:
        # check supertype is a subclass of AtomicType
        if not issubclass(supertype, AtomicType):
            raise TypeError(f"`supertype` must be a subclass of AtomicType")

        # break circular references
        if supertype is cls:
            raise TypeError("Type cannot be registered to itself")

        # check type is unregistered
        if cls._supertype:
            raise TypeError(
                f"Types can only be registered to one supertype at a time "
                f"(`{cls.__name__}` is currently registered to "
                f"`{cls._supertype[0].__name__}`)"
            )

        # register supertype
        cls._supertype = supertype

        # append type to supertype.subtypes
        supertype._subtypes |= {cls}

    @classmethod
    def register_subtype(cls, subtype: type) -> None:
        # check subtype is a subclass of AtomicType
        if not issubclass(subtype, AtomicType):
            raise TypeError(f"`subtype` must be a subclass of AtomicType")

        # delegate to subtype.register_supertype()
        subtype.register_supertype(cls)

    @property
    def subtypes(self) -> frozenset:
        """Override this if you want to change how a subtype is called."""
        reference_hash = hash(type(self)._subtypes)

        # update cache if instance out of date
        if self._subtypes_hash != reference_hash:
            subtype_defs = traverse_subtypes(type(self))

            # treat backend=None as wildcard
            if self.backend is None:
                self._subtypes_cache = frozenset({
                    sub.instance(backend=back)
                    for sub in subtype_defs
                    for back in self._backends
                    if back in sub._backends  # ignore invalid subtype backend
                })
            else:
                # use specified backend
                self._subtypes_cache = frozenset({
                    sub.instance(backend=self.backend)
                    for sub in subtype_defs
                    if self.backend in sub._backends  # ignore invalid subtype backend
                })

            # update instance hash
            self._subtypes_hash = reference_hash

        return self._subtypes_cache

    @property
    def supertype(self) -> AtomicType:
        """Override this if you want to change how a supertype is called."""
        reference_hash = hash(type(self)._supertype)

        # update cache if instance out of date
        if self._supertype_hash != reference_hash:
            if self._supertype:
                self._supertype_cache = (
                    self._supertype.instance(backend=self.backend)
                )
            else:
                self._supertype_cache = None

            # update instance hash
            self._supertype_hash = reference_hash

        return self._supertype_cache

    def is_subtype(self, other: AtomicType) -> bool:
        # TODO: update to support collective tests via CompositeType
        return self in other

    def __contains__(self, other: AtomicType) -> bool:
        # TODO: update to support collective tests via CompositeType
        return other in self.subtypes

    def __eq__(self, other: AtomicType) -> bool:
        # TODO: update to support dynamic resolution via resolve_dtype()
        return self.hash == hash(other)

    def __hash__(self) -> int:
        return self.hash

    def __iter__(self) -> Iterator[AtomicType]:
        return iter(self.subtypes)

    def __len__(self) -> int:
        return len(self.subtypes)

    def __repr__(self) -> str:
        return f"{type(self).__name__}(backend={self.backend})"

    def __str__(self) -> str:
        return self.slug

    @classmethod
    def __init_subclass__(cls, **kwargs):
        super(AtomicType, cls).__init_subclass__(**kwargs)
        cls.type_registry.append(cls)
        cls._subtypes = frozenset()
        cls._supertype = None
