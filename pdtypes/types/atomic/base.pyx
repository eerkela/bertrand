import inspect
from itertools import combinations
import regex as re  # using alternate regex
from typing import Any, Iterator

cimport cython
cimport numpy as np
import numpy as np
import pandas as pd

from pdtypes.util.structs cimport LRUDict

cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve


# TODO: move from_caller to object.pyx.


# TODO: these go in individual AtomicType definitions, under cls.flyweights
# cdef LRUDict datetime64_registry = LRUDict(maxsize=cache_size)
# cdef LRUDict decimal_registry = LRUDict(maxsize=cache_size)
# cdef LRUDict object_registry = LRUDict(maxsize=cache_size)
# cdef LRUDict timedelta64_registry = LRUDict(maxsize=cache_size)


#######################
####    PRIVATE    ####
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


##########################
####    PRIMITIVES    ####
##########################


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


###########################
####    ATOMIC TYPE    ####
###########################


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
                    f"registered to {self.aliases[k].base.__name__}"
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
    ####    ADD/REMOVE TYPES    ####
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
        self.hash += 1  # this is overflow-safe

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

    Notes
    ----
    This is a metaclass.  Any time another class inherits from it, that class
    must conform to the standard AtomicType interface unless `add_to_registry`
    is explicitly set to `False`, like so:

        ```
        UnregisteredType(AtomicType, add_to_registry=False)
        ```

    If `add_to_registry=True` (the default behavior), then the inheriting class
    must define certain required fields, as follows:
        * `name: str`: a class property specifying a unique name to use when
            generating string representations of the given type.
        * `aliases: dict`: a dictionary whose keys represent aliases that can be
            resolved by the `resolve_type()` factory function.  Each key must
            be unique, and must map to another dictionary containing keyword
            arguments to that type's `__init__()` method.
        * `slugify(cls, ...) -> str`: a classmethod with the same argument
            signature as `__init__()`.  This must return a unique string
            representation for the given type, incorporating both its `name`
            and any arguments passed to it.  The uniqueness of this string must
            be emphasized, since it is directly hashed to identify flyweights
            of the given type.
        * `kwargs: dict`: a runtime `@property` that returns the same **kwargs
            dict that was supplied to create the AtomicType instance it is
            called from.

    A subclass can also override the following methods to customize its
    behavior:
        * `_generate_subtypes(self, types: set) -> frozenset`:
        * `_generate_supertype(self, type_def: type) -> AtomicType`:
        * `contains(self, other) -> bool`:
        * `resolve(cls, *args) -> AtomicType`:
        * `parse(cls, *args) -> Any`:
        * `detect(cls, example: Any) -> AtomicType`:
        * `to_boolean(...)`:
        * `to_integer(...)`:
        * `to_float(...)`:
        * `to_complex(...)`:
        * `to_decimal(...)`:
        * `to_datetime(...)`:
        * `to_timedelta(...)`:
        * `to_string(...)`:
        * `to_object(...)`:
    """

    registry: TypeRegistry = AtomicTypeRegistry()
    flyweights: dict[int, AtomicType] = {}
    is_nullable = True

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

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    @classmethod
    def detect(cls, example, **defaults) -> AtomicType:
        """Given a scalar example of the given AtomicType, construct a new
        instance with the corresponding representation.

        Override this if your AtomicType has attributes that depend on the
        value of a corresponding scalar (e.g. datetime64 units, timezones,
        etc.)
        """
        # NOTE: most types disregard example data
        return cls.instance(**defaults)

    @classmethod
    def instance(cls, *args, **kwargs) -> AtomicType:
        """Base flyweight constructor.

        This factory method is the preferred constructor for AtomicType
        objects.  It inherits the same interface as a subclass's `__init__()`
        method, and consults its `.flyweights` table to ensure the uniqueness
        of the result.

        This should never be overriden.
        """
        # generate slug and compute hash
        cdef long long _hash = hash(cls.slugify(*args, **kwargs))

        # get previous flyweight, if one exists
        cdef AtomicType result = cls.flyweights.get(_hash, None)
        if result is None:  # create new flyweight
            result = cls(*args, **kwargs)
            cls.flyweights[_hash] = result

        # return flyweight
        return result

    def replace(self, **kwargs) -> AtomicType:
        """Return a modified copy of the given AtomicType with the values
        specified in `**kwargs`.
        """
        cdef dict merged = {**self.kwargs, **kwargs}
        return self.instance(**merged)

    @classmethod
    def resolve(cls, *args: str) -> AtomicType:
        """An alternate constructor used to parse input in the type
        specification mini-language.
        
        Override this if your AtomicType implements custom parsing rules for
        any arguments that are supplied to this type.

        .. Note: The inputs to each argument will always be strings.
        """
        return cls.instance(*args)

    #################################
    ####    SUBTYPE/SUPERTYPE    ####
    #################################

    def _generate_subtypes(self, types: set) -> frozenset:
        """Given a set of subtype definitions, map them to their corresponding
        instances.

        `types` is always a set containing all the AtomicType subclass
        definitions that have been registered to this AtomicType.  This
        method is responsible for transforming them into their respective
        instances, which are cached until the AtomicType registry is updated.

        Override this if your AtomicType implements custom logic to generate
        subtype instances (such as wildcard behavior or similar functionality).
        """
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
        """Given a (possibly null) supertype definition, map it to its
        corresponding instance.

        `type_def` is always either `None` or an AtomicType subclass definition
        representing the supertype that this AtomicType has been registered to.
        This method is responsible for transforming it into the corresponding
        instance, which is cached until the AtomicType registry is updated.

        Override this if your AtomicType implements custom logic to generate
        supertype instances (due to an interface mismatch or similar obstacle).
        """
        if type_def is None:
            return None
        return type_def.instance(**self.kwargs)

    def contains(self, other):
        """Test whether `other` is a subtype of the given AtomicType.

        This is functionally equivalent to `other in self`, except that it
        applies automatic type resolution to `other`.

        Override this to change the behavior of the `in` keyword and implement
        custom logic for membership tests of the given type.
        """
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(o in self.subtypes.atomic_types for o in other)
        return other in self.subtypes.atomic_types

    def is_subtype(self, other) -> bool:
        """Reverse of `AtomicType.contains()`.

        This is functionally equivalent to `self in other`, except that it
        applies automatic type resolution + `.contains()` logic to `other`.
        """
        return self in resolve.resolve_type(other)

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
    def register_subtype(
        cls,
        subtype: type,
        overwrite: bool = False
    ) -> None:
        """Add an AtomicType subclass to this class's subtypes set."""
        # check subtype is a subclass of AtomicType
        if not issubclass(subtype, AtomicType):
            raise TypeError(f"`subtype` must be a subclass of AtomicType")

        # delegate to subtype.register_supertype()
        subtype.register_supertype(cls)

    @classmethod
    def register_supertype(
        cls,
        supertype: type,
        overwrite: bool = False
    ) -> None:
        """Add this class to another AtomicType's subtypes set"""
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

    @property
    def root(self) -> AtomicType:
        """Return the root node of this AtomicType's subtype hierarchy."""
        if self.supertype is None:
            return self
        return self.supertype.root

    @property
    def subtypes(self) -> CompositeType:
        """Return a CompositeType containing instances for every subtype
        currently registered to this AtomicType.

        The result is cached between `AtomicType.registry` updates, and can be
        customized via the `_generate_subtypes()` helper method.
        """
        if self.registry.needs_updating(self._subtypes):
            subtype_defs = traverse_subtypes(type(self))
            result = self._generate_subtypes(subtype_defs)
            self._subtypes = self.registry.remember(result)

        return CompositeType(self._subtypes.value)

    @property
    def supertype(self) -> AtomicType:
        """Return an AtomicType instance representing the supertype to which
        this AtomicType is registered, if one exists.

        The result is cached between `AtomicType.registry` updates, and can be
        customized via the `_generate_supertype()` helper method.
        """
        if self.registry.needs_updating(self._supertype):
            result = self._generate_supertype(self._supertype_def)
            self._supertype = self.registry.remember(result)

        return self._supertype.value

    #####################
    ####    ALIAS    ####
    #####################

    @classmethod
    def clear_aliases(cls) -> None:
        """Remove every alias that is registered to this AtomicType."""
        cls.aliases.clear()
        cls.registry.flush()  # rebuild regex patterns

    @classmethod
    def register_alias(
        cls,
        alias: Any,
        defaults: dict,
        overwrite: bool = False
    ) -> None:
        """Register a new alias for this AtomicType."""
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
        """Remove an alias from this AtomicType."""
        del cls.aliases[alias]
        cls.registry.flush()  # rebuild regex patterns

    ####################
    ####    MISC    ####
    ####################

    def parse(self, input_str: str) -> Any:
        """Convert an input string into an object of the corresponding type.

        This is invoked to detect literal values from arguments given in the
        type specification mini-language.

        Override this if your AtomicType implements custom logic to parse
        string equivalents of the given type outside its normal constructor.
        """
        if input_str in resolve.na_strings:
            return resolve.na_strings[input_str]

        if self.type_def is None:
            raise ValueError(
                f"{repr(str(self))} types have no associated type_def"
            )

        return self.type_def(input_str)

    def unwrap(self) -> AtomicType:
        """Strip any AdapterTypes that have been attached to this AtomicType.
        """
        result = self
        while hasattr(result, "atomic_type"):
            result = result.atomic_type
        return result

    ###########################
    ####    CONVERSIONS    ####
    ###########################

    def to_boolean(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        **unused
    ) -> pd.Series:
        """Convert arbitrary data to a boolean data type.

        Override this to change the behavior of the generic `to_boolean()` and
        `cast()` functions on objects of the given type.
        """
        # python bool special case
        if dtype.backend == "python":
            result = np.frompyfunc(dtype.type_def, 1, 1)(series)
            if dtype.dtype != np.dtype("O"):
                return result.astype(dtype.dtype)
            return result

        # ensure dtype is nullable if missing values are detected
        if series.hasnans and isinstance(dtype.dtype, np.dtype):
            dtype = dtype.replace(backend="pandas")

        return series.astype(dtype.dtype, copy=False)

    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        downcast: bool = False,
        **unused
    ) -> pd.Series:
        """Convert arbitrary data to an integer data type.

        Override this to change the behavior of the generic `to_boolean()` and
        `cast()` functions on objects of the given type.
        """
        if downcast:
            dtype = dtype.downcast(series)

        # python int special case
        if dtype.backend == "python":
            return np.frompyfunc(dtype.type_def, 1, 1)(series)

        if series.hasnans and isinstance(dtype.dtype, np.dtype):
            dtype = dtype.replace(backend="pandas")

        # converting object series to pandas extension type is inconsistent
        if dtype.backend == "pandas":
            series.series = series.rectify()

        return series.astype(dtype.dtype, copy=False)

    def to_string(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        **unused
    ) -> pd.Series:
        """Convert arbitrary data to a string data type.

        Override this to change the behavior of the generic `to_string()` and
        `cast()` functions on objects of the given type.
        """
        return series.astype(dtype.dtype)

    #############################
    ####    MAGIC METHODS    ####
    #############################

    def __contains__(self, other) -> bool:
        return self.contains(other)

    def __eq__(self, other) -> bool:
        other = resolve.resolve_type(other)
        return isinstance(other, AtomicType) and self.hash == other.hash

    @classmethod
    def __init_subclass__(
        cls,
        add_to_registry: bool = True,
        cache_size: int = None,
        supertype: type = None,
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

        # add separate LRU cache, if directed
        if cache_size is not None:
            cls.flyweights = LRUDict(maxsize=cache_size)

        # register supertype, if one is given
        if supertype is not None:
            cls.register_supertype(supertype)

        # validate subclass properties and add to registry, if directed
        if add_to_registry:
            cls.registry.add(cls)
            cls.aliases[cls] = {}  # cls always aliases itself

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


##############################
####    COMPOSITE TYPE    ####
##############################


cdef class CompositeType(BaseType):
    """Set-like container for AtomicType objects.

    Implements the same interface as the built-in set type, but is restricted
    to contain only AtomicType objects.  Also extends subset/superset/membership
    checks to include subtypes for each of the contained AtomicTypes.
    """

    def __init__(
        self,
        atomic_types = None,
        AtomicType[:] index = None
    ):
        # parse argument
        if atomic_types is None:  # empty
            self.atomic_types = set()
        elif isinstance(atomic_types, AtomicType):  # wrap
            self.atomic_types = {atomic_types}
        elif isinstance(atomic_types, CompositeType):  # copy
            self.atomic_types = atomic_types.atomic_types.copy()
            if index is None:
                index = atomic_types._index
        elif (
            hasattr(atomic_types, "__iter__") and
            not isinstance(atomic_types, str)
        ):  # build
            self.atomic_types = set()
            for val in atomic_types:
                if isinstance(val, AtomicType):
                    self.atomic_types.add(val)
                elif isinstance(val, CompositeType):
                    self.atomic_types.update(x for x in val)
                else:
                    raise TypeError(
                        f"CompositeType objects can only contain AtomicTypes, "
                        f"not {type(val)}"
                    )
        else:
            raise TypeError(
                f"CompositeType objects can only contain AtomicTypes, "
                f"not {type(atomic_types)}"
            )

        # assign index
        self._index = index
    
    ###############################
    ####    UTILITY METHODS    ####
    ###############################

    def collapse(self) -> CompositeType:
        """Return a copy with redundant subtypes removed.  A subtype is
        redundant if it is fully encapsulated within the other members of the
        CompositeType.
        """
        cdef AtomicType atomic_type
        cdef AtomicType t

        # for every AtomicType a, check if there is another AtomicType t such
        # that a != t and a in t.  If this is true, then the type is redundant.
        return CompositeType(
            a for a in self
            if not any(a != t and a in t for t in self.atomic_types)
        )

    def expand(self) -> CompositeType:
        """Expand the contained AtomicTypes to include each of their subtypes.
        """
        cdef AtomicType atomic_type

        # simple union of subtypes
        return self.union(atomic_type.subtypes for atomic_type in self)

    cdef void forget_index(self):
        self._index = None

    @property
    def index(self) -> np.ndarray:
        if self._index is None:
            return None
        return self._index.base.base

    ####################################
    ####    STATIC WRAPPER (SET)    ####
    ####################################

    def add(self, typespec) -> None:
        """Add a type specifier to the CompositeType."""
        # resolve input
        resolved = resolve.resolve_type(typespec)

        # update self.atomic_types
        if isinstance(resolved, AtomicType):
            self.atomic_types.add(resolved)
        else:
            self.atomic_types.update(t for t in resolved)

        # throw out index
        self.forget_index()

    def clear(self) -> None:
        """Remove all AtomicTypes from the CompositeType."""
        self.atomic_types.clear()
        self.forget_index()

    def copy(self) -> CompositeType:
        """Return a shallow copy of the CompositeType."""
        return CompositeType(self)

    def difference(self, *others) -> CompositeType:
        """Return a new CompositeType with AtomicTypes that are not in any of
        the others.
        """
        cdef AtomicType x
        cdef AtomicType y
        cdef CompositeType other
        cdef CompositeType result = self.expand()

        # search expanded set and reject types that contain an item in other
        for item in others:
            other = CompositeType(resolve.resolve_type(item)).expand()
            result = CompositeType(
                x for x in result if not any(y in x for y in other)
            )

        # expand/reduce called only once
        return result.collapse()

    def difference_update(self, *others) -> None:
        """Update a CompositeType in-place, removing AtomicTypes that can be
        found in others.
        """
        self.atomic_types = self.difference(*others).atomic_types
        self.forget_index()

    def discard(self, typespec) -> None:
        """Remove the given type specifier from the CompositeType if it is
        present.
        """
        # resolve input
        resolved = resolve.resolve_type(typespec)

        # update self.atomic_types
        if isinstance(resolved, AtomicType):
            self.atomic_types.discard(resolved)
        else:
            for t in resolved:
                self.atomic_types.discard(t)

        # throw out index
        self.forget_index()

    def intersection(self, *others) -> CompositeType:
        """Return a new CompositeType with AtomicTypes in common to this
        CompositeType and all others.
        """
        cdef CompositeType other
        cdef CompositeType result = self.copy()

        # include any AtomicType in self iff it is contained in other.  Do the
        # same in reverse.
        for item in others:
            other = CompositeType(resolve.resolve_type(other))
            result = CompositeType(
                {a for a in result if a in other} |
                {t for t in other if t in result}
            )

        # return compiled result
        return result

    def intersection_update(self, *others) -> None:
        """Update a CompositeType in-place, keeping only the AtomicTypes found
        in it and all others.
        """
        self.atomic_types = self.intersection(*others).atomic_types
        self.forget_index()

    def isdisjoint(self, other) -> bool:
        """Return `True` if the CompositeType has no AtomicTypes in common
        with `other`.

        CompositeTypes are disjoint if and only if their intersection is the
        empty set.
        """
        return not self.intersection(other)

    def issubset(self, other) -> bool:
        """Test whether every AtomicType in the CompositeType is also in
        `other`.
        """
        return self in resolve.resolve_type(other)

    def issuperset(self, other) -> bool:
        """Test whether every AtomicType in `other` is contained within the
        CompositeType.
        """
        return resolve.resolve_type(other) in self

    def pop(self) -> AtomicType:
        """Remove and return an arbitrary AtomicType from the CompositeType.
        Raises a KeyError if the CompositeType is empty.
        """
        self.forget_index()
        return self.atomic_types.pop()

    def remove(self, typespec) -> None:
        """Remove the given type specifier from the CompositeType.  Raises a
        KeyError if `typespec` is not contained in the set.
        """
        # resolve input
        resolved = resolve.resolve_type(typespec)

        # update self.atomic_types
        if isinstance(resolved, AtomicType):
            self.atomic_types.remove(resolved)
        else:
            for t in resolved:
                self.atomic_types.remove(t)

        # throw out index
        self.forget_index()

    def symmetric_difference(self, other) -> CompositeType:
        """Return a new CompositeType with AtomicTypes that are in either the
        original CompositeType or `other`, but not both.
        """
        resolved = CompositeType(resolve.resolve_type(other))
        return (self.difference(resolved)) | (resolved.difference(self))

    def symmetric_difference_update(self, other) -> None:
        """Update a CompositeType in-place, keeping only AtomicTypes that
        are found in either `self` or `other`, but not both.
        """
        self.atomic_types = self.symmetric_difference(other).atomic_types
        self.forget_index()

    def union(self, *others) -> CompositeType:
        """Return a new CompositeType with all the AtomicTypes from this
        CompositeType and all others.
        """
        return CompositeType(
            self.atomic_types.union(*[
                CompositeType(resolve.resolve_type(o)).atomic_types
                for o in others
            ])
        )

    def update(self, *others) -> None:
        """Update the CompositeType in-place, adding AtomicTypes from all
        others.
        """
        self.atomic_types = self.union(*others).atomic_types
        self.forget_index()

    #############################
    ####    MAGIC METHODS    ####
    #############################

    def __and__(self, other) -> CompositeType:
        """Return a new CompositeType containing the AtomicTypes common to
        `self` and all others.
        """
        return self.intersection(other)

    def __contains__(self, other) -> bool:
        """Test whether a given type specifier is a member of `self` or any of
        its subtypes.
        """
        resolved = resolve.resolve_type(other)
        if isinstance(resolved, AtomicType):
            return any(other in t for t in self)
        return all(any(o in t for t in self) for o in resolved)

    def __eq__(self, other) -> bool:
        """Test whether `self` and `other` contain identical AtomicTypes."""
        resolved = CompositeType(resolve.resolve_type(other))
        return self.atomic_types == resolved.atomic_types

    def __ge__(self, other) -> bool:
        """Test whether every element in `other` is contained within `self`.
        """
        return self.issuperset(other)

    def __gt__(self, other) -> bool:
        """Test whether `self` is a proper superset of `other`
        (``self >= other and self != other``).
        """
        return self != other and self >= other

    def __iand__(self, other) -> CompositeType:
        """Update a CompositeType in-place, keeping only the AtomicTypes found
        in it and all others.
        """
        self.intersection_update(other)
        return self

    def __ior__(self, other) -> CompositeType:
        """Update a CompositeType in-place, adding AtomicTypes from all
        others.
        """
        self.update(other)
        return self

    def __isub__(self, other) -> CompositeType:
        """Update a CompositeType in-place, removing AtomicTypes that can be
        found in others.
        """
        self.difference_update(other)
        return self

    def __iter__(self):
        """Iterate through the AtomicTypes contained within a CompositeType.
        """
        return iter(self.atomic_types)

    def __ixor__(self, other) -> CompositeType:
        """Update a CompositeType in-place, keeping only AtomicTypes that
        are found in either `self` or `other`, but not both.
        """
        self.symmetric_difference_update(other)
        return self

    def __le__(self, other) -> bool:
        """Test whether every element in `self` is contained within `other`."""
        return self.issubset(other)

    def __lt__(self, other) -> bool:
        """Test whether `self` is a proper subset of `other`
        (``self <= other and self != other``).
        """
        return self != other and self <= other

    def __len__(self):
        """Return the number of AtomicTypes in the CompositeType."""
        return len(self.atomic_types)

    def __or__(self, other) -> CompositeType:
        """Return a new CompositeType containing the AtomicTypes of `self`
        and all others.
        """
        return self.union(other)

    def __repr__(self) -> str:
        slugs = ", ".join(str(x) for x in self.atomic_types)
        return f"{type(self).__name__}({{{slugs}}})"

    def __str__(self) -> str:
        slugs = ", ".join(str(x) for x in self.atomic_types)
        return f"{{{slugs}}}"

    def __sub__(self, other) -> CompositeType:
        """Return a new CompositeType with AtomicTypes that are not in the
        others.
        """
        return self.difference(other)

    def __xor__(self, other) -> CompositeType:
        """Return a new CompositeType with AtomicTypes that are in either
        `self` or `other` but not both.
        """
        return self.symmetric_difference(other)