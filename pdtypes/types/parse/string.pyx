"""Implement a PEG parser to convert string-based type specifiers into
ElementType objects.
"""
cimport numpy as np
from pyparsing import *

from pdtypes.types cimport *


# TODO: "object"/"obj"/"O"/"pyobject"/"object_"/"object0"



# TODO: unify this with types package.  Parsing functions go under separate
# subpackage pdtypes.types.parse
# pdtypes.types.parse.array.parse_array
# pdtypes.types.parse.dtype.parse_dtype
# pdtypes.types.parse.scalar.parse_scalar
# pdtypes.types.parse.string.parse_string
# pdtypes.types.parse.type.parse_type

# pdtypes.types.core then implements an
# element_type(typespec: Any, parse: bool = True) function, which
# serves as a universal flyweight constructor.  Its behavior depends on the
# input type.
# - isinstance(typespec, type) -> parse_type
# - isinstance(typespec, (np.dtype, pd.extensions.ExtensionDtype)) -> parse_dtype
# - parse and isinstance(typespec, str) -> parse_string
# - isinstance(typespec, (np.array, pd.Series)) -> parse_array
# - else -> parse_scalar




# TODO: parse_all doesn't apply to contents of nested expressions


######################
####    PUBLIC    ####
######################


cdef dict parse_string(str typespec):
    """Parse a string-based type specifier into an equivalent **kwargs dict,
    for use in ElementType construction.
    """
    cdef object components
    cdef object element
    cdef dict result

    components = type_specifier.parse_string(typespec, parse_all = True)
    result = {}
    for element in flatten(components)[::-1]:
        result = {**result, **element}
    return result


###########################
####    EXPRESSIONS    ####
###########################


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


cdef dict lookup(object tokens, str category):
    """Parse action mapping a base type specifier keyword to its equivalent
    **kwargs dict, as used in ElementType construction.
    """
    cdef object kwargs

    kwargs = keywords[category][tokens[0]]
    while isinstance(kwargs, str):  # resolve synonyms
        kwargs = keywords[category][kwargs]
    return kwargs


cdef dict keywords = {
    "boolean": {
        # boolean supertype
        "boolean": "bool",
        "bool_": "bool",
        "bool8": "bool",
        "b1": "bool",
        "bool": {"base": BooleanType, "nullable": False},
        "?": "bool",
        "Boolean": {"base": BooleanType, "nullable": True}
    },
    "integer": {
        # uint8 (unsigned)
        "uint8": {"base": UInt8Type, "nullable": False},
        "u1": "uint8",
        "ubyte": "uint8",
        "unsigned char": np.dtype("ubyte").name,  # platform-specific
        "UInt8": {"base": UInt8Type, "nullable": True},  # pandas extension
        "B": "uint8",

        # uint16 (unsigned)
        "uint16": {"base": UInt16Type, "nullable": False},
        "u2": "uint16",
        "ushort": np.dtype("ushort").name,  # platform-specific
        "unsigned short int": np.dtype("ushort").name,  # platform-specific
        "unsigned short": np.dtype("ushort").name,  # platform-specific
        "UInt16": {"base": UInt16Type, "nullable": True},  # pandas extension
        "H": "uint16",

        # uint32 (unsigned)
        "uint32": {"base": UInt32Type, "nullable": False},
        "u4": "uint32",
        "uintc": np.dtype("uintc").name,  # platform-specific
        "unsigned intc": np.dtype("uintc").name,  # platform-specific
        "UInt32": {"base": UInt32Type, "nullable": True},  # pandas extension
        "I": "uint32",

        # uint64 (unsigned)
        "uint64": {"base": UInt64Type, "nullable": False},
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

        # unsigned integer supertype
        "uint": {"base": UnsignedIntegerType, "nullable": False},
        "unsigned integer": "uint",
        "unsigned int": "uint",
        "unsigned": "uint",
        "u": "uint",

        # int8 (signed)
        "int8": {"base": Int8Type, "nullable": False},
        "i1": "int8",
        "byte": "int8",
        "signed char": np.dtype("byte").name,  # platform-specific
        "char": np.dtype("byte").name,  # platform-specific
        "Int8": {"base": Int8Type, "nullable": True},  # pandas extension
        "b": "int8",

        # int16 (signed)
        "int16": {"base": Int16Type, "nullable": False},
        "i2": "int16",
        "signed short int": np.dtype("short").name,  # platform-specific
        "signed short": np.dtype("short").name,  # platform-specific
        "short int": np.dtype("short").name,  # platform-specific
        "short": np.dtype("short").name,  # platform-specific
        "Int16": {"base": Int16Type, "nullable": True},  # pandas extension
        "h": "int16",

        # int32 (signed)
        "int32": {"base": Int32Type, "nullable": False},
        "i4": "int32",
        "signed intc": np.dtype("intc").name,  # platform-specific
        "intc": np.dtype("intc").name,  # platform-specific
        "Int32": {"base": Int32Type, "nullable": True},  # pandas extension

        # int64 (signed)
        "int64": {"base": Int64Type, "nullable": False},
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

        # signed integer supertype
        "signed integer": {"base": SignedIntegerType, "nullable": True},
        "signed int": "signed integer",
        "signed": "signed integer",
        "i": "signed integer",

        # integer supertype
        "int": {"base": IntegerType, "nullable": True},
        "integer": "int"
    },
    "float": {
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

        # float supertype
        "float": {"base": FloatType},
        "floating": "float",
        "f": "float"
    },
    "complex": {
        # complex64
        "complex64": {"base": Complex64Type},
        "c8": "complex64",
        "csingle": "complex64",
        "complex single": "complex64",
        "singlecomplex": "complex64",
        "F": "complex64",

        # complex128
        "complex128": {"base": Complex128Type},
        "c16": "complex64",
        "complex_": "complex64",
        "cdouble": "complex64",
        "complex double": "complex64",
        "D": "complex64",

        # clongdouble
        "complex256": {"base": CLongDoubleType},
        "complex192": {"base": CLongDoubleType},
        "clongdouble": np.dtype("clongdouble").name,
        "complex longdouble": np.dtype("clongdouble").name,
        "complex long double": np.dtype("clongdouble").name,
        "clongfloat": np.dtype("clongdouble").name,
        "complex longfloat": np.dtype("clongdouble").name,
        "complex long float": np.dtype("clongdouble").name,
        "longcomplex": np.dtype("clongdouble").name,
        "long complex": np.dtype("clongdouble").name,
        "G": np.dtype("clongdouble").name,

        # complex supertype
        "cfloat": "complex",
        "complex float": "complex",
        "complex floating": "complex",
        "complex": {"base": ComplexType},
        "c": "complex"
    },
    "decimal": {
        "decimal": {"base": DecimalType},
        "arbitrary precision": "decimal"
    },
    "datetime": {
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
        # datetime64/M8 (with or without units) handled in special case

        # datetime supertype
        "datetime": {"base": DatetimeType}
    },
    "timedelta": {
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
        # timedelta64/m8 (with or without units) handled in special case

        # timedelta supertype
        "timedelta": {"base": TimedeltaType}
    },
    "string": {
        # python backend
        "string[python]": {"base": StringType, "storage": "python"},
        "str[python]": "string[python]",
        "pystring": "string[python]",
        "python string": "string[python]",

        # pyarrow backend
        "string[pyarrow]": {"base": StringType, "storage": "pyarrow"},
        "str[pyarrow]": "string[pyarrow]",
        "pyarrow string": "string[pyarrow]",

        # string supertype
        "string": {"base": StringType},
        "str": "string"
    }
}


# define expressions
boolean_type = OneOrMore(Or(Keyword(s) for s in keywords["boolean"]))
integer_type = OneOrMore(Or(Keyword(s) for s in keywords["integer"]))
float_type = OneOrMore(Or(Keyword(s) for s in keywords["float"]))
complex_type = OneOrMore(Or(Keyword(s) for s in keywords["complex"]))
decimal_type = OneOrMore(Or(Keyword(s) for s in keywords["decimal"]))
datetime_type_no_unit = OneOrMore(
    Or(Keyword(s) for s in keywords["datetime"])
)
timedelta_type_no_unit = OneOrMore(
    Or(Keyword(s) for s in keywords["timedelta"])
)
string_type = OneOrMore(Or(Keyword(s) for s in keywords["string"]))


# add parsing actions
boolean_type.set_parse_action(lambda tokens: lookup(tokens, "boolean"))
integer_type.set_parse_action(lambda tokens: lookup(tokens, "integer"))
float_type.set_parse_action(lambda tokens: lookup(tokens, "float"))
complex_type.set_parse_action(lambda tokens: lookup(tokens, "complex"))
decimal_type.set_parse_action(lambda tokens: lookup(tokens, "decimal"))
datetime_type_no_unit.set_parse_action(
    lambda tokens: lookup(tokens, "datetime")
)
timedelta_type_no_unit.set_parse_action(
    lambda tokens: lookup(tokens, "timedelta")
)
string_type.set_parse_action(lambda tokens: lookup(tokens, "string"))


# add special case for "datetime64"/"M8" with optional units, step size
datetime64_base = Keyword("datetime64") | Keyword("M8")
unit = Or(
    Literal(s) for s in ("ns", "us", "ms", "s", "m", "h", "D", "W", "M", "Y")
)
step_size = Word(nums)
datetime64_base.set_parse_action(lambda _: NumpyDatetime64Type)
step_size.set_parse_action(pyparsing_common.convert_to_integer)
datetime64_type_with_unit = (
    datetime64_base.set_results_name("base") +
    Opt(
        Suppress("[") +
        Opt(step_size.set_results_name("step_size")) +
        unit.set_results_name("unit") +
        Suppress("]"))
)
datetime64_type_with_unit.set_parse_action(dict)


# add special case for "timedelta64"/"m8" with optional units, step size
timedelta64_base = Keyword("timedelta64") | Keyword("m8")
unit = Or(
    Literal(s) for s in ("ns", "us", "ms", "s", "m", "h", "D", "W", "M", "Y")
)
step_size = Word(nums)
timedelta64_base.set_parse_action(lambda _: NumpyTimedelta64Type)
step_size.set_parse_action(pyparsing_common.convert_to_integer)
timedelta64_type_with_unit = (
    timedelta64_base.set_results_name("base") +
    Opt(
        Suppress("[") +
        Opt(step_size.set_results_name("step_size")) +
        unit.set_results_name("unit") +
        Suppress("]")
    )
)
timedelta64_type_with_unit.set_parse_action(dict)


# formalize special cases
datetime_type = datetime_type_no_unit | datetime64_type_with_unit
timedelta_type = timedelta_type_no_unit | timedelta64_type_with_unit


# define base type
base_type = Or(
    [
        boolean_type,
        integer_type,
        float_type,
        complex_type,
        decimal_type,
        datetime_type,
        timedelta_type,
        string_type
    ]
)


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
