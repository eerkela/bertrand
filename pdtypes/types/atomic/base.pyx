from collections import namedtuple
import inspect
from itertools import combinations
import regex as re
from typing import Any, Callable, Iterator

cimport cython
cimport numpy as np
import numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE
from pdtypes.util.structs cimport LRUDict


# TODO: remove all references to backend in AtomicType methods.
# -> the only attributes strictly necessary for program flow are
# typedef, dtype, na_value, slug
# -> itemsize is necessary to overwrite existing itemsize setting


# TODO: register_subtype shouldn't exist.  Always use register_supertype.
# This forces some nodes to be root nodes and outlaws circular references.
# Add an optional overwrite keyword that controls whether or not to replace
# an existing supertype with a new one.


#########################
####    CONSTANTS    ####
#########################


# a null value other than None
cdef NullValue null = NullValue()


# namedtuple specifying everything needed to construct an AtomicType object
# from a given alias.
cdef type AliasInfo = namedtuple("AliasInfo", "type, default_kwargs")
cdef type remember = namedtuple("remember", "value, hash")


# strings associated with every possible missing value (for scalar parsing)
cdef dict na_strings = {
    str(pd.NA).lower(): pd.NA,
    str(pd.NaT).lower(): pd.NaT,
    str(np.nan).lower(): np.nan,
}


# TODO: these go in individual AtomicType definitions, under cls.flyweights
# cdef LRUDict datetime64_registry = LRUDict(maxsize=cache_size)
# cdef LRUDict decimal_registry = LRUDict(maxsize=cache_size)
# cdef LRUDict object_registry = LRUDict(maxsize=cache_size)
# cdef LRUDict timedelta64_registry = LRUDict(maxsize=cache_size)


#######################
####    HELPERS    ####
#######################


def from_caller(name: str, stack_index: int = 1):
    """Get an arbitrary object from a parent calling context."""
    # split name into '.'-separated object path
    full_path = name.split(".")
    frame = inspect.stack()[stack_index].frame

    # get first component of full path from calling context
    first = full_path[0]
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
    for component in full_path[1:]:
        result = getattr(result, component)

    # return fully resolved object
    return result


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


cdef int validate(
    type subclass,
    str name,
    object expected_type = None,
    object signature = None,
) except -1:
    """Ensure that a subclass defines a particular named attribute."""
    # ensure attribute exists
    if not hasattr(subclass, name):
        raise TypeError(
            f"{subclass.__name__} must define a `{name}` attribute"
        )

    # get attribute value
    attr = getattr(subclass, name)

    # if an expected type is given, check it
    if expected_type is not None:
        if expected_type in ("method", "classmethod"):
            bound = getattr(attr, "__self__", None)
            if expected_type == "method" and bound:
                raise TypeError(
                    f"{subclass.__name__}.{name}() must be an instance method"
                )
            elif expected_type == "classmethod" and bound != subclass:
                raise TypeError(
                    f"{subclass.__name__}.{name}() must be a classmethod"
                )
        elif not isinstance(attr, expected_type):
            raise TypeError(
                f"{subclass.__name__}.{name} must be of type {expected_type}, "
                f"not {type(attr)}"
            )

    # if attribute has a signature match, check it
    if signature is not None:
        attr_sig = inspect.signature(attr)
        if attr_sig.parameters != signature.parameters:
            raise TypeError(
                f"{subclass.__name__}.{name}() must have the following "
                f"signature: {str(signature)}, not {attr_sig}"
            )

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

    #################################
    ####    VALIDATE SUBCLASS    ####
    #################################

    cdef int validate_aliases(self, type subclass) except -1:
        """Ensure that a subclass of AtomicType has an `aliases` dictionary
        and that none of its aliases overlap with another registered
        AtomicType.
        """
        validate(
            subclass=subclass,
            name="aliases",
            expected_type=dict
        )

        # ensure that no aliases are already registered to another AtomicType
        for k in subclass.aliases:
            if k in self.aliases:
                raise TypeError(
                    f"{subclass.__name__} alias {repr(k)} is already "
                    f"registered to {self.aliases[k].type.__name__}"
                )

    cdef int validate_generate_slug(self, type subclass) except -1:
        """Ensure that a subclass of AtomicType has a `generate_slug()`
        classmethod and that its signature matches __init__.
        """
        validate(
            subclass=subclass,
            name="generate_slug",
            expected_type="classmethod",
            signature=inspect.signature(subclass)
        )

    cdef int validate_name(self, type subclass) except -1:
        """Ensure that a subclass of AtomicType has a unique `name` attribute
        associated with it.
        """
        validate(
            subclass=subclass,
            name="name",
            expected_type=str,
        )

        # ensure subclass.name is unique
        observed_names = {x.name for x in self.atomic_types}
        if subclass.name in observed_names:
            raise TypeError(
                f"{subclass.__name__}.name ({repr(subclass.name)}) must be "
                f"unique (not one of {observed_names})"
            )

    cdef int validate_replace(self, type subclass) except -1:
        """Ensure that a subclass of AtomicType defines a `replace()` method.
        """
        validate(
            subclass=subclass,
            name="replace",
            expected_type="method"
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
        self.validate_replace(new_type)
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
        `from_string()` constructor.
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
        typedef: type,
        dtype: object,
        na_value: Any,
        itemsize: int,
        slug: str
    ):
        self.backend = backend
        self.typedef = typedef
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
    def from_string(cls, *args: str) -> AtomicType:
        """An alias for `AtomicType.instance()` that gets called during
        resolution of string-based type specifiers.  Override this if your type
        implements custom logic at this step.

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

        # rebuild regex patterns
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
        cls.registry.flush()  # rebuild regex patterns

    @classmethod
    def remove_alias(cls, alias: Any) -> None:
        del cls.aliases[alias]
        cls.registry.flush()  # rebuild regex patterns

    @classmethod
    def clear_aliases(cls) -> None:
        cls.aliases.clear()
        cls.registry.flush()  # rebuild regex patterns

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

    def parse(self, input_str: str) -> Any:
        lower = input_str.lower()
        if lower in na_strings:
            return na_strings[lower]
        # TODO: check if self.typedef is None and raise a ValueError
        # This should never occur.
        return self.typedef(input_str)

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

        # validate subclass properties and add to registry, if directed
        if add_to_registry:
            cls.registry.add(cls)

        # initialize required fields as if cls were an orphan type
        cls._subtypes = frozenset()
        cls._supertype = None

        # create an LRU flyweight cache (or use default shared dict)
        if cache_size is not None:
            cls.flyweights = LRUDict(maxsize=cache_size)





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
