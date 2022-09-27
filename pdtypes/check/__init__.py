from .core import check_dtype, get_dtype, is_dtype
from .extensions import extension_type
from .introspect import object_types
from .resolve import resolve_dtype
from .supertypes import subtypes, supertype


# TODO: allow resolve_dtype to handle extension types/supertypes, M8/m8 with
# various units, etc.
# -> create a class hierarchy
# base classes:
# - ElementType
# supertypes (inherit from ElementType):
# - bool
# - int
# - float
# - complex
# - "decimal"
# - "datetime"
# - "timedelta"
# - str
# atomic types (inherit from a single supertype):
# - bool
# - uint8
# - uint16
# - uint32
# - uint64
# - int8
# - int16
# - int32
# - int64
# - float16
# - float32
# - float64
# - longdouble
# - complex64
# - complex128
# - clongdouble
# - decimal.Decimal
# - pd.Timestamp
# - datetime.datetime
# - np.datetime64
# - pd.Timedelta
# - datetime.timedelta
# - np.timedelta64
# - str
# - object

# These should go into a pdtypes.types subpackage, and define custom comparison
# behavior for each one.
# pdtype(np.datetime64, "ns", 1) != pdtype(np.datetime64, "s", 1)


# TODO: when dispatching, use sparse and categorical boolean flags.  If the
# provided dtype is SparseDtype(), set sparse=True and convert to base type.
# If dtype is pd.CategoricalDtype() with or without `categories`, `ordered`,
# then set categorical=True and convert to base type.  If none is provided,
# use current element type.
# -> ElementType can have sparse and categorical boolean flags
