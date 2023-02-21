import inspect
from functools import wraps
import regex as re  # using alternate regex
from types import MappingProxyType
from typing import Any, Callable

cimport numpy as np
import numpy as np
import pandas as pd

from pdtypes.type_hints import type_specifier

cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve

from pdtypes.util.round cimport Tolerance
from pdtypes.util.structs cimport LRUDict


# TODO: insert type_specifier hints where applicable


# conversions
# +------------------------------------------------
# |           | b | i | f | c | d | d | t | s | o |
# +-----------+------------------------------------
# | bool      | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | int       | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | float     | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | complex   | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | decimal   | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | datetime  | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | timedelta | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | string    | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | object    | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+


# AdapterTypes do not need to be flyweights, but they do need appropriate
# resolve() methods similar to AtomicType.  This means they need aliases, a
# name, and access to the shared registry.  They do NOT need .instance(),
# .detect() or .slugify() methods, since they will never be observed directly.
# Otherwise, __getattr__ automatically wraps any methods that may return
# AtomicType or CompositeType objects.


##########################
####    PRIMITIVES    ####
##########################


cdef class CacheValue:

    def __init__(self, object value, long long hash):
        self.value = value
        self.hash = hash


cdef class BaseType:
    """Base type for all type objects.  This has no interface of its own and
    merely serves to anchor inheritance.  Since Cython does not support true
    Union types, this is the simplest way of coupling them reliably in the
    Cython layer.
    """

    # TODO: put registry here?  would make it available from CompositeTypes

    pass



cdef class ScalarType(BaseType):
    """Base type for AtomicType and AdapterType objects."""

    @classmethod
    def clear_aliases(cls) -> None:
        """Remove every alias that is registered to this AdapterType."""
        cls.aliases.clear()
        cls.registry.flush()

    @classmethod
    def register_alias(cls, alias: Any, overwrite: bool = False) -> None:
        """Register a new alias for this AdapterType."""
        if alias in cls.registry.aliases:
            other = cls.registry.aliases[alias]
            if other is cls:
                return None
            if overwrite:
                del other.aliases[alias]
            else:
                raise ValueError(
                    f"alias {repr(alias)} is already registered to {other}"
                )
        cls.aliases.add(alias)
        cls.registry.flush()  # rebuild regex patterns

    @classmethod
    def remove_alias(cls, alias: Any) -> None:
        """Remove an alias from this AdapterType."""
        del cls.aliases[alias]
        cls.registry.flush()  # rebuild regex patterns


##########################
####    DECORATORS    ####
##########################


def dispatch(_method=None, *, namespace: str = None):
    """Dispatch an AtomicType method to pandas series' of the given type, so
    that it is discovered during attribute lookup.
    """
    has_options = _method is not None
    if has_options:
        validate_dispatch_signature(_method)

    def dispatch_decorator(method):
        if not has_options:
            validate_dispatch_signature(method)

        @wraps(method)
        def dispatch_wrapper(self, *args, **kwargs):
            return method(self, *args, **kwargs)

        dispatch_wrapper._dispatch = True
        dispatch_wrapper._namespace = namespace
        return dispatch_wrapper

    if has_options:
        return dispatch_decorator(_method)
    return dispatch_decorator


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
    cdef dict orig = {
        k: getattr(_class, k) for k in (
            "_generate_subtypes", "instance", "resolve"
        )
    }

    def _generate_subtypes(self, types: set) -> frozenset:
        result = orig["_generate_subtypes"](self, types)
        for k, v in self.backends.items():
            if k is not None and v in self.registry:
                result |= v.instance().subtypes.atomic_types
        return result

    @classmethod
    def instance(cls, backend: str = None, *args, **kwargs) -> AtomicType:
        if backend is None:
            return orig["instance"](*args, **kwargs)
        extension = cls.backends.get(backend, None)
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
        specific = cls.backends.get(backend, None)
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
            if backend in cls.backends:
                raise TypeError(
                    f"`backend` must be unique, not one of {set(cls.backends)}"
                )

            # ensure backend is self-consistent
            if specific.backend is None:
                specific.backend = backend
            elif backend != specific.backend:
                raise TypeError(
                    f"backends must match ({repr(backend)} != "
                    f"{repr(specific.backend)})"
                )

            # inherit generic attributes
            specific.is_generic = False
            specific.conversion_func = cls.conversion_func
            specific.name = cls.name
            specific._generic = cls
            cls.backends[backend] = specific
            specific.registry.flush()
            return specific

        return decorator

    # overwrite class attributes
    _class.is_generic = True
    _class.backend = None
    _class.backends = {None: _class}

    # patch in new methods
    loc = locals()
    for k in orig:
        setattr(_class, k, loc[k])
    _class.register_backend = register_backend
    return _class


def register(_class=None, *, ignore=False):
    """Validate an AtomicType definition and add it to the registry.

    Note: Any decorators above this one will be ignored during validation.
    """
    def register_decorator(_class_):
        if not issubclass(_class_, (AtomicType, AdapterType)):
            raise TypeError(f"`@register` can only be applied to AtomicTypes")
        if not ignore:
            AtomicType.registry.add(_class_)
        return _class_

    if _class is None:
        return register_decorator
    return register_decorator(_class)


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
        class_def.conversion_func = supertype.conversion_func

        # overwrite class attributes
        class_def.is_root = False
        class_def._parent = supertype
        supertype._children.add(class_def)
        AtomicType.registry.flush()
        return class_def

    return decorator


########################
####    REGISTRY    ####
########################


cdef class TypeRegistry:
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

    ##########################
    ####    PROPERTIES    ####
    ##########################

    @property
    def aliases(self) -> dict[Any, type]:
        """Return an up-to-date dictionary of all of the AtomicType aliases
        that are currently recognized by `resolve_type()`.

        The returned dictionary maps aliases (of any kind) to their AtomicType
        definitions.
        """
        # check if cache is out of date
        if self.needs_updating(self._aliases):
            result = {k: c for c in self.atomic_types for k in c.aliases}
            self._aliases = self.remember(result)

        # return cached value
        return self._aliases.value

    @property
    def dispatch_map(self) -> dict[str, dict[AtomicType, Callable]]:
        """Return an up-to-date dictionary of all methods that are currently
        being dispatched to Series objects based on their type.

        The structure of this dictionary reflects the calling signature of
        the methods it contains.  It goes as follows:

        {
            namespace1 (str): {
                method1_name (str): {
                    atomic_type1 (type): method1 (Callable),
                    atomic_type2 (type): method1 (Callable),
                    ...
                },
                method2_name (str): {
                    atomic_type1 (type): method2 (Callable),
                    atomic_type2 (type): method2 (Callable),
                    ...
                },
                ...
            },
            ...
            None: {
                method3_name (str): {
                    atomic_type1 (type): method3 (Callable),
                    atomic_type2 (type): method3 (Callable),
                    ...
                },
                ...
            }
        }
        """
        # check if cache is out of date
        if self.needs_updating(self._dispatch_map):
            # building a dispatch map consists of 4 steps:
            # 1) For each type held in registry, check for @dispatch methods.
            # 2) For each @dispatch method, setdefault(namespace, {}).
            # 3) namespace.setdefault(method_name, {}).
            # 4) method_name |= {atomic_type: method_def}.
            result = {}
            for atomic_type in self.atomic_types:
                for method_name in dir(atomic_type):
                    method_def = getattr(atomic_type, method_name)
                    if hasattr(method_def, "_dispatch"):
                        namespace = method_def._namespace
                        submap = result.setdefault(namespace, {})
                        submap = submap.setdefault(method_name, {})
                        submap[atomic_type] = method_def

            self._dispatch_map = self.remember(result)

        # return cached value
        return self._dispatch_map.value

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

    #######################
    ####    METHODS    ####
    #######################

    def add(self, new_type: type) -> None:
        """Add an AtomicType/AdapterType subclass to the registry."""
        # validate subclass has required fields
        self.validate_aliases(new_type)
        self.validate_slugify(new_type)
        if issubclass(new_type, AtomicType):
            self.validate_name(new_type)
            self.validate_type_def(new_type)
            self.validate_dtype(new_type)
            self.validate_itemsize(new_type)
            self.validate_na_value(new_type)

        # add type to registry and update hash
        self.atomic_types.append(new_type)
        self.update_hash()

    def clear(self):
        """Clear the AtomicType registry of all AtomicType subclasses."""
        self.atomic_types.clear()
        self.update_hash()

    def flush(self):
        """Reset the registry's internal state, forcing every property to be
        recomputed.
        """
        self.hash += 1  # this is overflow-safe

    def needs_updating(self, prop) -> bool:
        """Check if a `remember()`-ed registry property is out of date."""
        return prop is None or prop.hash != self.hash

    def remember(self, val) -> CacheValue:
        return CacheValue(value=val, hash=self.hash)

    def remove(self, old_type: type) -> None:
        """Remove an AtomicType subclass from the registry."""
        self.atomic_types.remove(old_type)
        self.update_hash()

    #######################
    ####    PRIVATE    ####
    #######################

    cdef void update_hash(self):
        """Hash the registry's internal state, for use in cached properties."""
        self.hash = hash(tuple(self.atomic_types))

    cdef int validate_aliases(self, type subclass) except -1:
        """Ensure that a subclass of AtomicType has an `aliases` dictionary
        and that none of its aliases overlap with another registered
        AtomicType.
        """
        validate(subclass, "aliases", expected_type=set)

        # ensure that no aliases are already registered to another AtomicType
        for k in subclass.aliases:
            if k in self.aliases:
                raise TypeError(
                    f"{subclass.__name__} alias {repr(k)} is already "
                    f"registered to {self.aliases[k].__name__}"
                )

    cdef int validate_dtype(self, type subclass) except -1:
        """Ensure that if a subclass of AtomicType defines a `dtype`
        attribute, that it is a valid numpy dtype or pandas extension type.
        """
        valid_dtypes = (np.dtype, pd.api.extensions.ExtensionDtype)
        if subclass.dtype is not None:
            validate(subclass, "dtype", expected_type=valid_dtypes)

    cdef int validate_itemsize(self, type subclass) except -1:
        """Ensure that if a subclass of AtomicType defines an `itemsize`
        attribute, that it is a positive integer.
        """
        if subclass.itemsize is not None:
            validate(subclass, "itemsize", expected_type=int)
            if subclass.itemsize < 1:
                raise TypeError(f"`{subclass.__name__}.itemsize` must be >= 1")

    cdef int validate_slugify(self, type subclass) except -1:
        """Ensure that a subclass of AtomicType has a `slugify()`
        classmethod and that its signature matches __init__.
        """
        validate(
            subclass,
            "slugify",
            expected_type="classmethod",
            signature=subclass
        )

    cdef int validate_name(self, type subclass) except -1:
        """Ensure that a subclass of AtomicType has a unique `name` attribute
        associated with it.
        """
        validate(subclass, "name", expected_type=str)

        # ensure subclass.name is unique or inherited from generic type
        if (issubclass(subclass, AtomicType) and (
            subclass.is_generic != False or
            subclass.name != subclass._generic.name
        )):
            observed_names = {x.name for x in self.atomic_types}
            if subclass.name in observed_names:
                raise TypeError(
                    f"{subclass.__name__}.name ({repr(subclass.name)}) must be "
                    f"unique (not one of {observed_names})"
                )

    cdef int validate_na_value(self, type subclass) except -1:
        """Ensure that if a subclass of AtomicType defines an `na_value`
        attribute, that it is accepted by `pd.isna()`.
        """
        if not pd.isna(subclass.na_value):
            raise TypeError(
                f"`{subclass.__name__}.na_value` must pass pd.isna()"
            )

    cdef int validate_type_def(self, type subclass) except -1:
        """Ensure that if a subclass of AtomicType defines a `type_def`
        attribute, that it is a valid type definition.
        """
        if subclass.type_def is not None:
            validate(subclass, "type_def", expected_type=type)

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


######################
####    ATOMIC    ####
######################


cdef class AtomicType(ScalarType):
    """Base type for all user-defined atomic types.

    Notes
    ----
    This is a metaclass.  Any time another class inherits from it, that class
    must conform to the standard AtomicType interface unless `ignore` is
    explicitly set to `True`, like so:

        ```
        UnregisteredType(AtomicType, ignore=True)
        ```

    If `ignore=False` (the default behavior), then the inheriting class
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

    # Internal fields.  These should never be overridden.
    registry: TypeRegistry = TypeRegistry()
    flyweights: dict[str, AtomicType] = {}

    # Default fields.  These can be overridden in AtomicType definitions to
    # customize behavior.
    conversion_func = cast.to_object
    type_def = None
    dtype = np.dtype("O")
    itemsize = None
    na_value = pd.NA
    is_nullable = True  # must be explicitly set False where applicable

    def __init__(self, **kwargs):
        self.kwargs = MappingProxyType(kwargs)
        self.slug = self.slugify(**kwargs)
        self.hash = hash(self.slug)
        self.adapters = ()
        self._is_frozen = True  # no new attributes beyond this point

    #############################
    ####    CLASS METHODS    ####
    #############################

    @classmethod
    def detect(cls, example: Any) -> AtomicType:
        """Given a scalar example of the given AtomicType, construct a new
        instance with the corresponding representation.

        Override this if your AtomicType has attributes that depend on the
        value of a corresponding scalar (e.g. datetime64 units, timezones,
        etc.)
        """
        return cls.instance()  # NOTE: most types disregard example data

    @classmethod
    def instance(cls, *args, **kwargs) -> AtomicType:
        """Base flyweight constructor.

        This factory method is the preferred constructor for AtomicType
        objects.  It inherits the same signature as a subclass's `__init__()`
        method, and consults its `.flyweights` table to ensure the uniqueness
        of the result.

        This should never be overriden.
        """
        # generate slug
        cdef str slug = cls.slugify(*args, **kwargs)

        # get previous flyweight if one exists
        cdef AtomicType result = cls.flyweights.get(slug, None)
        if result is None:  # create new flyweight
            result = cls(*args, **kwargs)
            cls.flyweights[slug] = result

        # return flyweight
        return result

    @classmethod
    def resolve(cls, *args: str) -> AtomicType:
        """An alternate constructor used to parse input in the type
        specification mini-language.
        
        Override this if your AtomicType implements custom parsing rules for
        any arguments that are supplied to this type.

        .. Note: The inputs to each argument will always be strings.
        """
        return cls.instance(*args)

    @classmethod
    def slugify(cls) -> str:
        if cls.is_generic == False:
            return f"{cls.name}[{cls.backend}]"
        return cls.name

    ##########################
    ####    PROPERTIES    ####
    ##########################

    @property
    def generic(self) -> AtomicType:
        """Return the generic equivalent for this AtomicType."""
        if self.registry.needs_updating(self._generic_cache):
            if self.is_generic:
                result = self
            elif self._generic is None:
                result = None
            else:
                result = self._generic.instance()
            self._generic_cache = self.registry.remember(result)
        return self._generic_cache.value

    @property
    def root(self) -> AtomicType:
        """Return the root node of this AtomicType's subtype hierarchy."""
        if self.is_root:
            return self
        return self.supertype.root

    @property
    def larger(self) -> list:
        """return a list of candidate AtomicTypes that this type can be
        upcasted to in the event of overflow.

        Override this to change the behavior of a bounded type (with
        appropriate `.min`/`.max` fields) when an OverflowError is detected.
        Note that candidate types will always be tested in order.
        """
        return []  # NOTE: empty list skips upcasting entirely

    @property
    def subtypes(self) -> CompositeType:
        """Return a CompositeType containing instances for every subtype
        currently registered to this AtomicType.

        The result is cached between `AtomicType.registry` updates, and can be
        customized via the `_generate_subtypes()` helper method.
        """
        if self.registry.needs_updating(self._subtype_cache):
            subtype_defs = traverse_subtypes(type(self))
            result = self._generate_subtypes(subtype_defs)
            self._subtype_cache = self.registry.remember(result)

        return CompositeType(self._subtype_cache.value)

    @property
    def supertype(self) -> AtomicType:
        """Return an AtomicType instance representing the supertype to which
        this AtomicType is registered, if one exists.

        The result is cached between `AtomicType.registry` updates, and can be
        customized via the `_generate_supertype()` helper method.
        """
        if self.registry.needs_updating(self._supertype_cache):
            result = self._generate_supertype(self._parent)
            self._supertype_cache = self.registry.remember(result)

        return self._supertype_cache.value

    ############################
    ####    TYPE METHODS    ####
    ############################

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
        if type_def is None or type_def not in self.registry:
            return None
        return type_def.instance()

    def contains(self, other: type_specifier) -> bool:
        """Test whether `other` is a subtype of the given AtomicType.
        This is functionally equivalent to `other in self`, except that it
        applies automatic type resolution to `other`.

        Override this to change the behavior of the `in` keyword and implement
        custom logic for membership tests of the given type.
        """
        other = resolve.resolve_type(other)

        # respect wildcard rules in subtypes
        subtypes = self.subtypes.atomic_types - {self}
        if isinstance(other, CompositeType):
            return all(
                o == self or any(o in a for a in subtypes) for o in other
            )
        return other == self or any(other in a for a in subtypes)

    def is_subtype(self, other: type_specifier) -> bool:
        """Reverse of `AtomicType.contains()`.

        This is functionally equivalent to `self in other`, except that it
        applies automatic type resolution + `.contains()` logic to `other`.
        """
        return self in resolve.resolve_type(other)

    def replace(self, **kwargs) -> AtomicType:
        """Return a modified copy of the given AtomicType with the values
        specified in `**kwargs`.
        """
        cdef dict merged = {**self.kwargs, **kwargs}
        return self.instance(**merged)

    def unwrap(self) -> AtomicType:
        """Strip any adapters that have been attached to this AtomicType."""
        return self

    def upcast(self, series: cast.SeriesWrapper) -> AtomicType:
        """Attempt to upcast an AtomicType to fit the observed range of a
        series.
        """
        # NOTE: this takes advantage of SeriesWrapper's min/max caching.
        min_val = series.min()
        max_val = series.max()
        if min_val < self.min or max_val > self.max:
            # recursively search for a larger alternative
            for t in self.larger:
                try:
                    return t.upcast(series)
                except OverflowError:
                    pass

            # no matching type could be found
            raise OverflowError(
                f"could not upcast {self} to fit observed range ({min_val}, "
                f"{max_val})"
            )

        # series fits type
        return self

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    def to_boolean(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert generic data to a boolean data type.

        Note: this method does not do any cleaning/pre-processing of the
        incoming data.  If a type definition requires this, then it should be
        implemented in its own `to_boolean()` equivalent before delegating
        to this method in the return statement.  Any changes made to this
        method will be propagated to the top-level `to_boolean()` and `cast()`
        functions when they are called on objects of the given type.
        """
        if series.hasnans:
            dtype = dtype.force_nullable()
        return series.astype(dtype, errors=errors)

    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert generic data to an integer data type.

        Note: this method does not do any cleaning/pre-processing of the
        incoming data.  If a type definition requires this, then it should be
        implemented in its own `to_integer()` equivalent before delegating
        to this method in the return statement.  Any changes made to this
        method will be propagated to the top-level `to_integer()` and `cast()`
        functions when they are called on objects of the given type.
        """
        if series.hasnans:
            dtype = dtype.force_nullable()

        series = series.astype(dtype, errors=errors)
        if downcast is not None:
            return dtype.downcast(series, smallest=downcast)
        return series

    def to_float(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert boolean data to a floating point data type."""
        series = series.astype(dtype, errors=errors)
        if downcast is not None:
            return dtype.downcast(series, smallest=downcast, tol=tol)
        return series

    def to_complex(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert boolean data to a complex data type."""
        series = series.astype(dtype, errors=errors)
        if downcast is not None:
            return dtype.downcast(series, smallest=downcast, tol=tol)
        return series

    def to_decimal(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert boolean data to a decimal data type."""
        return series.astype(dtype, errors=errors)

    def to_string(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        format: str,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert arbitrary data to a string data type.

        Override this to change the behavior of the generic `to_string()` and
        `cast()` functions on objects of the given type.
        """
        if format:
            series = series.apply_with_errors(
                lambda x: f"{x:{format}}",
                errors=errors
            )
            series.element_type = str
        return series.astype(dtype, errors=errors)

    def to_object(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        call: Callable,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert arbitrary data to an object data type."""
        direct = call is None
        if direct:
            call = dtype.type_def

        def wrapped_call(object val):
            cdef object result = call(val)
            if direct:
                return result

            cdef type output_type = type(result)
            if output_type != dtype.type_def:
                raise ValueError(
                    f"`call` must return an object of type {dtype.type_def}"
                )
            return result

        series = series.apply_with_errors(call=dtype.type_def, errors=errors)
        series.element_type = dtype
        return series

    #############################
    ####    MAGIC METHODS    ####
    #############################

    def __contains__(self, other: type_specifier) -> bool:
        return self.contains(other)

    def __eq__(self, other: type_specifier) -> bool:
        other = resolve.resolve_type(other)
        return isinstance(other, AtomicType) and self.hash == other.hash

    def __getattr__(self, name: str) -> Any:
        try:
            return self.kwargs[name]
        except KeyError as err:
            err_msg = (
                f"{repr(type(self).__name__)} object has no attribute: "
                f"{repr(name)}"
            )
            raise AttributeError(err_msg) from err

    @classmethod
    def __init_subclass__(cls, cache_size: int = None, **kwargs):
        valid = AtomicType.__subclasses__()
        if cls not in valid:
            raise TypeError(
                f"{cls.__name__} cannot inherit from another AtomicType "
                f"definition"
            )

        # required fields
        cls.aliases.add(cls)  # cls always aliases itself
        if cache_size is not None:
            cls.flyweights = LRUDict(maxsize=cache_size)

        # required fields for @generic
        cls._generic = None
        cls.backend = None
        cls.backends = {}
        cls.is_generic = None  # True if @generic, False if @register_backend

        # required fields for @subtype
        cls._children = set()
        cls._parent = None
        cls.is_root = True

        # allow cooperative inheritance
        super(AtomicType, cls).__init_subclass__(**kwargs)


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


#######################
####    ADAPTER    ####
#######################


cdef class AdapterType(ScalarType):
    """Special case for AtomicTypes that modify other AtomicTypes."""

    def __init__(self, atomic_type: AtomicType, **kwargs):
        self.atomic_type = atomic_type
        self.kwargs = MappingProxyType({"atomic_type": atomic_type} | kwargs)
        self.slug = self.slugify(atomic_type, **kwargs)
        self.hash = hash(self.slug)
        self.adapters = (self.adapter_name,) + atomic_type.adapters
        self._is_frozen = True  # no new attributes beyond this point

    #############################
    ####    CLASS METHODS    ####
    #############################

    @classmethod
    def resolve(cls, atomic_type: str, *args: str) -> AdapterType:
        """An alternate constructor used to parse input in the type
        specification mini-language.

        Override this if your AdapterType implements custom parsing rules for
        any arguments that are supplied to this type.

        .. Note: The inputs to each argument will always be strings.
        """
        instance = resolve.resolve_type(atomic_type)
        if isinstance(instance, CompositeType):
            raise TypeError(f"wrapped type must be atomic, not {instance}")
        return cls(instance, *args)

    @classmethod
    def slugify(cls, atomic_type: AtomicType) -> str:
        return f"{cls.adapter_name}[{str(atomic_type)}]"

    #######################
    ####    METHODS    ####
    #######################

    def contains(self, other: type_specifier) -> bool:
        """Test whether `other` is a subtype of the given AtomicType.
        This is functionally equivalent to `other in self`, except that it
        applies automatic type resolution to `other`.

        For AdapterTypes, this merely delegates to AtomicType.contains().
        """
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            raise NotImplementedError()
        return (
            self.unwrap().contains(other.unwrap()) and
            self.adapters == other.adapters
        )

    def replace(self, **kwargs) -> AdapterType:
        # extract kwargs pertaining to AdapterType
        adapter_kwargs = {}
        atomic_kwargs = {}
        for k, v in kwargs.items():
            if k in self.kwargs:
                adapter_kwargs[k] = v
            else:
                atomic_kwargs[k] = v

        # merge adapter_kwargs with self.kwargs and get atomic_type
        adapter_kwargs = {**self.kwargs, **adapter_kwargs}
        atomic_type = adapter_kwargs.pop("atomic_type")

        # pass non-sparse kwargs to atomic_type.replace()
        atomic_type = atomic_type.replace(**atomic_kwargs)

        # construct new AdapterType
        return type(self)(atomic_type=atomic_type, **adapter_kwargs)

    def unwrap(self) -> AtomicType:
        """Strip any AdapterTypes that have been attached to this AtomicType.
        """
        result = self.atomic_type
        while isinstance(result, AdapterType):
            result = result.atomic_type
        return result

    #############################
    ####    MAGIC METHODS    ####
    #############################

    def __contains__(self, other: type_specifier) -> bool:
        return self.contains(other)

    def __dir__(self) -> list:
        result = dir(type(self))
        result += list(self.__dict__.keys())
        result += [x for x in dir(self.atomic_type) if x not in result]
        return result

    def __eq__(self, other: type_specifier) -> bool:
        other = resolve.resolve_type(other)
        return isinstance(other, AdapterType) and self.hash == other.hash

    def __getattr__(self, name: str) -> Any:
        try:
            return self.kwargs[name]
        except KeyError as err:
            val = getattr(self.atomic_type, name)

        # decorate callables to return AdapterTypes
        if callable(val):
            def sticky_wrapper(*args, **kwargs):
                result = val(*args, **kwargs)
                if isinstance(result, AtomicType):
                    result = self.replace(atomic_type=result)
                elif isinstance(result, CompositeType):
                    result = CompositeType(
                        {self.replace(atomic_type=t) for t in result}
                    )
                return result

            return sticky_wrapper

        # wrap properties as AdapterTypes
        if isinstance(val, AtomicType):
            val = self.replace(atomic_type=val)
        elif isinstance(val, CompositeType):
            val = CompositeType({self.replace(atomic_type=t) for t in val})

        return val

    def __hash__(self) -> int:
        return self.hash

    @classmethod
    def __init_subclass__(cls, cache_size: int = None, **kwargs):
        valid = AdapterType.__subclasses__()
        if cls not in valid:
            raise TypeError(
                f"{cls.__name__} cannot inherit from another AdapterType "
                f"definition"
            )

    def __repr__(self) -> str:
        sig = ", ".join(f"{k}={repr(v)}" for k, v in self.kwargs.items())
        return f"{type(self).__name__}({sig})"

    def __setattr__(self, name: str, value: Any) -> None:
        if self._is_frozen:
            raise AttributeError("AdapterType objects are read-only")
        else:
            self.__dict__[name] = value

    def __str__(self) -> str:
        return self.slug

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
        np.ndarray[object] index = None
    ):
        # parse argument
        if atomic_types is None:  # empty
            self.atomic_types = set()
        elif isinstance(atomic_types, (AtomicType, AdapterType)):  # wrap
            self.atomic_types = {atomic_types}
        elif isinstance(atomic_types, CompositeType):  # copy
            self.atomic_types = atomic_types.atomic_types.copy()
            if index is None:
                index = atomic_types.index
        elif (
            hasattr(atomic_types, "__iter__") and
            not isinstance(atomic_types, str)
        ):  # build
            self.atomic_types = set()
            for val in atomic_types:
                if isinstance(val, (AtomicType, AdapterType)):
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
        self.index = index
    
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
        self.index = None

    @property
    def subtypes(self) -> CompositeType:
        """An alias for `CompositeType.expand()`"""
        return self.expand()

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


#######################
####    PRIVATE    ####
#######################


cdef void _traverse_subtypes(type atomic_type, set result):
    """Recursive helper for traverse_subtypes()"""
    result.add(atomic_type)
    for subtype in atomic_type._children:
        if subtype in atomic_type.registry:
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
        if (
            isinstance(signature, type) and
            signature.__init__ == AtomicType.__init__
        ):
            expected = MappingProxyType({})
        else:
            expected = inspect.signature(signature).parameters

        try:
            attr_sig = inspect.signature(attr).parameters
        except ValueError:  # cython methods aren't introspectable
            attr_sig = MappingProxyType({})

        if attr_sig != expected:
            raise TypeError(
                f"{subclass.__name__}.{name}() must have the following "
                f"signature: {dict(expected)}, not {attr_sig}"
            )


cdef int validate_dispatch_signature(object call) except -1:
    """Inspect the signature of an AtomicType method decorated with @dispatch,
    ensuring that it accepts and returns SeriesWrapper objects.
    """
    cdef object sig = inspect.signature(call)
    cdef object first_type = list(sig.parameters.values())[1].annotation
    cdef object return_type = sig.return_annotation
    cdef set valid_annotations = {"SeriesWrapper", cast.SeriesWrapper}

    # NOTE: methods defined in .pyx files will store their SeriesWrapper
    # annotations as strings, while those defined in .py files store them as
    # direct references.

    if first_type not in valid_annotations:
        raise TypeError(
            f"@dispatch method {call.__qualname__}() must accept a "
            f"SeriesWrapper as its first argument after self, not {first_type}"
        )
    if return_type not in valid_annotations:
        raise TypeError(
            f"@dispatch method {call.__qualname__}() must return a "
            f"SeriesWrapper object, not {return_type}"
        )
