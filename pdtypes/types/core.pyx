from pdtypes import DEFAULT_STRING_DTYPE
from pdtypes.util.structs cimport LRUDict

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


# TODO: after implementing get_dtype, resolve_dtype, make this cdef-only once
# again.


cpdef object element_type(
    object base,
    bint is_categorical = False,
    bint is_sparse = False,
    bint is_nullable = False,
    str unit = None,
    unsigned long step_size = 1,
    str storage = None
):
    """ElementType factory function.  Constructs ElementType objects according
    to the Flyweight pattern.
    """
    # integer/boolean types (not nullable by default)
    if issubclass(base, (BooleanType, IntegerType)):
        return integer_or_boolean_type(
            base=base,
            is_categorical=is_categorical,
            is_sparse=is_sparse,
            is_nullable=is_nullable
        )

    # numpy datetime/timedelta special cases
    if issubclass(base, NumpyDatetime64Type):
        return numpy_datetime64_type(
            base=base,
            is_categorical=is_categorical,
            is_sparse=is_sparse,
            unit=unit,
            step_size=step_size
        )
    if issubclass(base, NumpyTimedelta64Type):
        return numpy_timedelta64_type(
            base=base,
            is_categorical=is_categorical,
            is_sparse=is_sparse,
            unit=unit,
            step_size=step_size
        )

    # string special case
    if issubclass(base, StringType):
        return string_type(
            base=base,
            is_categorical=is_categorical,
            is_sparse=is_sparse,
            storage=storage
        )

    # standard case
    return nullable_type(
        base=base,
        is_categorical=is_categorical,
        is_sparse=is_sparse
    )


#######################
####    HELPERS    ####
#######################


cdef object integer_or_boolean_type(
    object base,
    bint is_categorical = False,
    bint is_sparse = False,
    bint is_nullable = False
):
    """Construct an IntegerType or BooleanType object according to the
    Flyweight pattern.
    """
    cdef long _hash
    cdef object flyweight

    # compute property hash
    _hash = hash((base, is_categorical, is_nullable, is_sparse))

    # check shared registry for previous flyweight definition
    flyweight = shared_registry.get(_hash, None)
    if flyweight is None:
        # construct a new flyweight and add it to the registry
        flyweight = base(
            is_categorical=is_categorical,
            is_nullable=is_nullable,
            is_sparse=is_sparse
        )
        shared_registry[_hash] = flyweight

    # return flyweight object
    return flyweight


cdef object nullable_type(
    object base,
    bint is_categorical = False,
    bint is_sparse = False
):
    """Construct a nullable ElementType object according to the Flyweight
    pattern.
    """
    cdef long _hash
    cdef object flyweight

    # compute property hash (`is_nullable=True` by default)
    _hash = hash((base, is_categorical, True, is_sparse))

    # check shared registry for previous flyweight definition
    flyweight = shared_registry.get(_hash, None)
    if flyweight is None:
        # construct a new flyweight
        flyweight = base(
            is_categorical=is_categorical,
            is_sparse=is_sparse
        )

        # add flyweight to shared registry
        shared_registry[_hash] = flyweight

    # return flyweight object
    return flyweight


cdef object numpy_datetime64_type(
    object base,
    bint is_categorical = False,
    bint is_sparse = False,
    str unit = None,
    unsigned long step_size = 1
):
    """Construct a NumpyDatetime64Type according to the Flyweight pattern."""
    cdef long _hash
    cdef object flyweight

    # compute property hash (`is_nullable=True` by default)
    _hash = hash((base, is_categorical, True, is_sparse, unit, step_size))

    # check LRU datetime64 registry for previous flyweight definition
    flyweight = datetime64_registry.get(_hash, None)
    if flyweight is None:
        # construct a new flyweight and add it to the LRU registry
        flyweight = base(
            is_categorical=is_categorical,
            is_sparse=is_sparse,
            unit=unit,
            step_size=step_size
        )

        # add flyweight to LRU datetime64 registry
        datetime64_registry[_hash] = flyweight

    # return flyweight object
    return flyweight


cdef object numpy_timedelta64_type(
    object base,
    bint is_categorical = False,
    bint is_sparse = False,
    str unit = None,
    unsigned long step_size = 1
):
    """Construct a NumpyDatetime64Type according to the Flyweight pattern."""
    cdef long _hash
    cdef object flyweight

    # compute property hash (`is_nullable=True` by default)
    _hash = hash((base, is_categorical, True, is_sparse, unit, step_size))

    # check LRU timedelta64 registry for previous flyweight definition
    flyweight = timedelta64_registry.get(_hash, None)
    if flyweight is None:
        # construct a new flyweight
        flyweight = base(
            is_categorical=is_categorical,
            is_sparse=is_sparse,
            unit=unit,
            step_size=step_size
        )

        # add flyweight to LRU timedelta64 registry
        timedelta64_registry[_hash] = flyweight

    # return flyweight object
    return flyweight


cdef object string_type(
    object base,
    bint is_categorical = False,
    bint is_sparse = False,
    str storage = None
):
    """Construct a StringType object according to the Flyweight pattern."""
    cdef long _hash
    cdef object flyweight

    # resolve default storage type
    if storage is None:
        storage = default_string_storage

    # compute property hash (`is_nullable=True` by default)
    _hash = hash((base, is_categorical, True, is_sparse, storage))

    # check shared registry for previous flyweight definition
    flyweight = shared_registry.get(_hash, None)
    if flyweight is None:
        # construct a new flyweight
        flyweight = base(
            is_categorical=is_categorical,
            is_sparse=is_sparse,
            stora=storage
        )

        # add flyweight to shared registry
        shared_registry[_hash] = flyweight

    # return flyweight object
    return flyweight
