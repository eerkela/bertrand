cimport cython
cimport numpy as np
import numpy as np
import pandas as pd
from typing import Iterator

from pdtypes import DEFAULT_STRING_DTYPE
from pdtypes.util.structs cimport LRUDict

from .parse cimport (
    parse_example_scalar, parse_example_vector, parse_typespec_dtype,
    parse_typespec_string, parse_typespec_type
)
from .parse.type cimport type_lookup

from .boolean cimport *
from .integer cimport *
from .float cimport *
from .complex cimport *
from .decimal cimport *
from .datetime cimport *
from .timedelta cimport *
from .string cimport *
from .object cimport *


# TODO: Consider appending aliases to type object


# TODO: CompositeType should not take `index` argument in constructor.  This
# should be populated by get_dtype exclusively and ignored when doing set
# operations on CompositeType objects.


# TODO: probably a good idea not to overload __contains__.  Offer separate
# is_subtype()/is_supertype() methods instead.  These are more explicit and
# easy to read.


# TODO: resolve_dtype should accept optional boolean arguments:
# `sparse`, `categorical`, `nullable`.  In cast(), sparse and categorical are
# passed as-is.
# -> currently these are broken


# TODO: If sparse=False and/or categorical=False, add respective combinations
# to subtypes, just like nullable=False
# -> this causes "sparse[int]" in "int" to return True
# -> ("sparse[categorical[nullable[int64]]]" in "int") == True


# TODO: If sparse=True/categorical=True, have ElementType.pandas_type reflect
# that.  This would require an optional categories argument to ElementType
# constructors.


# TODO: resolve_dtype doesn't work on fixed-length numpy string dtypes
# -> maybe add a separate StringType for fixed-length strings?


# TODO: get_dtype on array inputs:
# - dtype != "O": resolve_dtype(dtype)
# - dtype == "O": scan through series
# - dtype == "sparse[O]": scan through series
# - dtype == "categorical[O]": scan through categories


# NOTE: This is an example of the Flyweight pattern
# https://python-patterns.guide/gang-of-four/flyweight/


#########################
####    CONSTANTS    ####
#########################


# size (in items) to use for LRU cache registries
cdef unsigned short cache_size = 64


# Flyweight registries
cdef dict shared_registry = {}
cdef LRUDict datetime64_registry = LRUDict(maxsize=cache_size)
cdef LRUDict decimal_registry = LRUDict(maxsize=cache_size)
cdef LRUDict object_registry = LRUDict(maxsize=cache_size)
cdef LRUDict timedelta64_registry = LRUDict(maxsize=cache_size)


# slug registries
cdef dict base_slugs = {
    # boolean
    BooleanType: "bool",

    # integer
    IntegerType: "int",
    SignedIntegerType: "signed int",
    Int8Type: "int8",
    Int16Type: "int16",
    Int32Type: "int32",
    Int64Type: "int64",
    UnsignedIntegerType: "unsigned int",
    UInt8Type: "uint8",
    UInt16Type: "uint16",
    UInt32Type: "uint32",
    UInt64Type: "uint64",

    # float
    FloatType: "float",
    Float16Type: "float16",
    Float32Type: "float32",
    Float64Type: "float64",
    LongDoubleType: "longdouble",

    # complex
    ComplexType: "complex",
    Complex64Type: "complex64",
    Complex128Type: "complex128",
    CLongDoubleType: "clongdouble",

    # decimal
    DecimalType: "decimal",

    # datetime
    DatetimeType: "datetime",
    PandasTimestampType: "datetime[pandas]",
    PyDatetimeType: "datetime[python]",
    NumpyDatetime64Type: "M8",

    # timedelta
    TimedeltaType: "timedelta",
    PandasTimedeltaType: "timedelta[pandas]",
    PyTimedeltaType: "timedelta[python]",
    NumpyTimedelta64Type: "m8",

    # string
    StringType: "string",

    # object:
    ObjectType: "object"  # TODO: special case?
}


######################
####    PUBLIC    ####
######################


def check_dtype(example, typespec, exact: bool = False) -> bool:
    """TODO"""
    observed = get_dtype(example)
    expected = CompositeType(typespec)

    # return based on `exact`
    if exact:
        # return True if and only if every type in `observed` is explicitly
        # declared in `typespec`
        return all(
            any(observed_type == expected_type for expected_type in expected)
            for observed_type in CompositeType(observed)
        )

    # return True if `observed` is a subset of `expected`
    return observed <= expected


def get_dtype(example) -> ElementType | CompositeType:
    """ElementType factory function.  Constructs ElementType objects according
    to the Flyweight pattern.
    """
    # trivial case: example is already a valid type specifier
    if isinstance(example, (ElementType, CompositeType)):
        return example

    cdef object missing
    cdef object dtype

    # np.ndarray
    if isinstance(example, np.ndarray):
        dtype = example.dtype
        if np.issubdtype(dtype, "O"):
            missing = pd.isna(example)
            return parse_example_vector(
                example[~missing],
                sparse=False,
                categorical=False,
                force_nullable=missing.any()
            )
        return parse_typespec_dtype(dtype)

    # pd.Series
    if isinstance(example, pd.Series):
        dtype = example.dtype
        missing = pd.isna(example)

        # object dtype - can be sparse
        if pd.api.types.is_object_dtype(dtype):
            return parse_example_vector(
                example[~missing].to_numpy(),
                sparse=pd.api.types.is_sparse(dtype),
                categorical=False,
                force_nullable=missing.any()
            )

        # non-object dtype - can be categorical or sparse + categorical
        return parse_typespec_dtype(
            dtype,
            sparse=pd.api.types.is_sparse(dtype),
            categorical=False,
            force_nullable=missing.any()
        )

    # scalar
    return parse_example_scalar(example)


cpdef ElementType resolve_dtype(
    object typespec,
    bint sparse = False,
    bint categorical = False,
    bint nullable = False
):
    """Resolve a scalar type specifier, returning an appropriate ElementType
    object.
    """
    # trivial case: typespec is already a valid type specifier
    if isinstance(typespec, ElementType):
        return typespec

    # atomic type
    if isinstance(typespec, type):
        # reject ElementType/CompositeType specifiers
        if typespec == ElementType or issubclass(typespec, CompositeType):
            raise ValueError(f"{typespec} is not a valid type specifier")

        # get ElementType definition for given type
        if not issubclass(typespec, ElementType):
            typespec = type_lookup.get(typespec, ObjectType)

        # apply nullable
        if issubclass(typespec, (BooleanType, IntegerType)):
            return typespec.instance(
                sparse=sparse,
                categorical=categorical,
                nullable=nullable
            )
        return typespec.instance(
            sparse=sparse,
            categorical=categorical
        )

    # numpy/pandas dtype object
    if isinstance(typespec, (np.dtype, pd.api.extensions.ExtensionDtype)):
        return parse_typespec_dtype(
            typespec,
            sparse=sparse,
            categorical=categorical,
            force_nullable=nullable
        )

    # type string
    try:
        return parse_typespec_string(typespec)
    except TypeError as err:
        raise TypeError(f"Could not interpret specifier of type: "
                        f"{type(typespec)}") from err


#######################
####    HELPERS    ####
#######################


cdef set flatten_nested_typespec(object nested):
    """Flatten nested type specifiers for resolve_dtype."""
    cdef set result = set()

    for item in nested:
        if isinstance(item, (tuple, list, set, frozenset, CompositeType)):
            result.update(flatten_nested_typespec(item))
        else:
            result.add(item)

    return result


cdef str generate_slug(
    type base_type,
    bint sparse,
    bint categorical
):
    """Return a unique slug string associated with the given `base_type`,
    accounting for `sparse`, `categorical` flags.
    """
    cdef str slug = base_slugs[base_type]

    if categorical:
        slug = f"categorical[{slug}]"
    if sparse:
        slug = f"sparse[{slug}]"

    return slug


#######################
####    CLASSES    ####
#######################


cdef class CompositeType:
    """Set-like container for ElementType objects.

    Implements the same interface as the built-in set type, but is restricted
    to containing ElementType objects.  Also extends subset/superset/membership
    checks to include subtypes for each of the contained ElementTypes.
    """

    def __init__(self, arg = None):
        if arg is None:
            self.types = set()
        elif isinstance(arg, CompositeType):
            self.types = arg.types
        elif hasattr(arg, "__iter__") and not isinstance(arg, str):
            self.types = {
                resolve_dtype(t) for t in flatten_nested_typespec(arg)
            }
        else:
            self.types = {resolve_dtype(arg)}

        # NOTE: index is only populated by get_dtype()
        self.index = None

    ###############################
    ####    UTILITY METHODS    ####
    ###############################

    def expand(self) -> CompositeType:
        """Expand each of the ElementTypes contained within the CompositeType
        to include each of their subtypes.
        """
        cdef ElementType element_type

        return self.union(element_type.subtypes for element_type in self)

    def reduce(self) -> CompositeType:
        """Return a copy of the CompositeType with redundant subtypes removed.
        A subtype is redundant if it is fully encapsulated within the other
        members of the CompositeType.
        """
        cdef ElementType element_type
        cdef ElementType t

        return CompositeType(
            element_type for element_type in self
            if not any(
                (element_type != t) and (element_type in t) for t in self.types
            )
        )

    ####################################
    ####    STATIC WRAPPER (SET)    ####
    ####################################

    def add(self, typespec) -> None:
        """Add a type specifier to the CompositeType."""
        self.types.add(resolve_dtype(typespec))

    def clear(self) -> None:
        """Remove all ElementTypes from the CompositeType."""
        self.types.clear()

    def copy(self) -> CompositeType:
        """Return a shallow copy of the CompositeType."""
        result = CompositeType(self.types.copy())
        if self.index is not None:
            result.index = self.index.copy()
        return result

    def difference(self, *others) -> CompositeType:
        """Return a new CompositeType with ElementTypes that are not in any of
        the others.
        """
        cdef ElementType x
        cdef ElementType y
        cdef CompositeType other
        cdef CompositeType result = self.expand()

        for item in others:
            other = CompositeType(item).expand()
            result = CompositeType(
                x for x in result if not any(y in x for y in other)
            )

        # expand/reduce called only once
        return result.reduce()

    def difference_update(self, *others) -> None:
        """Update a CompositeType in-place, removing ElementTypes that can be
        found in others.
        """
        self.types = self.difference(*others).types

    def discard(self, typespec) -> None:
        """Remove the given type specifier from the CompositeType if it is
        present.
        """
        self.types.discard(resolve_dtype(typespec))

    def intersection(self, *others) -> CompositeType:
        """Return a new CompositeType with ElementTypes in common to this
        CompositeType and all others.
        """
        cdef CompositeType other
        cdef CompositeType result = self.copy()

        for item in others:
            other = CompositeType(other)
            result = CompositeType(
                {t for t in result if t in other} |
                {t for t in other if t in result}
            )

        return result

    def intersection_update(self, *others) -> None:
        """Update a CompositeType in-place, keeping only the ElementTypes found
        in it and all others.
        """
        self.types = self.intersection(*others).types

    def isdisjoint(self, other) -> bool:
        """Return `True` if the CompositeType has no ElementTypes in common
        with `other`.

        CompositeTypes are disjoint if and only if their intersection is the
        empty set.
        """
        return not self & other

    def issubset(self, other) -> bool:
        """Test whether every ElementType in the CompositeType is also in
        `other`.
        """
        return self <= other

    def issuperset(self, other) -> bool:
        """Test whether every ElementType in `other` is contained within the
        CompositeType.
        """
        return self >= other

    def pop(self) -> ElementType:
        """Remove and return an arbitrary ElementType from the CompositeType.
        Raises a KeyError if the CompositeType is empty.
        """
        return self.types.pop()

    def remove(self, typespec) -> None:
        """Remove the given type specifier from the CompositeType.  Raises a
        KeyError if `typespec` is not contained in the set.
        """
        self.types.remove(resolve_dtype(typespec))

    def symmetric_difference(self, other) -> CompositeType:
        """Return a new CompositeType with ElementTypes that are in either the
        original CompositeType or `other`, but not both.
        """
        other = CompositeType(other)
        return (self - other) | (other - self)

    def symmetric_difference_update(self, other) -> None:
        """Update a CompositeType in-place, keeping only ElementTypes that
        are found in either `self` or `other`, but not both.
        """
        self.types = self.symmetric_difference(other).types

    def union(self, *others) -> CompositeType:
        """Return a new CompositeType with all the ElementTypes from this
        CompositeType and all others.
        """
        return CompositeType(
            self.types.union(*[CompositeType(o).types for o in others])
        )

    def update(self, *others) -> None:
        """Update the CompositeType in-place, adding ElementTypes from all
        others.
        """
        self.types = self.union(*others).types

    #############################
    ####    MAGIC METHODS    ####
    #############################

    def __repr__(self) -> str:
        if self.types:
            return f"{type(self).__name__}({self.types})"
        return f"{type(self).__name__}()"

    def __str__(self) -> str:
        if self.types:
            return f"{{{', '.join(str(t) for t in self.types)}}}"
        return "{}"

    def __iter__(self):
        """Iterate through the ElementTypes contained within a CompositeType.
        """
        return iter(self.types)

    def __len__(self):
        """Return the number of ElementTypes in the CompositeType."""
        return len(self.types)

    def __contains__(self, other) -> bool:
        """Test whether a given type specifier is a member of `self` or one of
        its subtypes.
        """
        other = resolve_dtype(other)
        return any(other in t for t in self)

    def __le__(self, other) -> bool:
        """Test whether every element in `self` is contained within `other`."""
        other = CompositeType(other)
        return all(t in other for t in self)

    def __lt__(self, other) -> bool:
        """Test whether `self` is a proper subset of `other`
        (``self <= other and self != other``).
        """
        return self != other and self <= other

    def __eq__(self, other) -> bool:
        """Test whether `self` and `other` contain identical ElementTypes."""
        return self.types == CompositeType(other).types

    def __ge__(self, other) -> bool:
        """Test whether every element in `other` is contained within `self`.
        """
        other = CompositeType(other)
        return all(t in self for t in other)

    def __gt__(self, other) -> bool:
        """Test whether `self` is a proper superset of `other`
        (``self >= other and self != other``).
        """
        return self != other and self >= other

    def __or__(self, other) -> CompositeType:
        """Return a new CompositeType containing the ElementTypes of `self`
        and all others.
        """
        return self.union(other)

    def __ior__(self, other) -> CompositeType:
        """Update a CompositeType in-place, adding ElementTypes from all
        others.
        """
        self.update(other)
        return self

    def __and__(self, other) -> CompositeType:
        """Return a new CompositeType containing the ElementTypes common to
        `self` and all others.
        """
        return self.intersection(other)

    def __iand__(self, other) -> CompositeType:
        """Update a CompositeType in-place, keeping only the ElementTypes found
        in it and all others.
        """
        self.intersection_update(other)
        return self

    def __sub__(self, other) -> CompositeType:
        """Return a new CompositeType with ElementTypes that are not in the
        others.
        """
        return self.difference(other)

    def __isub__(self, other) -> CompositeType:
        """Update a CompositeType in-place, removing ElementTypes that can be
        found in others.
        """
        self.difference_update(other)
        return self

    def __xor__(self, other) -> CompositeType:
        """Return a new CompositeType with ElementTypes that are in either
        `self` or `other` but not both.
        """
        return self.symmetric_difference(other)

    def __ixor__(self, other) -> CompositeType:
        """Update a CompositeType in-place, keeping only ElementTypes that
        are found in either `self` or `other`, but not both.
        """
        self.symmetric_difference_update(other)
        return self


cdef class ElementType:
    """Base class for type definitions.

    Attributes
    ----------
    categorical : bool
        `True` if ElementType represents categorical data.  `False` otherwise.
    sparse : bool
        `True` if ElementType represents sparse data.  `False` otherwise.
    nullable : bool
        `True` if ElementType can take missing/null values.  `False` otherwise.
    supertype : type
        The supertype to which this ElementType is attached.  If the
        ElementType is itself a top-level supertype, this will be `None`.
    subtypes : frozenset, default frozenset()
        An immutable set of subtypes associated with this ElementType.  If
        the ElementType has no subtypes, this will be an empty set.
    atomic_type : type
        The atomic type associated with each of this type's indices in a numpy
        array or pandas series.  If the ElementType describes a scalar value,
        this will always be equivalent to `type(scalar)`.  If no equivalent
        atomic type exists, this defaults to `None`.
    numpy_type : np.dtype
        The numpy dtype object associated with the given ElementType, if one
        exists.  These allow users to hook into the existing numpy dtype
        functionality, if desired.  If no equivalent numpy dtype exists, this
        defaults to `None`.
    pandas_type : pd.api.extensions.ExtensionDType
        The pandas extension type associated with the given ElementType, if one
        exists.  These allow non-nullable numpy types to accept missing values
        in the form of `pd.NA`, as well as exposing custom string storage
        backends, etc.  If no equivalent pandas type exists, this defaults to
        `None`.
    hash : int
        A unique hash value based on the unique settings of this ElementType.
        This is used for caching operations and equality checks.
    slug : str
        A shortened string identifier (e.g. 'int64', 'bool', etc.) for this
        ElementType.
    """

    def __init__(
        self,
        bint sparse,
        bint categorical,
        bint nullable,
        type atomic_type,
        object numpy_type,
        object pandas_type,
        object na_value,
        object itemsize,
        str slug
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = nullable
        self.atomic_type = atomic_type
        self.numpy_type = numpy_type
        self.pandas_type = pandas_type
        self.na_value = na_value
        self.itemsize = itemsize
        self.slug = slug
        self.hash = hash(slug)
        self._subtypes = None
        self._supertype = None

    @classmethod
    def instance(
        cls,
        bint sparse = False,
        bint categorical = False
    ) -> ElementType:
        """Flyweight Constructor."""
        # construct slug
        cdef str slug = generate_slug(
            base_type=cls,
            sparse=sparse,
            categorical=categorical
        )

        # hash slug
        cdef long long _hash = hash(slug)

        # get previous flyweight if one exists
        cdef ElementType result = shared_registry.get(_hash, None)

        if result is None:
            # construct new flyweight
            result = cls(
                sparse=sparse,
                categorical=categorical
            )
    
            # add flyweight to registry
            shared_registry[_hash] = result

        # return flyweight
        return result

    @property
    def subtypes(self) -> frozenset:
        if self._subtypes is None:
            self._subtypes = frozenset({self})
        return self._subtypes

    @property
    def supertype(self) -> ElementType:
        return self._supertype

    def is_subtype(self, other) -> bool:
        return self in CompositeType(other)

    def __contains__(self, other) -> bool:
        """Test whether the given type specifier is a subtype of this
        ElementType.

        Examples
        --------
        >>> series.get_dtype() == int
        >>> series.get_dtype() in CompositeType({int, bool})
        >>> series.get_dtype().is_subtype({int, bool})
        >>> "int" in series.get_dtype()
        """
        return all(t in self.subtypes for t in CompositeType(other))

    def __eq__(self, other) -> bool:
        return self.hash == hash(resolve_dtype(other))

    def __hash__(self) -> int:
        return self.hash

    def __iter__(self) -> Iterator[ElementType]:
        return iter(self.subtypes)

    def __len__(self) -> int:
        return len(self.subtypes)

    def __repr__(self) -> str:
        return (
            f"{type(self).__name__}("
            f"sparse={self.sparse}, "
            f"categorical={self.categorical}"
            f")"
        )

    def __str__(self) -> str:
        return self.slug
