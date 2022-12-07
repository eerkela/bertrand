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
    # bool
    "bool": {"base": BooleanType},
    "boolean": "bool",
    "bool_": "bool",
    "bool8": "bool",
    "b1": "bool",
    "?": "bool",
    "Boolean": {"base": BooleanType, "nullable": True},

    # int
    "int": {"base": IntegerType},
    "integer": "int",

    # signed int
    "signed": {"base": SignedIntegerType},
    "signed integer": "signed",
    "signed int": "signed",
    "i": "signed",

    # unsigned integer supertype
    "unsigned": {"base": UnsignedIntegerType},
    "unsigned integer": "unsigned",
    "unsigned int": "unsigned",
    "uint": "unsigned",
    "u": "unsigned",

    # int8
    "int8": {"base": Int8Type},
    "i1": "int8",
    "Int8": {"base": Int8Type, "nullable": True},  # pandas extension

    # int16
    "int16": {"base": Int16Type},
    "i2": "int16",
    "Int16": {"base": Int16Type, "nullable": True},  # pandas extension

    # int32
    "int32": {"base": Int32Type},
    "i4": "int32",
    "Int32": {"base": Int32Type, "nullable": True},  # pandas extension

    # int64
    "int64": {"base": Int64Type},
    "i8": "int64",
    "Int64": {"base": Int64Type, "nullable": True},  # pandas extension

    # uint8
    "uint8": {"base": UInt8Type},
    "u1": "uint8",
    "UInt8": {"base": UInt8Type, "nullable": True},  # pandas extension

    # uint16
    "uint16": {"base": UInt16Type},
    "u2": "uint16",
    "UInt16": {"base": UInt16Type, "nullable": True},  # pandas extension

    # uint32
    "uint32": {"base": UInt32Type},
    "u4": "uint32",
    "UInt32": {"base": UInt32Type, "nullable": True},  # pandas extension

    # uint64
    "uint64": {"base": UInt64Type},
    "u8": "uint64",
    "UInt64": {"base": UInt64Type, "nullable": True},  # pandas extension

    # char = C char (platform specific)
    "char": np.dtype("byte").name,
    "signed char": "char",
    "byte": "char",
    "b": "char",

    # short = C short (platform specific)
    "short": np.dtype("short").name,
    "signed short int": "short",
    "signed short": "short",
    "short int": "short",
    "h": "short",

    # intc = C int (platform specific)
    "intc": np.dtype("intc").name,
    "signed intc": "intc",

    # long = C long (platform specific)
    "long": np.dtype("int_").name,
    "long int": "long",
    "signed long": "long",
    "signed long int": "long",
    "l": "long",

    # long long = C long long (platform specific)
    "long long": np.dtype("longlong").name,
    "longlong": "long long",
    "long long int": "long long",
    "signed long long": "long long",
    "signed longlong": "long long",
    "signed long long int": "long long",
    "q": "long long",

    # ssize_t = C ssize_t (platform specific)
    "ssize_t": np.dtype("intp").name,
    "intp": "ssize_t",
    "int0": "ssize_t",
    "p": "ssize_t",

    # unsigned char = C unsigned char (platform specific)
    "unsigned char": np.dtype("ubyte").name,
    "unsigned byte": "unsigned char",
    "ubyte": "unsigned char",
    "B": "unsigned char",

    # unsigned short = C unsigned short (platform specific)
    "unsigned short": np.dtype("ushort").name,
    "unsigned short int": "unsigned short",
    "ushort": "unsigned short",
    "H": "unsigned short",

    # unsigned intc = C unsigned int (platform specific)
    "unsigned intc": np.dtype("uintc").name,
    "uintc": "unsigned intc",
    "I": "unsigned intc",

    # unsigned long = C unsigned long (platform specific)
    "unsigned long": np.dtype("uint").name,
    "unsigned long int": "unsigned long",
    "ulong": "unsigned long",
    "L": "unsigned long",

    # unsigned long long = C unsigned long long (platform specific)
    "unsigned long long": np.dtype("ulonglong").name,
    "unsigned longlong": "unsigned long long",
    "unsigned long long int": "unsigned long long",
    "ulonglong": "unsigned long long",
    "Q": "unsigned long long",

    # size_t = C size_t (platform specific)
    "size_t": np.dtype("uintp").name,
    "uintp": "size_t",
    "uint0": "size_t",
    "P": "size_t",

    # float
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
    "f12": "float96",
    "longdouble": np.dtype("longdouble").name,
    "longfloat": np.dtype("longfloat").name,
    "long double": np.dtype("longdouble").name,
    "long float": np.dtype("longfloat").name,
    "g": np.dtype("longdouble").name,

    # complex
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

    # decimal
    "decimal": {"base": DecimalType},
    "arbitrary precision": "decimal",

    # datetime
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

    # timedelta
    "timedelta": {"base": TimedeltaType},

    # pandas.Timedelta
    "timedelta[pandas]": {"base": PandasTimedeltaType},
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

    # string
    "string": {"base": StringType},
    "str": "string",
    "unicode": "string",
    "U": "string",
    "str0": "string",
    "str_": "string",
    "unicode_": "string",

    # string[python]
    "string[python]": {"base": StringType, "storage": "python"},
    "str[python]": "string[python]",
    "unicode[python]": "string[python]",
    "pystr": "string[python]",
    "pystring": "string[python]",
    "python string": "string[python]",

    # string[pyarrow]
    "string[pyarrow]": {"base": StringType, "storage": "pyarrow"},
    "str[pyarrow]": "string[pyarrow]",
    "unicode[pyarrow]": "string[pyarrow]",
    "pyarrow str": "string[pyarrow]",
    "pyarrow string": "string[pyarrow]",

    # object
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
