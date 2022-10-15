cimport cython
cimport numpy as np
import numpy as np
import pandas as pd

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



# TODO: resolve_dtype() only works on scalars?
# -> use CompositeType() constructor for collections of type specifiers.


# TODO: get_dtype() should return a CompositeType on mixed input


# TODO: get_dtype on array inputs:
# - dtype != "O": resolve_dtype(dtype)
# - dtype == "O": scan through series
# - dtype == "sparse[O]": scan through series
# - dtype == "categorical[O]": scan through categories


# This is an example of the Flyweight pattern
# https://python-patterns.guide/gang-of-four/flyweight/


#########################
####    CONSTANTS    ####
#########################


# size (in items) to use for LRU cache registries
cdef unsigned short cache_size = 64


# Flyweight registries
cdef dict shared_registry = {}
cdef LRUDict datetime64_registry = LRUDict(maxsize=cache_size)
cdef LRUDict timedelta64_registry = LRUDict(maxsize=cache_size)


# default string storage backend
cdef str default_string_storage = DEFAULT_STRING_DTYPE.storage


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
        observed = CompositeType(observed)
        return all(
            any(observed_type == expected_type for expected_type in expected)
            for observed_type in observed
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


cpdef ElementType resolve_dtype(object typespec):
    """Resolve a scalar type specifier, returning an appropriate ElementType
    object.
    """
    # trivial case: typespec is already a valid type specifier
    if isinstance(typespec, ElementType):
        return typespec

    # atomic type
    if isinstance(typespec, type):
        if typespec == ElementType or issubclass(typespec, CompositeType):
            raise ValueError(f"{typespec} is not a valid type specifier")
        if not issubclass(typespec, ElementType):
            typespec = type_lookup.get(typespec, ObjectType)
        return typespec.instance()

    # numpy/pandas dtype object
    if isinstance(typespec, (np.dtype, pd.api.extensions.ExtensionDtype)):
        return parse_typespec_dtype(typespec)

    # type string
    try:
        return parse_typespec_string(typespec)
    except TypeError as err:
        raise TypeError(f"Could not interpret specifier of type: "
                        f"{type(typespec)}") from err



# def resolve_dtype(typespec) -> ElementType | CompositeType:
#     """ElementType factory function.  Constructs ElementType objects according
#     to the Flyweight pattern.
#     """
#     cdef CompositeType result
# 
#     # composite case
#     if hasattr(typespec, "__iter__") and not isinstance(typespec, str):
#         # check for empty sequence
#         if not typespec:
#             raise ValueError(f"type specifier must contain at least 1 "
#                              f"element: {typespec}")
# 
#         # convert to CompositeType and check if scalar
#         result = CompositeType(typespec)
#         if len(result) == 1:  # single element type
#             return result.pop()
#         return result  # multiple element types
# 
#     # scalar case
#     return resolve_dtype_scalar(typespec)


#######################
####    HELPERS    ####
#######################


cdef long long compute_hash(
    bint sparse = False,
    bint categorical = False,
    bint nullable = True,
    type base = None,
    str unit = None,
    unsigned long long step_size = 1,
    str storage = default_string_storage
):
    """Compute a unique hash based on the given ElementType properties."""
    return hash(
        (sparse, categorical, nullable, base, unit, step_size, storage)
    )


cdef set flatten_nested_typespec(object nested):
    """Flatten nested type specifiers for resolve_dtype."""
    cdef set result = set()

    for item in nested:
        if isinstance(item, (tuple, list, set, frozenset, CompositeType)):
            result.update(flatten_nested_typespec(item))
        else:
            result.add(item)

    return result


#######################
####    CLASSES    ####
#######################


cdef class CompositeType(set):
    """Set-like container for ElementType objects.

    Implements the same interface as the built-in set type, but is restricted
    to containing ElementType objects.  Also extends subset/superset/membership
    checks to include subtypes for each of the contained ElementTypes.
    """

    def __init__(self, arg = None):
        if arg is None:
            super(CompositeType, self).__init__()
        elif isinstance(arg, CompositeType):
            super(CompositeType, self).__init__(arg)
        elif hasattr(arg, "__iter__") and not isinstance(arg, str):
            super(CompositeType, self).__init__(
                resolve_dtype(t) for t in flatten_nested_typespec(arg)
            )
        else:
            super(CompositeType, self).__init__((resolve_dtype(arg),))

    ##########################################
    ####    EXPAND/REDUCE MEMBER TYPES    ####
    ##########################################

    def expand(self) -> CompositeType:
        """Expand each of the ElementTypes contained within the CompositeType
        to include each of their subtypes.
        """
        return self.union(
            element_type.subtypes for element_type in self
        )

    def reduce(self) -> CompositeType:
        """Return a copy of the CompositeType with subtypes removed if they are
        fully encapsulated within the other members of the CompositeType.
        """
        raise NotImplementedError()

    #######################################
    ####    COMPARE SUBSET/SUPERSET    ####
    #######################################

    def isdisjoint(self, other) -> bool:
        """Return `True` if the CompositeType has no ElementTypes in common
        with `other`.

        CompositeTypes are disjoint if and only if their intersection is the
        empty set.
        """
        return not self.intersection(other)

    def issubset(self, other) -> bool:
        """Test whether every ElementType in the CompositeType is also in
        `other`.

        Identical to `self <= other`.
        """
        return self <= other

    def issuperset(self, other) -> bool:
        """Test whether every ElementType in `other` is contained within the
        CompositeType.

        Identical to `self >= other`.
        """
        return self >= other

    ###################################
    ####    ADD/REMOVE ELEMENTS    ####
    ###################################

    def add(self, typespec) -> None:
        """Add a type specifier to the CompositeType."""
        super(CompositeType, self).add(resolve_dtype(typespec))

    def remove(self, typespec) -> None:
        """Remove the given type specifier from the CompositeType.  Raises a
        KeyError if `typespec` is not contained in the set.

        Identical to the built-in `set.remove()` method, but extends
        ElementType resolution to the given type specifier.
        """
        super(CompositeType, self).remove(resolve_dtype(typespec))

    def discard(self, typespec) -> None:
        """Remove the given type specifier from the CompositeType if it is
        present.

        Identical to the built-in `set.discard()` method, but extends
        ElementType resolution to the given type specifier.
        """
        super(CompositeType, self).discard(resolve_dtype(typespec))

    ##########################
    ####    MERGE SETS    ####
    ##########################

    def intersection(self, *others) -> CompositeType:
        """Return a new CompositeType with ElementTypes in common to the
        CompositeType and all others.
        """
        cdef CompositeType result = self.expand()
        cdef ElementType element_type

        for item in others:
            for element_type in result:
                if element_type not in CompositeType(item):
                    result.remove(element_type)

        return result.reduce()

    def difference(self, *others) -> CompositeType:
        """Return a new CompositeType with ElementTypes that are not in any of
        the others.
        """
        cdef CompositeType result = self.expand()
        cdef ElementType element_type

        for item in others:
            for element_type in result:
                if element_type in CompositeType(item):
                    result.remove(element_type)

        return result.reduce()

    def symmetric_difference(self, other) -> CompositeType:
        """Return a new CompositeType with ElementTypes that are in either the
        original CompositeType or `other`, but not both.
        """
        cdef CompositeType result = self.expand()
        cdef CompositeType disregard = CompositeType(other)
        cdef ElementType element_type

        # TODO: figure out how to do this
        # -> self.difference(self.intersection())?


    def union(self, *others) -> CompositeType:
        """Return a new CompositeType with all the ElementTypes from this
        CompositeType and all others.

        Arguments to this method are resolved during the union calculation and
        can be arbitrarily nested.
        """
        cdef CompositeType result = self.copy()
        result.update(*others)
        return result


    def copy(self) -> CompositeType:
        """Return a shallow copy of the CompositeType."""
        return CompositeType(self)

    ###############################
    ####    UPDATE IN-PLACE    ####
    ###############################

    def update(self, *others) -> None:
        """Update the CompositeType in-place, adding ElementTypes from all
        others.

        Arguments to this method are resolved during the update process and can
        be arbitrarily nested.
        """
        cdef ElementType element_type

        for item in others:
            for element_type in CompositeType(item):
                super(CompositeType, self).add(element_type)

    #############################
    ####    MAGIC METHODS    ####
    #############################

    # TODO: non-operator versions of union(), intersection(), difference(),
    # symmetric_difference(), issubset(), and issuperset() should all accept
    # any value as an argument.  Their operator-based counterparts should
    # require their arguments to be CompositeType instances instead.

    def __contains__(self, other) -> bool:
        """Test whether a given type specifier is a member of `self` or one of
        its subtypes.
        """
        other = resolve_dtype(other)
        return any(other in elem for elem in self)

    def __le__(self, other) -> bool:
        """Test whether every element in `self` is contained within `other`.

        Identical to ``self.issubset(other)``.
        """
        other = CompositeType(other)
        return all(self_type in other for self_type in self)

    def __lt__(self, other) -> bool:
        """Test whether `self` is a proper subset of `other`
        (``self <= other and self != other``).
        """
        other = CompositeType(other)
        return (
            not self.__eq__(other) and
            all(self_type in other for self_type in self)
        )

    def __eq__(self, other) -> bool:
        """Test whether `self` and `other` contain identical ElementTypes."""
        return super(CompositeType, self).__eq__(CompositeType(other))

    def __ge__(self, other) -> bool:
        """Test whether every element in `other` is contained within `self`.

        Identical to ``self.issuperset(other)``.
        """
        other = CompositeType(other)
        return all(other_type in self for other_type in other)

    def __gt__(self, other) -> bool:
        """Test whether `self` is a proper superset of `other`
        (``self >= other and self != other``).
        """
        other = CompositeType(other)
        return (
            not self.__eq__(other) and
            all(other_type in self for other_type in other)
        )

    def __or__(self, other) -> None:
        """Return a new CompositeType containing the ElementTypes of `self`
        and all others.
        """
        # TODO: identical to self.union(*others)
        other = CompositeType(other)
        super(CompositeType, self).__or__(set(other))

    def __ror__(self, other) -> None:
        """Reverse operation of ``__or__``."""
        return self.__or__(other)

    def __and__(self, other) -> None:
        """Return a new CompositeType containing the ElementTypes common to
        `self` and all others.
        """
        # TODO: identical to self.intersection(*others)
        # TODO: finish this
        other = CompositeType(other)


    def __repr__(self) -> str:
        if self:
            return f"{self.__class__.__name__}({set(self)})"
        return f"{self.__class__.__name__}()"

    def __str__(self) -> str:
        if self:
            return f"{{{', '.join(str(t) for t in self)}}}"
        return "{}"


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

    @classmethod
    def instance(
        cls,
        bint sparse = False,
        bint categorical = False
    ) -> ElementType:
        """Flyweight Constructor."""
        # hash arguments
        cdef long long _hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=cls
        )

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

    def __contains__(self, other) -> bool:
        """Test whether the given type specifier is a subtype of this
        ElementType.
        """
        return resolve_dtype(other) in self.subtypes

    def __eq__(self, other) -> bool:
        return self.hash == hash(resolve_dtype(other))

    def __hash__(self) -> int:
        return self.hash

    def __repr__(self) -> str:
        return (
            f"{self.__class__.__name__}("
            f"sparse={self.sparse}, "
            f"categorical={self.categorical}"
            f")"
        )

    def __str__(self) -> str:
        return self.slug
