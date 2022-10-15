import datetime as pydatetime
import decimal as pydecimal

cimport numpy as np
import numpy as np
import pandas as pd

from pdtypes.types cimport (
    ElementType, BooleanType, IntegerType, SignedIntegerType, Int8Type,
    Int16Type, Int32Type, Int64Type, UnsignedIntegerType, UInt8Type,
    UInt16Type, UInt32Type, UInt64Type, FloatType, Float16Type, Float32Type,
    Float64Type, LongDoubleType, ComplexType, Complex64Type, Complex128Type,
    CLongDoubleType, DecimalType, PandasTimestampType, PyDatetimeType,
    NumpyDatetime64Type, PandasTimedeltaType, PyTimedeltaType,
    NumpyTimedelta64Type, StringType, ObjectType
)


######################
####    PUBLIC    ####
######################


cdef ElementType parse_typespec_type(
    type typespec,
    bint sparse = False,
    bint categorical = False,
    bint force_nullable = False
):
    """TODO"""
    # comply with force_nullable
    if force_nullable and issubclass(typespec, non_nullable_types):
        return type_lookup[typespec].instance(
            sparse=sparse,
            categorical=categorical,
            nullable=True
        )

    return type_lookup.get(typespec, ObjectType).instance(
        sparse=sparse,
        categorical=categorical
    )


#########################
####    CONSTANTS    ####
#########################


cdef tuple non_nullable_types = (np.integer, np.bool_, bool)


cdef dict type_lookup = {
    # boolean
    bool: BooleanType,
    np.bool_: BooleanType,

    # integer
    int: IntegerType,
    np.integer: IntegerType,
    np.signedinteger: SignedIntegerType,
    np.unsignedinteger: UnsignedIntegerType,
    np.int8: Int8Type,
    np.int16: Int16Type,
    np.int32: Int32Type,
    np.int64: Int64Type,
    np.uint8: UInt8Type,
    np.uint16: UInt16Type,
    np.uint32: UInt32Type,
    np.uint64: UInt64Type,

    # float
    float: FloatType,
    np.float16: Float16Type,
    np.float32: Float32Type,
    np.float64: Float64Type,
    np.longdouble: LongDoubleType,

    # complex
    complex: ComplexType,
    np.complex64: Complex64Type,
    np.complex128: Complex128Type,
    np.clongdouble: CLongDoubleType,

    # decimal
    pydecimal.Decimal: DecimalType,

    # datetime
    pd.Timestamp: PandasTimestampType,
    pydatetime.datetime: PyDatetimeType,
    np.datetime64: NumpyDatetime64Type,

    # timedelta
    pd.Timedelta: PandasTimedeltaType,
    pydatetime.timedelta: PyTimedeltaType,
    np.timedelta64: NumpyTimedelta64Type,

    # string
    str: StringType,
    np.str_: StringType
}
