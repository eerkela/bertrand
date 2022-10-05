import datetime as pydatetime
import decimal as pydecimal

cimport numpy as np
import numpy as np
import pandas as pd

from pdtypes.types cimport *


# TODO: object type


######################
####    PUBLIC    ####
######################


cdef dict parse_type(type typespec):
    """Parse an atomic type specifier into an equivalent **kwargs dict for use
    in ElementType construction.
    """
    if issubclass(typespec, ElementType):
        raise NotImplementedError(f"NYI: direct ElementType recognition.")
    # return lookup.get(typespec, {"base": ObjectType})
    return lookup[typespec].copy()


#########################
####    CONSTANTS    ####
#########################


cdef dict lookup = {
    # boolean
    bool: {"base": BooleanType, "nullable": False},
    np.bool_: {"base": BooleanType, "nullable": False},

    # integer
    int: {"base": IntegerType, "nullable": True},
    np.signedinteger: {"base": SignedIntegerType, "nullable": False},
    np.unsignedinteger: {"base": UnsignedIntegerType, "nullable": False},
    np.int8: {"base": Int8Type, "nullable": False},
    np.int16: {"base": Int16Type, "nullable": False},
    np.int32: {"base": Int32Type, "nullable": False},
    np.int64: {"base": Int64Type, "nullable": False},
    np.uint8: {"base": UInt8Type, "nullable": False},
    np.uint16: {"base": UInt16Type, "nullable": False},
    np.uint32: {"base": UInt32Type, "nullable": False},
    np.uint64: {"base": UInt64Type, "nullable": False},

    # float
    float: {"base": FloatType},
    np.float16: {"base": Float16Type},
    np.float32: {"base": Float32Type},
    np.float64: {"base": Float64Type},
    np.longdouble: {"base": LongDoubleType},

    # complex
    complex: {"base": ComplexType},
    np.complex64: {"base": Complex64Type},
    np.complex128: {"base": Complex128Type},
    np.clongdouble: {"base": CLongDoubleType},

    # decimal
    pydecimal.Decimal: {"base": DecimalType},

    # datetime
    pd.Timestamp: {"base": PandasTimestampType},
    pydatetime.datetime: {"base": PyDatetimeType},
    np.datetime64: {"base": NumpyDatetime64Type},

    # timedelta
    pd.Timedelta: {"base": PandasTimedeltaType},
    pydatetime.timedelta: {"base": PyTimedeltaType},
    np.timedelta64: {"base": NumpyTimedelta64Type},

    # string
    str: {"base": StringType},
    np.str_: {"base": StringType}
}
