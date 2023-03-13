from types import MappingProxyType
from typing import Any, Callable, Iterator

cimport numpy as np
import numpy as np
import pandas as pd
from pandas.api.extensions import register_extension_dtype

cimport pdcast.convert as convert
import pdcast.convert as convert
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve

cimport pdcast.types.base.registry as registry
import pdcast.types.base.registry as registry
cimport pdcast.types.base.adapter as adapter
cimport pdcast.types.base.composite as composite

import pdcast.util.array as array
from pdcast.util.round cimport Tolerance
from pdcast.util.structs cimport LRUDict
from pdcast.util.type_hints import type_specifier


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


##########################
####    PRIMITIVES    ####
##########################


cdef class BaseType:
    """Base type for all type objects.

    This has no interface of its own and merely serves to anchor inheritance.
    """

    registry: registry.TypeRegistry = registry.TypeRegistry()


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


######################
####    ATOMIC    ####
######################


cdef class AtomicType(ScalarType):
    """Base type for all user-defined atomic types.

    AtomicTypes hold all the necessary implementation logic for dispatched
    methods, conversions, and type-related functionality.  They can be linked
    together into tree structures to represent subtypes, and can be marked as
    generic to hold different implementations.  If you're looking to extend
    ``pdcast``, it will most likely boil down to writing a new AtomicType.
    """

    # Internal fields.  These should never be overridden.
    flyweights: dict[str, AtomicType] = {}

    # Default fields.  These can be overridden in AtomicType definitions to
    # customize behavior.
    conversion_func = convert.to_object
    type_def = None
    dtype = NotImplemented  # if using abstract extensions, uncomment this
    # dtype = np.dtype("O")  # if not using abstract extensions, uncomment this
    itemsize = None
    na_value = pd.NA
    is_nullable = True  # must be explicitly set False where applicable
    _is_boolean = None
    _is_numeric = None

    def __init__(self, **kwargs):
        self.kwargs = MappingProxyType(kwargs)
        self.slug = self.slugify(**kwargs)
        self.hash = hash(self.slug)

        # auto-generate an ExtensionDtype for objects of this type
        if self.dtype == NotImplemented:
            self.dtype = self._construct_extension_dtype()

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
    def from_dtype(
        cls,
        dtype: np.dtype | pd.api.extensions.ExtensionDtype
    ) -> AtomicType:
        """Construct an AtomicType from a corresponding numpy/pandas ``dtype``
        object.
        """
        return cls.instance()  # NOTE: most types disregard dtype fields

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
    def adapters(self) -> Iterator[adapter.AdapterType]:
        """Iterate through each AdapterType that is attached to this instance.

        For AtomicTypes, this is always an empty iterator.
        """
        yield from ()

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
    def subtypes(self) -> composite.CompositeType:
        """Return a CompositeType containing instances for every subtype
        currently registered to this AtomicType.

        The result is cached between `AtomicType.registry` updates, and can be
        customized via the `_generate_subtypes()` helper method.
        """
        if self.registry.needs_updating(self._subtype_cache):
            subtype_defs = traverse_subtypes(type(self))
            result = self._generate_subtypes(subtype_defs)
            self._subtype_cache = self.registry.remember(result)

        return composite.CompositeType(self._subtype_cache.value)

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

    def _construct_extension_dtype(self) -> pd.api.extensions.ExtensionDtype:
        """Construct a new pandas ``ExtensionDtype`` to refer to elements
        of this type.

        This method is only invoked if no explicit ``dtype`` is assigned to
        this ``AtomicType``.
        """
        return array.construct_extension_dtype(
            self,
            is_boolean=self._is_boolean,
            is_numeric=self._is_numeric,
            add_comparison_ops=True,
            add_arithmetic_ops=True
        )

    def contains(self, other: type_specifier) -> bool:
        """Test whether `other` is a subtype of the given AtomicType.
        This is functionally equivalent to `other in self`, except that it
        applies automatic type resolution to `other`.

        Override this to change the behavior of the `in` keyword and implement
        custom logic for membership tests of the given type.
        """
        other = resolve.resolve_type(other)
        if isinstance(other, composite.CompositeType):
            return all(self.contains(o) for o in other)

        # respect wildcard rules in subtypes
        subtypes = self.subtypes.atomic_types - {self}
        return other == self or any(other in a for a in subtypes)

    def is_na(self, val: Any) -> bool:
        """Check if an arbitrary value is an NA value in this representation.

        Override this if your type does something funky with missing values.
        """
        return val is pd.NA or val is None or val != val

    def is_subtype(self, other: type_specifier) -> bool:
        """Reverse of `AtomicType.contains()`.

        This is functionally equivalent to `self in other`, except that it
        applies automatic type resolution + `.contains()` logic to `other`.
        """
        return self in resolve.resolve_type(other)

    def make_nullable(self) -> AtomicType:
        """Create an equivalent AtomicType that can accept missing values."""
        if self.is_nullable:
            return self
        return self.generic.instance(backend="pandas", **self.kwargs)

    def replace(self, **kwargs) -> AtomicType:
        """Return a modified copy of the given AtomicType with the values
        specified in `**kwargs`.
        """
        cdef dict merged = {**self.kwargs, **kwargs}
        return self.instance(**merged)

    def strip(self) -> AtomicType:
        """Strip any adapters that have been attached to this AtomicType."""
        return self

    def upcast(self, series: convert.SeriesWrapper) -> AtomicType:
        """Attempt to upcast an AtomicType to fit the observed range of a
        series.
        """
        # NOTE: we convert to pyint to prevent inconsistent comparisons
        min_val = int(series.min - bool(series.min % 1))  # round floor
        max_val = int(series.max + bool(series.max % 1))  # round ceiling
        if min_val < self.min or max_val > self.max:
            # recursively search for a larger alternative
            for t in self.larger:
                try:
                    return t.upcast(series)
                except OverflowError:
                    pass

            # no matching type could be found
            raise OverflowError(
                f"could not upcast {self} to fit observed range "
                f"({series.min}, {series.max})"
            )

        # series fits type
        return self

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    def make_categorical(
        self,
        series: convert.SeriesWrapper,
        levels: list
    ) -> convert.SeriesWrapper:
        """Convert a SeriesWrapper of the associated type into a categorical
        format, with the given levels.

        This is invoked whenever a categorical conversion is performed that
        targets this type.
        """
        if levels is None:
            categorical_type = pd.CategoricalDtype()
        else:
            categorical_type = pd.CategoricalDtype(
                pd.Index(levels, dtype=self.dtype)
            )
        return convert.SeriesWrapper(
            series.series.astype(categorical_type),
            hasnans=series.hasnans
            # element_type is set in AdapterType.wrap()
            )

    def make_sparse(
        self,
        series: convert.SeriesWrapper,
        fill_value: Any
    ) -> convert.SeriesWrapper:
        """Convert a SeriesWrapper of the associated type into a sparse format,
        with the given fill value.

        This is invoked whenever a sparse conversion is performed that targets
        this type.
        """
        if fill_value is None:
            fill_value = self.na_value
        sparse_type = pd.SparseDtype(series.dtype, fill_value)
        return convert.SeriesWrapper(
            series.series.astype(sparse_type),
            hasnans=series.hasnans
            # element_type is set in AdapterType.wrap()
        )

    def to_boolean(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert generic data to a boolean data type.

        Note: this method does not do any cleaning/pre-processing of the
        incoming data.  If a type definition requires this, then it should be
        implemented in its own `to_boolean()` equivalent before delegating
        to this method in the return statement.  Any changes made to this
        method will be propagated to the top-level `to_boolean()` and `cast()`
        functions when they are called on objects of the given type.
        """
        if series.hasnans:
            dtype = dtype.make_nullable()
        return series.astype(dtype, errors=errors)

    def to_integer(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        downcast: composite.CompositeType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert generic data to an integer data type.

        Note: this method does not do any cleaning/pre-processing of the
        incoming data.  If a type definition requires this, then it should be
        implemented in its own `to_integer()` equivalent before delegating
        to this method in the return statement.  Any changes made to this
        method will be propagated to the top-level `to_integer()` and `cast()`
        functions when they are called on objects of the given type.
        """
        if series.hasnans:
            dtype = dtype.make_nullable()

        series = series.astype(dtype, errors=errors)
        if downcast is not None:
            return dtype.downcast(series, smallest=downcast)
        return series

    def to_float(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        downcast: composite.CompositeType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert boolean data to a floating point data type."""
        series = series.astype(dtype, errors=errors)
        if downcast is not None:
            return dtype.downcast(series, smallest=downcast, tol=tol)
        return series

    def to_complex(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        downcast: composite.CompositeType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert boolean data to a complex data type."""
        series = series.astype(dtype, errors=errors)
        if downcast is not None:
            return dtype.downcast(series, smallest=downcast, tol=tol)
        return series

    def to_decimal(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert boolean data to a decimal data type."""
        return series.astype(dtype, errors=errors)

    def to_string(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        format: str,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
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
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        call: Callable,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
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
