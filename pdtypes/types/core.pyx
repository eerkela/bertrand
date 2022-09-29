from pdtypes import DEFAULT_STRING_DTYPE
from pdtypes.util.structs cimport LRUDict

from .base cimport ElementType
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


cpdef ElementType factory(
    type base,
    bint is_categorical = False,
    bint is_sparse = False,
    bint is_nullable = True,
    str unit = None,
    unsigned long long step_size = 1,
    str storage = default_string_storage
):
    """ElementType factory function.  Constructs ElementType ElementTypes according
    to the Flyweight pattern.
    """
    cdef tuple props
    cdef long long _hash
    cdef dict registry
    cdef ElementType element_type

    # TODO: get **kwargs dict based on input (get_dtype/resolve_dtype)
    # (array, scalar, series) vs (string, type, dtype object, ElementType)

    # resolve_dtype:
    # is_sparse - identifier is SparseDtype() or has sparse[] directive.
    # is_categorical - identifier is CategoricalDtype() or has categorical[]
    #   directive
    # is_nullable - False if specifier is integer or boolean and not a pandas
    #   extension type.  Otherwise True.
    # base - consult the appropriate lookup table
    # unit - if base is M8/m8, check for the presence of units
    # step_size - if unit is found, check for step_size
    # storage - if base is string, check for presence of storage directive
    #   defaults to DEFAULT_STRING_DTYPE.storage.

    # if identifier is an atomic type:
    # is_sparse = False
    # is_categorical = False
    # is_nullable = False if type in numpy integer, boolean types, else True
    # base = consult the appropriate lookup table
    # unit = None
    # step_size = None
    # storage = default_storage_type
    # -> 1 dict lookup + issubclass(type, (bool, np.bool_, np.integer))

    # get_dtype:
    # is_sparse - data is masked array or sparse Series.
    # is_categorical - data is a categorical series.
    # is_nullable - data is an array/series and has missing values.
    # base - if scalar, resolve_dtype(type(data)).  If non-object array/series,
    #   resolve_dtype(data.dtype).  If object array/series, iteratively build a
    #   set, running resolve_dtype(type(data)) on each index.
    #       -> performance: 1 type operation, 1 dict lookup, 1 issubclass call,
    #       1 hash, 1 dict lookup, return, all in pure cython.
    #       object -> type -> lookup -> issubclass(int/bool), issubclass(time) -> hash -> lookup -> ElementType
    # unit - If datetime64 scalar, run np.datetime_data directly on input.  If
    #   input is array and np.issubdtype(array.dtype, "M8"/"m8"), run
    #   np.datetime_data on array.dtype.  If object array or series, insert an
    #   np.datetime_data call in issubclass switch.
    # step_size - same branch as unit.
    # storage - data is a pandas Series with pd.StringDtype().  Else, use
    #   default_string_storage.

    # gather properties into ordered tuple
    props = (
        is_sparse,
        is_categorical,
        is_nullable,
        base,
        unit,
        step_size,
        storage
    )

    # compute hash
    _hash = hash(props)

    # TODO: merge **kwargs dict with default value dictionary (module constant)
    # and cast values to tuple.  In python 3.9, this is:
    # _hash = hash(tuple((defaults | kwargs).values()))
    # In python 3.8 and below:
    # _hash = hash(tuple({**defaults, **kwargs}.values()))
    # -> no substantial performance difference.

    # get appropriate registry
    if issubclass(base, NumpyDatetime64Type):
        registry = datetime64_registry
    elif issubclass(base, NumpyTimedelta64Type):
        registry = timedelta64_registry
    else:
        registry = shared_registry

    # get flyweight
    element_type = registry.get(_hash, None)
    if element_type is None:
        # TODO: need an appropriate **kwargs dict for this step.  If using
        # get_dtype/resolve_dtype, this has already been computed.  Just call
        # element_type = base(**kwargs)
        # This will just raise a TypeError if any of the kwargs are invalid.

        # construct flyweight
        if issubclass(base, (BooleanType, IntegerType)):
            element_type = base(
                is_categorical=is_categorical,
                is_sparse=is_sparse,
                is_nullable=is_nullable
            )
        elif issubclass(base, NumpyDatetime64Type):
            element_type = base(
                is_categorical=is_categorical,
                is_sparse=is_sparse,
                unit=unit,
                step_size=step_size
            )
        elif issubclass(base, NumpyTimedelta64Type):
            element_type = base(
                is_categorical=is_categorical,
                is_sparse=is_sparse,
                unit=unit,
                step_size=step_size
            )
        elif issubclass(base, StringType):
            element_type = base(
                is_categorical=is_categorical,
                is_sparse=is_sparse,
                storage=storage
            )
        else:
            element_type = base(
                is_categorical=is_categorical,
                is_sparse=is_sparse
            )

        # add flyweight to registry
        registry[_hash] = element_type

    # return flyweight
    return element_type
