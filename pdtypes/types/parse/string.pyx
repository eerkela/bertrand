"""Implement a PEG parser to convert string-based type specifiers into
ElementType objects.
"""
cimport numpy as np
from pyparsing import *

from pdtypes.types cimport (
    ElementType, BooleanType, IntegerType, SignedIntegerType, Int8Type,
    Int16Type, Int32Type, Int64Type, UnsignedIntegerType, UInt8Type,
    UInt16Type, UInt32Type, UInt64Type, FloatType, Float16Type, Float32Type,
    Float64Type, LongDoubleType, ComplexType, Complex64Type, Complex128Type,
    CLongDoubleType, DecimalType, DatetimeType, PandasTimestampType,
    PyDatetimeType, NumpyDatetime64Type, TimedeltaType, PandasTimedeltaType,
    PyTimedeltaType, NumpyTimedelta64Type, StringType, ObjectType
)


# TODO: "string[...]" + "datetime/timedelta[...]" are broken

# TODO: "datetime" resolves to "sparse[datetime]"


######################
####    PUBLIC    ####
######################


cdef ElementType parse_typespec_string(str typespec):
    """Parse a string-based type specifier into an equivalent **kwargs dict,
    for use in ElementType construction.
    """
    cdef object components
    cdef object element
    cdef dict kwargs

    try:
        components = type_specifier.parse_string(typespec, parse_all = True)
        kwargs = {}
        for element in flatten(components)[::-1]:
            kwargs = {**kwargs, **element}
        return kwargs.pop("base").instance(**kwargs)
    except (TypeError, AttributeError, ParseException) as err:
        raise ValueError(f"invalid type string: {repr(typespec)}") from err


#######################
####    HELPERS    ####
#######################


cdef object flatten(object nested):
    """Recursively flatten a nested sequence of lists or list-like
    `pyparsing.ParseResults` objects into an equivalent object with depth 1.
    """
    # base case
    if not nested:  # empty list
        return nested

    # recursive case
    if isinstance(nested[0], (list, ParseResults)):
        return flatten(nested[0]) + flatten(nested[1:])
    return nested[:1] + flatten(nested[1:])


###########################
####    EXPRESSIONS    ####
###########################


cdef dict lookup(
    str original_string,
    unsigned int location,
    object tokens
):
    """Get the **kwargs dict that matches the parsed token, following synonyms.
    """
    cdef object kwargs

    kwargs = keywords[tokens[0]]
    while isinstance(kwargs, str):  # resolve synonyms
        kwargs = keywords[kwargs]
    return kwargs


cdef void ensure_single(
    str original_string,
    unsigned int location,
    object tokens
):
    """Reject pyparsing matches that contain more than one token."""
    if len(tokens) != 1:
        raise ParseException(
            original_string,
            location,
            "string must contain only a single base type"
        )


cdef dict keywords = {
    # boolean supertype
    "boolean": "bool",
    "bool_": "bool",
    "bool8": "bool",
    "b1": "bool",
    "bool": {"base": BooleanType},
    "?": "bool",
    "Boolean": {"base": BooleanType, "nullable": True},

    # integer supertype
    "int": {"base": IntegerType},
    "integer": "int",

    # signed integer supertype
    "signed integer": {"base": SignedIntegerType},
    "signed int": "signed integer",
    "signed": "signed integer",
    "i": "signed integer",

    # unsigned integer supertype
    "uint": {"base": UnsignedIntegerType},
    "unsigned integer": "uint",
    "unsigned int": "uint",
    "unsigned": "uint",
    "u": "uint",

    # int8 (signed)
    "int8": {"base": Int8Type},
    "i1": "int8",
    "byte": "int8",
    "signed char": np.dtype("byte").name,  # platform-specific
    "char": np.dtype("byte").name,  # platform-specific
    "Int8": {"base": Int8Type, "nullable": True},  # pandas extension
    "b": "int8",

    # int16 (signed)
    "int16": {"base": Int16Type},
    "i2": "int16",
    "signed short int": np.dtype("short").name,  # platform-specific
    "signed short": np.dtype("short").name,  # platform-specific
    "short int": np.dtype("short").name,  # platform-specific
    "short": np.dtype("short").name,  # platform-specific
    "Int16": {"base": Int16Type, "nullable": True},  # pandas extension
    "h": "int16",

    # int32 (signed)
    "int32": {"base": Int32Type},
    "i4": "int32",
    "cint": np.dtype("intc").name,  # platform-specific
    "signed cint": np.dtype("intc").name,  # platform-specific
    "signed intc": np.dtype("intc").name,  # platform-specific
    "intc": np.dtype("intc").name,  # platform-specific
    "Int32": {"base": Int32Type, "nullable": True},  # pandas extension

    # int64 (signed)
    "int64": {"base": Int64Type},
    "i8": "int64",
    "intp": "int64",
    "int0": "int64",
    "signed long long int": np.dtype("longlong").name,  # platform-specific
    "signed long long": np.dtype("longlong").name,  # platform-specific
    "signed long int": np.dtype("int_").name,  # platform-specific
    "signed long": np.dtype("int_").name,  # platform-specific
    "long long int": np.dtype("longlong").name,  # platform-specific
    "long long": np.dtype("longlong").name,  # platform-specific
    "long int": np.dtype("int_").name,  # platform-specific
    "long": np.dtype("int_").name,  # platform-specific
    "l": np.dtype("int_").name,
    "Int64": {"base": Int64Type, "nullable": True},  # pandas extension
    "p": "int64",

    # uint8 (unsigned)
    "uint8": {"base": UInt8Type},
    "u1": "uint8",
    "ubyte": "uint8",
    "unsigned char": np.dtype("ubyte").name,  # platform-specific
    "UInt8": {"base": UInt8Type, "nullable": True},  # pandas extension
    "B": "uint8",

    # uint16 (unsigned)
    "uint16": {"base": UInt16Type},
    "u2": "uint16",
    "ushort": np.dtype("ushort").name,  # platform-specific
    "unsigned short int": np.dtype("ushort").name,  # platform-specific
    "unsigned short": np.dtype("ushort").name,  # platform-specific
    "UInt16": {"base": UInt16Type, "nullable": True},  # pandas extension
    "H": "uint16",

    # uint32 (unsigned)
    "uint32": {"base": UInt32Type},
    "u4": "uint32",
    "ucint": np.dtype("uintc").name,  # platform-specific
    "unsigned cint": np.dtype("uintc").name,  # platform-specific
    "uintc": np.dtype("uintc").name,  # platform-specific
    "unsigned intc": np.dtype("uintc").name,  # platform-specific
    "UInt32": {"base": UInt32Type, "nullable": True},  # pandas extension
    "I": "uint32",

    # uint64 (unsigned)
    "uint64": {"base": UInt64Type},
    "u8": "uint64",
    "uintp": "uint64",
    "uint0": "uint64",
    "unsigned long long int":
        np.dtype("ulonglong").name,  # platform-specific
    "unsigned long long": np.dtype("ulonglong").name,  # platform-specific
    "unsigned long int": np.dtype("uint").name,  # platform-specific
    "unsigned long": np.dtype("uint").name,  # platform-specific
    "L": np.dtype("uint").name,  # platform-specific
    "UInt64": {"base": UInt64Type, "nullable": True},  # pandas extension
    "P": "uint64",

    # float supertype
    "float": {"base": FloatType},
    "floating": "float",
    "f": "float",

    # float16
    "float16": {"base": Float16Type},
    "f2": "float16",
    "half": "float16",  # IEEE 754 half-precision float
    "e": "float16",

    # float32
    "float32": {"base": Float32Type},
    "f4": "float32",
    "single": "float32",  # IEEE 754 single-precision float

    # float64
    "float64": {"base": Float64Type},
    "f8": "float64",
    "float_": "float64",
    "double": "float64",  # IEEE 754 double-precision float
    "d": "float64",

    # longdouble - x86 extended precision format (platform-specific)
    "float128": {"base": LongDoubleType},
    "float96": {"base": LongDoubleType},
    "f16": "float128",
    "f12": "float128",
    "longdouble": np.dtype("longdouble").name,
    "longfloat": np.dtype("longfloat").name,
    "long double": np.dtype("longdouble").name,
    "long float": np.dtype("longfloat").name,
    "g": np.dtype("longdouble").name,

    # complex supertype
    "cfloat": "complex",
    "complex float": "complex",
    "complex floating": "complex",
    "complex": {"base": ComplexType},
    "c": "complex",

    # complex64
    "complex64": {"base": Complex64Type},
    "c8": "complex64",
    "csingle": "complex64",
    "complex single": "complex64",
    "singlecomplex": "complex64",
    "F": "complex64",

    # complex128
    "complex128": {"base": Complex128Type},
    "c16": "complex128",
    "complex_": "complex128",
    "cdouble": "complex128",
    "complex double": "complex128",
    "D": "complex128",

    # clongdouble
    "complex256": {"base": CLongDoubleType},
    "complex192": {"base": CLongDoubleType},
    "c32": "complex256",
    "c24": "complex192",
    "clongdouble": np.dtype("clongdouble").name,
    "complex longdouble": np.dtype("clongdouble").name,
    "complex long double": np.dtype("clongdouble").name,
    "clongfloat": np.dtype("clongdouble").name,
    "complex longfloat": np.dtype("clongdouble").name,
    "complex long float": np.dtype("clongdouble").name,
    "longcomplex": np.dtype("clongdouble").name,
    "long complex": np.dtype("clongdouble").name,
    "G": np.dtype("clongdouble").name,

    # decimal supertype
    "decimal": {"base": DecimalType},
    "arbitrary precision": "decimal",

    # datetime supertype
    "datetime": {"base": DatetimeType},

    # pandas.Timestamp
    "datetime[pandas]": {"base": PandasTimestampType},
    "pandas.Timestamp": "datetime[pandas]",
    "pandas Timestamp": "datetime[pandas]",
    "pd.Timestamp": "datetime[pandas]",

    # datetime.datetime
    "datetime[python]": {"base": PyDatetimeType},
    "pydatetime": "datetime[python]",
    "datetime.datetime": "datetime[python]",

    # numpy.datetime64
    "datetime[numpy]": {"base": NumpyDatetime64Type},
    "numpy.datetime64": "datetime[numpy]",
    "numpy datetime64": "datetime[numpy]",
    "np.datetime64": "datetime[numpy]",
    # "datetime64"/"M8" (with or without units) handled in special case

    # timedelta supertype
    "timedelta": {"base": TimedeltaType},

    # pandas.Timedelta
    "timedelta[pandas]": {"base": PandasTimestampType},
    "pandas.Timedelta": "timedelta[pandas]",
    "pandas Timedelta": "timedelta[pandas]",
    "pd.Timedelta": "timedelta[pandas]",

    # datetime.timedelta
    "timedelta[python]": {"base": PyTimedeltaType},
    "pytimedelta": "timedelta[python]",
    "datetime.timedelta": "timedelta[python]",

    # numpy.timedelta64
    "timedelta[numpy]": {"base": NumpyTimedelta64Type},
    "numpy.timedelta64": "timedelta[numpy]",
    "numpy timedelta64": "timedelta[numpy]",
    "np.timedelta64": "timedelta[numpy]",
    # "timedelta64"/"m8" (with or without units) handled in special case

    # string supertype
    "string": {"base": StringType},
    "str": "string",
    "unicode": "string",
    "U": "string",
    "str0": "string",
    "str_": "string",
    "unicode_": "string",

    # python-backed string extension type
    "string[python]": {"base": StringType, "storage": "python"},
    "str[python]": "string[python]",
    "unicode[python]": "string[python]",
    "pystring": "string[python]",
    "python string": "string[python]",

    # pyarrow-backed string extension type
    "string[pyarrow]": {"base": StringType, "storage": "pyarrow"},
    "str[pyarrow]": "string[pyarrow]",
    "unicode[pyarrow]": "string[pyarrow]",
    "pyarrow string": "string[pyarrow]",

    "object": {"base": ObjectType},
    "obj": "object",
    "O": "object",
    "pyobject": "object",
    "object_": "object",
    "object0": "object"
}


cdef object build_parse_expression():
    """Build a pyparsing expression to recognize arbitrary string-based type
    specifiers.
    """
    # "datetime64"/"M8", "timedelta64"/"m8" with optional units, step size
    M8_base = Keyword("datetime64") | Keyword("M8")
    m8_base = Keyword("timedelta64") | Keyword("m8")
    M8_base.set_parse_action(lambda _: NumpyDatetime64Type)
    m8_base.set_parse_action(lambda _: NumpyTimedelta64Type)
    unit = MatchFirst(
        Literal(s) for s in ("ns", "us", "ms", "s", "m", "h", "D", "W", "M",
                             "Y", "generic")
    )
    step_size = Word(nums)
    step_size.set_parse_action(pyparsing_common.convert_to_integer)
    M8_with_unit = (
        M8_base.set_results_name("base") +
        Opt(
            Suppress("[") +
            Opt(step_size.set_results_name("step_size")) +
            unit.set_results_name("unit") +
            Suppress("]"))
    )
    M8_with_unit.set_parse_action(dict)  # uses named groups
    m8_with_unit = (
        m8_base.set_results_name("base") +
        Opt(
            Suppress("[") +
            Opt(step_size.set_results_name("step_size")) +
            unit.set_results_name("unit") +
            Suppress("]")
        )
    )
    m8_with_unit.set_parse_action(dict)  # uses named groups

    # base type specifier
    base_type = one_of(keywords).set_parse_action(lookup)

    # combine with M8/m8 special cases
    base_type = OneOrMore(base_type ^ M8_with_unit ^ m8_with_unit)
    base_type.set_parse_action(ensure_single)  # enforce single match

    # add nested sparse/categorical/nullable directives
    sparse = CaselessKeyword("sparse")
    categorical = CaselessKeyword("categorical")
    nullable = CaselessKeyword("nullable")
    sparse.set_parse_action(lambda _: {"sparse": True})
    categorical.set_parse_action(lambda _: {"categorical": True})
    nullable.set_parse_action(lambda _: {"nullable": True})
    directive = sparse | categorical | nullable

    # define recursive type specifier
    type_specifier = Forward()
    type_specifier <<= (
        (directive + nested_expr("[", "]", type_specifier)) | base_type
    )

    return type_specifier


# build pyparsing expression
type_specifier = build_parse_expression()
