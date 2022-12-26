from collections import namedtuple
import inspect
from itertools import combinations
import regex as re
from typing import Any, Iterator

cimport cython
cimport numpy as np
import numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE
from pdtypes.util.structs cimport LRUDict


# TODO: remove all references to backend in AtomicType methods.



# TODO: register_subtype shouldn't exist.  Always use register_supertype.
# This forces some nodes to be root nodes and outlaws circular references.
# Add an optional overwrite keyword that controls whether or not to replace
# an existing supertype with a new one.


# TODO: add negative versions for each operation
# subtype/supertype -> orphan()
# alias -> remove_alias()


# TODO: move ElementType into pdtypes.types.element_type.py
# -> ElementType gets broken up into AdapterType, which serves as a base class
# for SparseType, CategoricalType

# SparseType(Int64Type("numpy"), fill_value=pd.NA)
# CategoricalType(Int64Type("numpy"), levels=(3, 4, 5))
# SparseType(CategoricalType(Int64Type("numpy"), levels=(3, 4, 5)), fill_value=pd.NA)
# == sparse[categorical[int64[numpy]]]
# == sparse[categorical[int64[numpy], (3, 4, 5)], <NA>]
# the second one requires either a .parse method or you just run str() on
# levels, fill_value and compare for equality.  It also disallows
# shorthand forms, like sparse[int, float, bool]


#########################
####    CONSTANTS    ####
#########################


# size (in items) to use for LRU cache registries
# cdef unsigned short cache_size = 64


# namedtuple specifying everything needed to construct an AtomicType object
# from a given alias.
cdef type AliasInfo = namedtuple("AliasInfo", "type, default_kwargs")
cdef type remember = namedtuple("remember", "value, hash")


# TODO: these go in individual AtomicType definitions, under cls.flyweights
# cdef LRUDict datetime64_registry = LRUDict(maxsize=cache_size)
# cdef LRUDict decimal_registry = LRUDict(maxsize=cache_size)
# cdef LRUDict object_registry = LRUDict(maxsize=cache_size)
# cdef LRUDict timedelta64_registry = LRUDict(maxsize=cache_size)


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
    every subtype definition that is contained within it or any of its
    children.
    """
    cdef set result = set()
    _traverse_subtypes(atomic_type, result)  # in-place
    return result


#######################
####    CLASSES    ####
#######################


cdef class AtomicTypeRegistry:

    cdef:
        list atomic_types
        long long int hash
        object _aliases
        object _regex
        object _resolvable
        
    def __init__(self):
        self.atomic_types = []
        self.update_hash()

    ###################################
    ####    VALIDATE PROPERTIES    ####
    ###################################

    cdef void validate_aliases(self, type subclass):
        """Ensure that a subclass of AtomicType has an `aliases` dictionary
        and that none of its aliases overlap with another registered
        AtomicType.
        """
        # ensure subclass.aliases exists
        if not hasattr(subclass, "aliases"):
            raise TypeError(
                f"{subclass.__name__} must define an `aliases` class "
                f"attribute mapping aliases accepted by `resolve_type()` to "
                f"their corresponding `.instance()` **kwargs."
            )

        # ensure subclass.aliases is a dictionary
        if not isinstance(subclass.aliases, dict):
            raise TypeError(
                f"{subclass.__name__}.aliases must be a dict-like object "
                f"mapping aliases accepted by `resolve_type()` to their "
                f"corresponding `.instance()` **kwargs."
            )

        # ensure that no aliases are already registered to another AtomicType
        for k in subclass.aliases:
            if k in self.aliases:
                raise TypeError(
                    f"{subclass.__name__} alias {repr(k)} is already "
                    f"registered to {self.aliases[k].type.__name__}"
                )

    cdef void validate_generate_slug(self, type subclass):
        """Ensure that a subclass of AtomicType has a `generate_slug()`
        classmethod and that its signature matches __init__.
        """
        # ensure that subclass.generate_slug() exists
        if not hasattr(subclass, "generate_slug"):
            raise TypeError(
                f"AtomicType subclass {subclass.__name__} must define a "
                f"`generate_slug()` classmethod with the same parameters as "
                f"`__init__()`."
            )

        # TODO: ensure that subclass.generate_slug() is a classmethod

        # ensure that signatures match
        sig1 = inspect.signature(subclass.generate_slug).parameters
        sig2 = inspect.signature(subclass).parameters
        if sig1 != sig2:
            raise TypeError(
                f"{subclass.__name__}.generate_slug() must have the same "
                f"parameters as `{subclass.__name__}.__init__()`"
            )

    cdef void validate_name(self, type subclass):
        """Ensure that a subclass of AtomicType has a unique `name` attribute
        associated with it.
        """
        # ensure subclass.name exists
        if not hasattr(subclass, "name"):
            raise TypeError(
                f"AtomicType subclass {subclass.__name__} must define a "
                f"unique `name` class attribute to use as the basis for "
                f"associated string representations."
            )

        # ensure subclass.name is a string
        if not isinstance(subclass.name, str):
            raise TypeError(
                f"{subclass.__name__}.name must be a string, not "
                f"{type(subclass.name)}."
            )

        # ensure subclass.name is unique
        observed_names = {x.name for x in self.atomic_types}
        if subclass.name in observed_names:
            raise TypeError(
                f"{subclass.__name__}.name ({repr(subclass.name)}) must be "
                f"unique (not one of {observed_names})"
            )

    ############################
    ####    RECORD STATE    ####
    ############################

    def needs_updating(self, prop) -> bool:
        """Check if a `remember()`-ed registry property is out of date."""
        return prop is None or prop.hash != self.hash

    cdef void update_hash(self):
        """Hash the registry's internal state, for use in cached properties."""
        self.hash = hash(tuple(self.atomic_types))
    
    ################################
    ####    ADD/REMOVE TYPES    ####update_hash
    ################################

    def add(self, new_type: type) -> None:
        """Add an AtomicType subclass to the registry."""
        if not issubclass(new_type, AtomicType):
            raise TypeError(
                f"`new_type` must be a subclass of AtomicType, not "
                f"{type(new_type)}"
            )

        # validate AtomicType properties
        self.validate_name(new_type)
        self.validate_generate_slug(new_type)
        self.validate_aliases(new_type)

        # add type to registry and update hash
        self.atomic_types.append(new_type)
        self.update_hash()

    def flush(self):
        """Reset the registry's internal state, forcing every property to be
        recomputed.
        """
        self.hash += 1

    def remove(self, old_type: type) -> None:
        """Remove an AtomicType subclass from the registry."""
        self.atomic_types.remove(old_type)
        self.update_hash()

    def clear(self):
        """Clear the AtomicType registry of all AtomicType subclasses."""
        self.atomic_types.clear()
        self.update_hash()

    ###############################
    ####    LIVE PROPERTIES    ####
    ###############################

    @property
    def aliases(self) -> dict[str, AliasInfo]:
        """Return an up-to-date dictionary of all of the AtomicType aliases
        that are currently recognized by `resolve_type()`.
        
        The returned dictionary maps aliases of any kind to namedtuple
        (`AliasInfo`) objects, which contain the corresponding AtomicType
        subclass and default arguments to that class's `.instance()`
        method, if any are specified.
        """
        # check if cache is out of date
        if self.needs_updating(self._aliases):
            result = {
                k: AliasInfo(
                    type=c,
                    default_kwargs=v
                )
                for c in self.atomic_types
                for k, v in c.aliases.items()
            }
            self._aliases = remember(result, self.hash)

        # return cached value
        return self._aliases.value

    @property
    def regex(self) -> re.Pattern:
        """Compile a regular expression to match any registered AtomicType
        name or alias, as well as any arguments that may be passed to its
        `from_typespec()` constructor.
        """
        # check if cache is out of date
        if self.needs_updating(self._regex):
            # fastfail: empty case
            if not self.atomic_types:
                result = re.compile(".^")  # matches nothing

            # update using string aliases from every registered subtype
            else:
                # automatically escape reserved regex characters
                string_aliases = [
                    re.escape(k) for k in self.aliases if isinstance(k, str)
                ]

                # sort into reverse order based on length
                string_aliases.sort(key=len, reverse=True)

                # join with regex OR and compile regex
                result = re.compile(
                    rf"(?P<type>{'|'.join(string_aliases)})"
                    rf"(\[(?P<args>([^\[\]]|(?R))*)\])?"
                )

            # remember result
            self._regex = remember(result, self.hash)

        # return cached value
        return self._regex.value

    @property
    def resolvable(self) -> re.Pattern:
        # check if cache is out of date
        if self.needs_updating(self._resolvable):
            # wrap self.regex in ^$ to match the entire string and allow for
            # comma-separated repetition of AtomicType patterns.
            pattern = rf"(?P<atomic>{self.regex.pattern})(,\s*(?&atomic))*"
            lead = r"((CompositeType\(\{)|\{)?"
            follow = r"((\}\))|\})?"

            # compile regex
            result = re.compile(rf"{lead}(?P<body>{pattern}){follow}")

            # remember result
            self._resolvable = remember(result, self.hash)

        # return cached value
        return self._resolvable.value

    #############################
    ####    MAGIC METHODS    ####
    #############################

    def __contains__(self, val) -> bool:
        return val in self.atomic_types

    def __hash__(self) -> int:
        return self.hash

    def __iter__(self):
        return iter(self.atomic_types)

    def __len__(self) -> int:
        return len(self.atomic_types)

    def __str__(self) -> str:
        return str(self.atomic_types)

    def __repr__(self) -> str:
        return repr(self.atomic_types)


cdef class AtomicType:

    registry: TypeRegistry = AtomicTypeRegistry()
    flyweights: dict[int: AtomicType] = {}

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
    def instance(cls, *args, **kwargs) -> AtomicType:
        """Base flyweight constructor."""
        # generate slug and compute hash
        cdef long long _hash = hash(cls.generate_slug(*args, **kwargs))

        # get previous flyweight, if one exists
        cdef AtomicType result = cls.flyweights.get(_hash, None)
        if result is None:  # create new flyweight
            result = cls(*args, **kwargs)
            cls.flyweights[_hash] = result

        # return flyweight
        return result

    @classmethod
    def from_typespec(cls, *args) -> AtomicType:
        """An alias for `AtomicType.instance()` that gets called during
        typespec resolution.  Override this if your type accepts more arguments
        than the standard `backend`.

        .. Note: The inputs to each argument will always be strings.
        """
        return cls.instance(*args)

    @classmethod
    def orphan(cls) -> None:
        """Remove an AtomicType from the type hierarchy."""
        # for every subtype registered to this class, replace its supertype
        # with the class's supertype.  If the class is a root type, then every
        # subtype will become a root type as well.
        for t in cls._subtypes:
            t._supertype = cls._supertype

        # if the class has a supertype, remove it from the supertype's
        # `.subtypes` field and substitute the class's subtypes instead.
        if cls._supertype:
            cls._supertype._subtypes -= {cls}
            cls._supertype._subtypes |= cls._subtypes

        # clear the class's supertype/subtypes
        cls._supertype = None
        cls._subtypes -= cls._subtypes

        # flush registry to synchronize instances
        cls.registry.flush()

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
                f"`{cls._supertype.__name__}`)"
            )

        # register supertype
        cls._supertype = supertype

        # append type to supertype.subtypes
        supertype._subtypes |= {cls}

        # flush registry to synchronize instances
        cls.registry.flush()

    @classmethod
    def register_subtype(cls, subtype: type) -> None:
        # check subtype is a subclass of AtomicType
        if not issubclass(subtype, AtomicType):
            raise TypeError(f"`subtype` must be a subclass of AtomicType")

        # delegate to subtype.register_supertype()
        subtype.register_supertype(cls)

    @classmethod
    def register_alias(cls, alias: Any, defaults: dict) -> None:
        cls.aliases[alias] = defaults

        # flush registry to synchronize instances
        cls.registry.flush()

    @classmethod
    def remove_alias(cls, alias: Any) -> None:
        del cls.aliases[alias]

        # flush registry to synchronize instances
        cls.registry.flush()

    @property
    def root(self) -> AtomicType:
        if self.supertype is None:
            return self
        return self.supertype.root

    @property
    def subtypes(self) -> frozenset:
        """Override this if you want to change how a subtype is called."""
        # update cache if instance out of date
        if self.registry.needs_updating(self._subtypes_cache):
            subtype_defs = traverse_subtypes(type(self))

            # treat backend=None as wildcard
            if self.backend is None:
                result = frozenset({
                    sub.instance(backend=back)
                    for sub in subtype_defs
                    for back in self._backends
                    if back in sub._backends  # ignore invalid subtype backend
                })
            else:
                # use specified backend
                result = frozenset({
                    sub.instance(backend=self.backend)
                    for sub in subtype_defs
                    if self.backend in sub._backends  # ignore invalid subtype backend
                })

            # remember 
            self._subtypes_cache = remember(result, hash(self.registry))

        # return cached value
        return self._subtypes_cache.value

    @property
    def supertype(self) -> AtomicType:
        """Override this if you want to change how a supertype is called."""
        # update cache if instance out of date
        if self.registry.needs_updating(self._supertype_cache):
            if self._supertype:
                result = self._supertype.instance(backend=self.backend)
            else:
                result = None
            self._supertype_cache = remember(result, hash(self.registry))

        # return cached value
        return self._supertype_cache.value

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

    def __repr__(self) -> str:
        return f"{type(self).__name__}(backend={repr(self.backend)})"

    def __str__(self) -> str:
        return self.slug

    @classmethod
    def __init_subclass__(
        cls,
        add_to_registry: bool = True,
        cache_size: int = None,
        **kwargs
    ):
        # cooperative inheritance
        super(AtomicType, cls).__init_subclass__(**kwargs)

        # if this type is to be registered, ensure it has required metadata
        if add_to_registry:
            cls.registry.add(cls)

        # initialize required fields as if cls were a root type
        cls._subtypes = frozenset()
        cls._supertype = None

        # add flyweight cache
        if cache_size is not None:
            cls.flyweights = LRUDict(maxsize=cache_size)


class AdapterType(AtomicType, add_to_registry=False, cache_size=64):

    def __init__(self, atomic_type: AtomicType):
        self.atomic_type = atomic_type
        super(AdapterType, self).__init__(
            backend=self.atomic_type.backend,
            object_type=self.atomic_type.object_type,
            dtype=self.atomic_type.dtype,
            na_value=self.atomic_type.na_value,
            itemsize=self.atomic_type.itemsize,
            slug=f"{self.name}[{str(self.atomic_type)}]"
        )

    @classmethod
    def register_supertype(cls, supertype: type) -> None:
        raise TypeError(f"AdapterTypes cannot have supertypes")

    @classmethod
    def register_subtype(cls, subtype: type) -> None:
        raise TypeError(f"AdapterTypes cannot have subtypes")

    @property
    def root(self) -> AtomicType:
        # TODO: these are broken due to the lack of generate_slug()
        if self.atomic_type.supertype is None:
            return self
        return self.instance(self.atomic_type.root)

    @property
    def subtypes(self) -> frozenset:
        # TODO: these are broken due to the lack of generate_slug()
        return frozenset(self.instance(t) for t in self.atomic_type.subtypes)

    @property
    def supertype(self) -> AtomicType:
        # TODO: these are broken due to the lack of generate_slug()
        result = self.atomic_type.supertype
        if result is None:
            return None
        return self.instance(result)

    def __getattr__(self, name: str) -> Any:
        return getattr(self.atomic_type, name)

    def __repr__(self) -> str:
        return f"{type(self).__name__}({repr(self.atomic_type)})"




# sparse[int64] == SparseType(Int64Type.instance(), pd.NA)




cdef class ElementType:
    # TODO: lift this out of atomic to pdtypes.types.element_type.ElementType

    def __init__(
        self,
        atomic_type: AtomicType,
        sparse: bool = False,
        categorical: bool = False,
        index: pd.Index = None
    ):
        if not (index is None or isinstance(index, pd.Index)):
            raise TypeError(
                f"`index` must be a pandas.Index object, not {type(index)}"
            )

        self.atomic_type = atomic_type
        self.sparse = sparse
        self.categorical = categorical
        self.slug = self.atomic_type.slug
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"
        self.hash = hash(self.slug)
        self.index = index

    @property
    def root(self) -> ElementType:
        return ElementType(
            self.atomic_type.root,
            sparse=self.sparse,
            categorical=self.categorical,
            index=self.index
        )

    @property
    def subtypes(self) -> frozenset[ElementType]:
        return frozenset(
            ElementType(
                x,
                sparse=self.sparse,
                categorical=self.categorical
            )
            for x in self.atomic_type.subtypes
        )

    @property
    def supertype(self) -> ElementType:
        if self.atomic_type.supertype is None:
            return None

        return ElementType(
            self.atomic_type.supertype,
            sparse=self.sparse,
            categorical=self.categorical,
            index=self.index
        )

    def bind_index(self, root: bool = False) -> pd.Series:
        if self.index is None:
            raise TypeError(f"ElementType has no index to bind")

        val = self.atomic_type.root if root else self.atomic_type
        arr = np.full(len(self.index), val, dtype="O")
        return pd.Series(arr, index=self.index)

    def __contains__(self, other: ElementType) -> bool:
        # TODO: remove ElementType hint to allow automatic resolution
        return (
            other.sparse == self.sparse and
            other.categorical == self.categorical and
            other.atomic_type in self.atomic_type
        )

    def __eq__(self, other: ElementType) -> bool:
        # TODO: remove ElementType hint to allow automatic resolution
        # TODO: this currently does not take differing indexes into account
        return self.hash == hash(other)

    def __hash__(self) -> int:
        return self.hash

    def __repr__(self) -> str:
        index = "None" if self.index is None else "pandas.Index(...)"
        return (
            f"{type(self).__name__}({repr(self.atomic_type)}, "
            f"sparse={self.sparse}, "
            f"categorical={self.categorical}, "
            f"index={index}"
            f")"
        )

    def __str__(self) -> str:
        return self.slug

    # TODO: delegate all other attributes to the wrapped ElementType
    # def __getattr__(self, name: str)



cdef class CompositeType:
    # TODO: lift this out of atomic to pdtypes.types.composite_type.CompositeType

    # TODO: in reality, .index should only be defined for these types, not
    # ElementType.  It's fairly heavy all things considered.

    def __init__(self, element_types = None):
        if element_types is None:
            self.element_types = set()
        else:
            self.element_types = set(element_types)

    @property
    def has_index(self) -> bool:
        """`True` if every ElementType in this CompositeType has an index and
        none of their indices intersect.
        """
        return (
            len(self) and
            all(x.index is not None for x in self.element_types) and
            not any(
                len(x.index.intersection(y))
                for x, y in combinations(self.element_types, r=2)
            )
        )

    @property
    def index(self) -> pd.Series:
        if not self.has_index:
            return None

        indices = [x.bind_index() for x in self.element_types]
        result = indices[0]
        for series in indices[1:]:
            result.update(series)
        return result

    @property
    def root_index(self) -> pd.Series:
        if not self.has_index:
            return None

        indices = [x.bind_index(root=True) for x in self.element_types]
        result = indices[0]
        for series in indices[1:]:
            result.update(series)
        return result

    def __iter__(self) -> Iterator[ElementType]:
        return iter(self.element_types)

    def __len__(self) -> int:
        return len(self.element_types)

    def __repr__(self) -> str:
        slugs = ", ".join(str(x) for x in self.element_types)
        return f"{type(self).__name__}({{{slugs}}})"

    def __str__(self) -> str:
        slugs = ", ".join(str(x) for x in self.element_types)
        return f"{{{slugs}}}"
