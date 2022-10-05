cimport numpy as np
import numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE
from pdtypes.util.structs cimport LRUDict

from .parse cimport parse_dtype, parse_string, parse_type

from .boolean cimport *
from .integer cimport *
from .float cimport *
from .complex cimport *
from .decimal cimport *
from .datetime cimport *
from .timedelta cimport *
from .string cimport *


# This is an example of the Flyweight pattern
# https://python-patterns.guide/gang-of-four/flyweight/


# TODO: ElementType hashes aren't currently stored.


#########################
####    CONSTANTS    ####
#########################


# size (in items) to use for LRU cache registries
cdef unsigned short cache_size = 64


# Flyweight registries
cdef dict shared_registry = {}
cdef LRUDict datetime64_registry = LRUDict(maxsize=cache_size)
cdef LRUDict timedelta64_registry = LRUDict(maxsize=cache_size)


# default arguments for ElementType construction
cdef dict defaults = {
    "sparse": False,
    "categorical": False,
    "nullable": True,
    "base": None,
    "unit": None,
    "step_size": 1,
    "storage": DEFAULT_STRING_DTYPE.storage
}


######################
####    PUBLIC    ####
######################


cpdef object get_dtype(object example):
    """ElementType factory function.  Constructs ElementType objects according
    to the Flyweight pattern.
    """
    cdef object missing

    # np.ndarray
    if isinstance(example, np.ndarray):
        if np.issubdtype(example.dtype, "O"):
            missing = pd.isna(example)
            return parse_example_vector(
                example[~missing],
                as_index=False,
                force_nullable=missing.any()
            )
        return resolve_dtype(example.dtype)

    # pd.Series
    if isinstance(example, pd.Series):
        if pd.api.types.is_object_dtype(example):
            missing = pd.isna(example)
            return parse_example_vector(
                example[~missing].to_numpy(),
                as_index=False,
                force_nullable=missing.any()
            )
        return resolve_dtype(example.dtype)

    # scalar
    return parse_example_scalar(example)



cpdef ElementType resolve_dtype(object typespec):
    """ElementType factory function.  Constructs ElementType objects according
    to the Flyweight pattern.
    """
    cdef dict kwargs

    # get appropriate **kwargs dict depending on input type specifier
    if isinstance(typespec, type):
        kwargs = parse_type(typespec)
    elif isinstance(typespec, (np.dtype, pd.api.extensions.ExtensionDtype)):
        kwargs = parse_dtype(typespec)
    elif isinstance(typespec, str):
        kwargs = parse_string(typespec)
    else:
        raise ValueError(f"type specifier not recognized: {repr(typespec)}")

    # return flyweight object
    return element_type_from_kwargs(kwargs)


#######################
####    HELPERS    ####
#######################


cdef ElementType element_type_from_kwargs(dict kwargs):
    """Construct an ElementType flyweight from the given kwarg dict."""
    cdef long long _hash
    cdef dict registry
    cdef ElementType result

    # compute hash
    _hash = hash(tuple({**defaults, **kwargs}.values()))

    # get appropriate registry
    if issubclass(kwargs["base"], NumpyDatetime64Type):
        registry = datetime64_registry
    elif issubclass(kwargs["base"], NumpyTimedelta64Type):
        registry = timedelta64_registry
    else:
        registry = shared_registry

    # get flyweight
    result = registry.get(_hash, None)
    if result is None:
        # construct flyweight object
        result = kwargs.pop("base")(**kwargs)

        # add flyweight to registry
        registry[_hash] = result

    # return flyweight
    return result


cdef ElementType parse_example_scalar(
    object typespec,
    bint force_nullable = False
):
    """Parse scalar example data, returning a corresponding ElementType object.
    """
    # trivial case: typespec is already an ElementType object
    if isinstance(typespec, ElementType):
        return typespec

    cdef dict kwargs

    # get appropriate kwargs dict depending on input type specifier
    if isinstance(typespec, (np.datetime64, np.timedelta64)):
        kwargs = parse_dtype(typespec.dtype)
    else:
        kwargs = parse_type(type(typespec))

    # comply with nullable flag
    if force_nullable and not kwargs.get("nullable", True):
        kwargs["nullable"] = True

    # return flyweight object
    return element_type_from_kwargs(kwargs)


cdef object parse_example_vector(
    np.ndarray[object] arr,
    bint as_index = False,
    bint force_nullable = False
):
    """Parse vectorized example data, returning a scalar ElementType or a set
    of ElementType objects if multiple are present (dtype="O").
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef object result

    # return as index
    if as_index:
        result = np.empty(arr_length, dtype="O")
        for i in range(arr_length):
            result[i] = parse_example_scalar(
                arr[i],
                force_nullable=force_nullable
            )
        return result

    # return as sclar/set
    result = set()
    for i in range(arr_length):
        result.add(parse_example_scalar(arr[i], force_nullable=force_nullable))

    # check for homogeneity
    if len(result) == 1:  # homogenous
        return result.pop()
    return result  # mixed


#######################
####    CLASSES    ####
#######################


cdef class ElementType:
    """Base class for type definitions.

    Attributes
    ----------
    categorical : bool
        `True` if ElementType represents categorical data.
    sparse : bool
        `True` if ElementType represents sparse data.
    nullable : bool
        `True` if ElementType can take missing/null values.
    supertype : type
        The supertype to which this ElementType is attached.  If the
        ElementType is itself a top-level supertype, this will be `None`.
    subtypes : tuple
        A tuple of subtypes associated with this ElementType
    atomic_type : type
        The atomic type associated with each of this type's indices in a numpy
        array or pandas series.  If the ElementType describes a scalar value,
        this will always be equivalent to `type(scalar)`.
    extension_type : pd.api.extensions.ExtensionDType
        A pandas extension type for the given ElementType, if it has one.
        These allow non-nullable types to accept missing values in the form of
        `pd.NA`, as well as exposing custom string storage backends, etc.
    hash : int
        A unique hash value based on the unique settings of this ElementType.
        This is used for caching operations and equality checks.
    slug : str
        A shortened string identifier (e.g. 'int64', 'bool', etc.) for this
        ElementType.
    """

    def __eq__(self, other) -> bool:
        return self.hash == hash(resolve_dtype(other))

    def __contains__(self, other) -> bool:
        return isinstance(resolve_dtype(other), self.__class__)

    def __hash__(self) -> int:
        return self.hash
