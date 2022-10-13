cimport numpy as np
import numpy as np
import pandas as pd

from pdtypes.types cimport (
    BooleanType, Int8Type, Int16Type, Int32Type, Int64Type, UInt8Type,
    UInt16Type, UInt32Type, UInt64Type, Float16Type, Float32Type, Float64Type,
    LongDoubleType, Complex64Type, Complex128Type, CLongDoubleType,
    NumpyDatetime64Type, NumpyTimedelta64Type, StringType, ObjectType
)
from .example cimport parse_example_vector


# TODO: bytes?


######################
####    PUBLIC    ####
######################


cdef object parse_typespec_dtype(
    object typespec,
    bint sparse = False,
    bint categorical = False,
    bint force_nullable = False
):
    """Convert a numpy/pandas dtype object into its corresponding ElementType.
    """
    cdef object categories
    cdef str unit
    cdef unsigned long long step_size
    cdef set category_types

    # sparse
    if pd.api.types.is_sparse(typespec):
        sparse = True
        typespec = typespec.subtype

    # categorical
    if pd.api.types.is_categorical_dtype(typespec):
        categorical = True
        categories = typespec.categories
        if pd.api.types.is_object_dtype(categories.dtype):
            category_types = parse_example_vector(
                categories.to_numpy(),
                sparse=sparse,
                categorical=True
            )
            if len(category_types) == 1:
                return category_types.pop()
            return category_types
        else:
            typespec = categories.dtype

    # pandas extension type
    if pd.api.types.is_extension_array_dtype(typespec):
        if pd.api.types.is_string_dtype(typespec):
            return StringType.instance(
                sparse=sparse,
                categorical=categorical,
                storage = typespec.storage
            )
        else:
            return dtype_lookup[typespec.numpy_dtype].instance(
                sparse=sparse,
                categorical=categorical,
                nullable=True
            )

    # numpy datetime64 special case
    if np.issubdtype(typespec, "M8"):
        unit, step_size = np.datetime_data(typespec)
        return NumpyDatetime64Type.instance(
            sparse=sparse,
            categorical=categorical,
            unit=unit,
            step_size=step_size
        )

    # numpy timedelta64 special case
    if np.issubdtype(typespec, "m8"):
        unit, step_size = np.datetime_data(typespec)
        return NumpyTimedelta64Type.instance(
            sparse=sparse,
            categorical=categorical,
            unit=unit,
            step_size=step_size
        )

    # consult dtype_lookup table
    return dtype_lookup[typespec].instance(
        sparse=sparse,
        categorical=categorical
    )


#########################
####    CONSTANTS    ####
#########################


cdef dict dtype_lookup = {
    # boolean
    np.dtype(bool): BooleanType,

    # integer
    np.dtype(np.int8): Int8Type,
    np.dtype(np.int16): Int16Type,
    np.dtype(np.int32): Int32Type,
    np.dtype(np.int64): Int64Type,
    np.dtype(np.uint8): UInt8Type,
    np.dtype(np.uint16): UInt16Type,
    np.dtype(np.uint32): UInt32Type,
    np.dtype(np.uint64): UInt64Type,

    # float
    np.dtype(np.float16): Float16Type,
    np.dtype(np.float32): Float32Type,
    np.dtype(np.float64): Float64Type,
    np.dtype(np.longdouble): LongDoubleType,

    # complex
    np.dtype(np.complex64): Complex64Type,
    np.dtype(np.complex128): Complex128Type,
    np.dtype(np.clongdouble): CLongDoubleType,

    # datetime (M8) dtype handled in special case

    # timedelta (m8) dtype handled in special case

    # string
    np.dtype(str): StringType,

    # object
    np.dtype(object): ObjectType
}
