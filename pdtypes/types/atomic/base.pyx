import inspect
from itertools import combinations
import regex as re  # using alternate regex
from typing import Any, Iterator

cimport cython
cimport numpy as np
import numpy as np
import pandas as pd

from pdtypes.util.structs cimport LRUDict

import pdtypes.types.resolve as resolve


# TODO: update CompositeType
# - index
# - automatic resolution
# - expand()/collapse()
# etc.


# TODO: move from_caller to object.pyx.


#########################
####    CONSTANTS    ####
#########################


# strings associated with every possible missing value (for scalar parsing)
cdef dict na_strings = {
    str(None).lower(): None,
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
    for subtype in atomic_type._subtype_defs:
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


cdef class AliasInfo:

    def __init__(self, type base, dict defaults):
        self.base = base
        self.defaults = defaults


cdef class CacheValue:

    def __init__(self, object value, long long hash):
        self.value = value
        self.hash = hash


cdef class BaseType:
    """Base type for AtomicType and CompositeType objects.  This has no
    interface of its own and merely serves to anchor the inheritance of the
    aforementioned objects.  Since Cython does not support true Union types,
    this is the simplest way of coupling them reliably in the Cython layer.
    """
    pass


cdef class AtomicTypeRegistry:
    """A registry containing all of the AtomicType subclasses that are
    currently recognized by `resolve_type()` and related infrastructure.

    This is a global object attached to the base AtomicType class definition.
    It can be accessed through any of its instances, and it is updated
    automatically whenever a class inherits from AtomicType.  The registry
    itself contains methods to validate and synchronize subclass behavior,
    including the maintenance of a set of regular expressions that account for
    every known AtomicType alias, along with their default arguments.  These
    lists are updated dynamically each time a new alias and/or type is added
    to the pool.
    """

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

    cdef int validate_kwargs(self, type subclass) except -1:
        """Ensure that a subclass of AtomicType defines a `kwargs` @property
        descriptor.
        """
        validate(
            subclass=subclass,
            name="kwargs",
            expected_type=property
        )

    cdef int validate_slugify(self, type subclass) except -1:
        """Ensure that a subclass of AtomicType has a `slugify()`
        classmethod and that its signature matches __init__.
        """
        validate(
            subclass=subclass,
            name="slugify",
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

    ############################
    ####    RECORD STATE    ####
    ############################

    def remember(self, val) -> CacheValue:
        return CacheValue(value=val, hash=self.hash)

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
        self.validate_slugify(new_type)
        self.validate_aliases(new_type)
        self.validate_kwargs(new_type)

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

        The returned dictionary maps aliases of any kind to simple struct
        (`AliasInfo`) objects with two readonly fields: `AliasInfo.base` and
        `AliasInfo.defaults`.  These contain the corresponding AtomicType
        subclass and a default **kwargs dict to that class's `.instance()`
        method when the alias is encountered.
        """
        # check if cache is out of date
        if self.needs_updating(self._aliases):
            result = {
                k: AliasInfo(
                    base=c,
                    defaults=v
                )
                for c in self.atomic_types
                for k, v in c.aliases.items()
            }
            self._aliases = self.remember(result)

        # return cached value
        return self._aliases.value

    @property
    def regex(self) -> re.Pattern:
        """Compile a regular expression to match any registered AtomicType
        name or alias, as well as any arguments that may be passed to its
        `resolve()` constructor.
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
                    rf"(?P<nested>\[(?P<args>([^\[\]]|(?&nested))*)\])?"
                )

            # remember result
            self._regex = self.remember(result)

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
            self._resolvable = self.remember(result)

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


cdef class AtomicType(BaseType):
    """Base type for all user-defined atomic types.

    This is a metaclass.  Any time another class inherits from it, that class
    must conform to the standard AtomicType interface unless `add_to_registry`
    is explicitly set to `False`, like so:

        ```
        UnregisteredType(AtomicType, add_to_registry=False)
        ```

    If `add_to_registry=True` (the default behavior), then the inheriting class
    must define certain required fields, as follows:
        * `name`: a class property specifying a unique name to use when
            generating string representations of the given type.
        * `aliases`: a dictionary whose keys represent aliases that can be
            resolved by the `resolve_type()` factory function.  Each key must
            be unique, and must map to another dictionary containing keyword
            arguments to that type's `__init__()` method.
        * `slugify(cls, ...)`: a classmethod with the same argument signature
            as `__init__()`.  This must return a unique string representation
            for the given type, incorporating both its `name` and any arguments
            passed to it.  The uniqueness of this string must be emphasized,
            since it is directly hashed to locate flyweights of the given type.
        * `replace(self, ...)`: an instance method that returns a copy of the
            given type with the modifications specified in `...`.  For most
            types, this will have the same arguments as `__init__()`, but if
            a type modifies another type (e.g. `SparseType`/`CategoricalType`),
            then it may pass additional arguments to the modified type's
            `.replace()` method through the use of **kwargs.
    """

    registry: TypeRegistry = AtomicTypeRegistry()
    flyweights: dict[int, AtomicType] = {}

    def __init__(
        self,
        type_def: type,
        dtype: object,
        na_value: Any,
        itemsize: int,
        slug: str
    ):
        self.type_def = type_def
        self.dtype = dtype
        self.na_value = na_value
        self.itemsize = itemsize
        self.slug = slug
        self.hash = hash(slug)
        self._is_frozen = True  # no new attributes after this point

    @classmethod
    def instance(cls, *args, **kwargs) -> AtomicType:
        """Base flyweight constructor."""
        # generate slug and compute hash
        cdef long long _hash = hash(cls.slugify(*args, **kwargs))

        # get previous flyweight, if one exists
        cdef AtomicType result = cls.flyweights.get(_hash, None)
        if result is None:  # create new flyweight
            result = cls(*args, **kwargs)
            cls.flyweights[_hash] = result

        # return flyweight
        return result

    @classmethod
    def resolve(cls, *args: str) -> AtomicType:
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
        for t in cls._subtype_defs:
            t._supertype_def = cls._supertype_def

        # if the class has a supertype, remove it from the supertype's
        # `.subtypes` field and substitute the class's subtypes instead.
        if cls._supertype_def:
            cls._supertype_def._subtype_defs -= {cls}
            cls._supertype_def._subtype_defs |= cls._subtype_defs

        # clear the class's supertype/subtypes
        cls._supertype_def = None
        cls._subtype_defs -= cls._subtype_defs

        # rebuild regex patterns
        cls.registry.flush()

    @classmethod
    def register_supertype(
        cls,
        supertype: type,
        overwrite: bool = False
    ) -> None:
        # check supertype is a subclass of AtomicType
        if not issubclass(supertype, AtomicType):
            raise TypeError(f"`supertype` must be a subclass of AtomicType")

        # break circular references
        if supertype is cls:
            raise TypeError("Type cannot be registered to itself")

        # check type is already registered
        if cls._supertype_def:
            if overwrite:
                cls._supertype_def._subtype_defs -= {cls}
                cls._supertype_def = supertype
                supertype._subtype_defs |= {cls}
            else:
                raise TypeError(
                    f"Types can only be registered to one supertype at a "
                    f"time (`{cls.__name__}` is currently registered to "
                    f"`{cls._supertype_def.__name__}`)"
                )

        # register supertype
        else:
            cls._supertype_def = supertype
            supertype._subtype_defs |= {cls}

        # flush registry to synchronize instances
        cls.registry.flush()

    @classmethod
    def register_subtype(
        cls,
        subtype: type,
        overwrite: bool = False
    ) -> None:
        # check subtype is a subclass of AtomicType
        if not issubclass(subtype, AtomicType):
            raise TypeError(f"`subtype` must be a subclass of AtomicType")

        # delegate to subtype.register_supertype()
        subtype.register_supertype(cls)

    @classmethod
    def register_alias(
        cls,
        alias: Any,
        defaults: dict,
        overwrite: bool = False
    ) -> None:
        if alias in cls.registry.aliases:
            other = cls.registry.aliases[alias].base
            if other is cls:
                return None
            if overwrite:
                del other.aliases[alias]
            else:
                raise ValueError(
                    f"alias {repr(alias)} is already registered to {other}"
                )
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
    def is_sparse(self) -> bool:
        return False

    @property
    def is_categorical(self) -> bool:
        return False

    @property
    def root(self) -> AtomicType:
        if self.supertype is None:
            return self
        return self.supertype.root

    @property
    def subtypes(self) -> frozenset:
        """Override this if you want to change how a subtype is called."""
        # update cache if instance out of date
        if self.registry.needs_updating(self._subtypes):
            subtype_defs = traverse_subtypes(type(self))
            result = self._generate_subtypes(subtype_defs)
            self._subtypes = self.registry.remember(result)

        # return cached value
        return self._subtypes.value

    @property
    def supertype(self) -> AtomicType:
        """Override this if you want to change how a supertype is called."""
        # update cache if instance out of date
        if self.registry.needs_updating(self._supertype):
            result = self._generate_supertype(self._supertype_def)
            self._supertype = self.registry.remember(result)

        # return cached value
        return self._supertype.value

    def _generate_subtypes(self, types: set) -> frozenset:
        # build result, skipping invalid kwargs
        result = set()
        for t in types:
            try:
                result.add(t.instance(**self.kwargs))
            except TypeError:
                continue

        # return as frozenset
        return frozenset(result)

    def _generate_supertype(self, type_def: type) -> AtomicType:
        # check for root type
        if type_def is None:
            return None

        # pass self.kwargs to supertype constructor
        return type_def.instance(**self.kwargs)

    def contains(self, other):
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(o in self.subtypes for o in other)
        return other in self.subtypes

    def is_subtype(self, other) -> bool:
        return self in resolve.resolve_type(other)

    def parse(self, input_str: str) -> Any:
        lower = input_str.lower()
        if lower in na_strings:
            return na_strings[lower]
        # TODO: check if self.type_def is None and raise a ValueError
        # This should never occur.
        return self.type_def(input_str)

    def replace(self, **kwargs) -> AtomicType:
        cdef dict merged = {**self.kwargs, **kwargs}
        return self.instance(**merged)

    def __contains__(self, other) -> bool:
        return self.contains(other)

    def __eq__(self, other) -> bool:
        other = resolve.resolve_type(other)
        return isinstance(other, AtomicType) and self.hash == other.hash

    def __setattr__(self, name: str, value: Any) -> None:
        if self._is_frozen:
            raise AttributeError("AtomicType objects are read-only")
        else:
            self.__dict__[name] = value

    def __hash__(self) -> int:
        return self.hash

    def __repr__(self) -> str:
        sig = ", ".join(f"{k}={repr(v)}" for k, v in self.kwargs.items())
        return f"{type(self).__name__}({sig})"

    def __str__(self) -> str:
        return self.slug

    @classmethod
    def __init_subclass__(
        cls,
        add_to_registry: bool = True,
        cache_size: int = None,
        **kwargs
    ):
        # allow cooperative inheritance
        super(AtomicType, cls).__init_subclass__(**kwargs)

        # initialize required fields
        cls._subtype_defs = frozenset()
        cls._supertype_def = None
        if not issubclass(cls, AdapterType):
            cls.is_sparse = False
            cls.is_categorical = False
        if cache_size is not None:
            cls.flyweights = LRUDict(maxsize=cache_size)

        # validate subclass properties and add to registry, if directed
        if add_to_registry:
            cls.registry.add(cls)
            cls.aliases[cls] = {}  # cls always aliases itself


cdef class AdapterType(AtomicType):
    """Special case for AtomicTypes that modify other AtomicTypes."""

    def __init__(
        self,
        atomic_type: AtomicType,
        *args,
        **kwargs
    ):
        self.atomic_type = atomic_type
        super(AdapterType, self).__init__(*args, **kwargs)

    @classmethod
    def register_supertype(
        cls,
        supertype: type,
        overwrite: bool = False
    ) -> None:
        raise TypeError(f"AdapterTypes cannot have supertypes")

    @classmethod
    def register_subtype(
        cls,
        subtype: type,
        overwrite: bool = False
    ) -> None:
        raise TypeError(f"AdapterTypes cannot have subtypes")

    @property
    def root(self) -> AtomicType:
        return self.replace(atomic_type=self.atomic_type.root)

    def _generate_subtypes(self, types: set) -> frozenset:
        return frozenset(
            self.replace(atomic_type=t)
            for t in self.atomic_type.subtypes
        )

    def _generate_supertype(self, type_def: type) -> AtomicType:
        result = self.atomic_type.supertype
        if result is None:
            return None
        return self.replace(atomic_type=result)

    def replace(self, **kwargs) -> AtomicType:
        # extract kwargs pertaining to AdapterType
        adapter_kwargs = {k: v for k, v in kwargs.items() if k in self.kwargs}
        kwargs = {k: v for k, v in kwargs.items() if k not in self.kwargs}

        # merge adapter_kwargs with self.kwargs and get atomic_type
        adapter_kwargs = {**self.kwargs, **adapter_kwargs}
        atomic_type = adapter_kwargs["atomic_type"]
        del adapter_kwargs["atomic_type"]

        # pass non-sparse kwargs to atomic_type.replace()
        atomic_type = atomic_type.replace(**kwargs)

        # construct new AdapterType
        return self.instance(atomic_type=atomic_type, **adapter_kwargs)

    def __dir__(self) -> list:
        result = dir(type(self))
        result += list(self.__dict__.keys())
        result += [x for x in dir(self.atomic_type) if x not in result]
        return result

    def __getattr__(self, name: str) -> Any:
        return getattr(self.atomic_type, name)


cdef class CompositeType(BaseType):

    # TODO: rename element_types -> atomic_types
    # add index: np.ndarray = None argument

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

    def __iter__(self) -> Iterator[AtomicType]:
        return iter(self.element_types)

    def __len__(self) -> int:
        return len(self.element_types)

    def __repr__(self) -> str:
        slugs = ", ".join(str(x) for x in self.element_types)
        return f"{type(self).__name__}({{{slugs}}})"

    def __str__(self) -> str:
        slugs = ", ".join(str(x) for x in self.element_types)
        return f"{{{slugs}}}"
