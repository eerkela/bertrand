from .patch import attach, detach, DispatchMethod, Namespace

from .check import typecheck
from .convert import (
    cast, to_boolean, to_integer, to_float, to_complex, to_decimal,
    to_datetime, to_timedelta, to_string, to_object, SeriesWrapper
)
from .detect import detect_type
from .resolve import resolve_type
from .types import *
from .util.structs import (
    extension_func, no_default, ExtensionFunc, ExtensionMethod
)


# importing * from types also masks module names, which can be troublesome
del base
del boolean
del categorical
del complex
del datetime
del decimal
del float
del integer
del object
del sparse
del string
del timedelta
