"""Dynamic resolution of AtomicType/CompositeType specifiers.

Exports only a single function `resolve_type()`, which can parse type
specifiers of any of the following types:
    * numpy/pandas dtype objects.
    * raw type definitions (e.g. `int`, `float`, etc.).
    * strings that conform to the type resolution mini-language.
    * other AtomicType/CompositeType objects.

Custom aliases can be added or removed from the pool recognized by this
function through the `register_alias()`, `remove_alias()`, and
`clear_aliases()` classmethods attached to each respective AtomicType
definition.
"""
import regex as re  # alternate (PCRE-style) python regex engine
from typing import Iterable

cimport numpy as np
import numpy as np
import pandas as pd

import pdtypes.types as types
cimport pdtypes.types as types

from pdtypes.util.type_hints import type_specifier


#####################
####   PUBLIC    ####
#####################


def resolve_type(
    typespec: type_specifier | Iterable[type_specifier]
) -> types.BaseType:
    """Interpret a type specifier, returning a corresponding type object.

    .. note: the instances returned by this function are *flyweights*, meaning
    that repeated input will simply yield a reference to the first object
    created by this function, rather than distinct copies.  Practically,
    this means that only one instance of each type will ever exist at one time,
    and any changes made to that object will propagate wherever it is
    referenced.  Such objects are designed to be immutable for this reason,
    but the behavior should be noted nonetheless.
    """
    if isinstance(typespec, types.BaseType):
        result = typespec
    elif isinstance(typespec, str):
        result = resolve_typespec_string(typespec)
    elif isinstance(typespec, type):
        result = resolve_typespec_type(typespec)
    elif isinstance(typespec, (np.dtype, pd.api.extensions.ExtensionDtype)):
        result = resolve_typespec_dtype(typespec)
    elif hasattr(typespec, "__iter__"):
        result = types.CompositeType(resolve_type(x) for x in typespec)
    else:
        raise ValueError(
            f"could not resolve specifier of type {type(typespec)}"
        )
    return result


#######################
####    PRIVATE    ####
#######################


cdef dict na_strings = {
    str(pd.NA): pd.NA,
    str(pd.NaT): pd.NaT,
    str(np.nan): np.nan,
}


cdef str nested(str opener, str closer, str name):
    """Produce a regex pattern to match nested sequences with the specified
    opening and closing characters.  Relies on PCRE-style recursive
    expressions, which are enabled by the alternate python `regex` package.
    """
    opener = re.escape(opener)
    closer = re.escape(closer)
    body = rf"(?P<body>([^{opener}{closer}]|(?&{name}))*)"
    return rf"(?P<{name}>{opener}{body}{closer})"


cdef object call = re.compile(
    rf"(?P<call>[^\(\)\[\],]+)"
    rf"({nested('(', ')', 'signature')}|{nested('[', ']', 'options')})"
)


cdef object sequence = re.compile(
    rf"(?P<sequence>"
    rf"{nested('(', ')', 'parens')}|"
    rf"{nested('[', ']', 'brackets')})"
)


cdef object literal = re.compile(rf"[^,]+")


cdef object token = re.compile(
    rf"{call.pattern}|{sequence.pattern}|{literal.pattern}"
)


cdef list tokenize(str input_str):
    """Split a comma-separated input string into individual tokens, respecting
    nested sequences and callable invocations.
    """
    return [x.group().strip() for x in token.finditer(input_str)]


cdef types.BaseType resolve_typespec_string(str input_str):
    """Resolve a string-based type specifier, returning a corresponding
    AtomicType.
    """
    # strip leading, trailing whitespace from input
    input_str = input_str.strip()

    # retrieve alias/regex registry
    registry = types.AtomicType.registry

    # ensure input consists only of resolvable type specifiers
    valid = registry.resolvable.fullmatch(input_str)
    if not valid:
        raise ValueError(
            f"could not interpret type specifier: {repr(input_str)}"
        )

    # parse every type specifier contained in the body of the input string
    result = set()
    input_str = valid.group("body")
    matches = [x.groupdict() for x in registry.regex.finditer(input_str)]
    for m in matches:
        base = registry.aliases[m["type"]]

        # if no args are provided, use the default kwargs
        if not m["args"]:  # empty string or None
            instance = base.instance()

        # tokenize args and pass to base.resolve()
        else:
            instance = base.resolve(*tokenize(m["args"]))

        # add to result set
        result.add(instance)

    # return either as single AtomicType or as CompositeType with 2+ types
    if len(result) == 1:
        return result.pop()
    return types.CompositeType(result)


cdef types.ScalarType resolve_typespec_dtype(object input_dtype):
    """Resolve a numpy/pandas dtype object, returning a corresponding
    AtomicType.
    """
    cdef types.TypeRegistry registry = types.AtomicType.registry
    cdef dict sparse = None
    cdef dict categorical = None
    cdef str unit
    cdef int step_size
    cdef types.ScalarType result = None

    # pandas special cases (sparse/categorical/DatetimeTZ)
    if isinstance(input_dtype, pd.api.extensions.ExtensionDtype):
        if isinstance(input_dtype, pd.SparseDtype):
            sparse = {"fill_value": input_dtype.fill_value}
            input_dtype = input_dtype.subtype
        if isinstance(input_dtype, pd.CategoricalDtype):
            categorical = {"levels": input_dtype.categories.tolist()}
            input_dtype = input_dtype.categories.dtype
        if isinstance(input_dtype, pd.DatetimeTZDtype):
            result = types.PandasTimestampType.instance(tz=input_dtype.tz)

    # numpy special cases (M8/m8/U)
    if result is None and isinstance(input_dtype, np.dtype):
        if np.issubdtype(input_dtype, "M8"):
            unit, step_size = np.datetime_data(input_dtype)
            result = types.NumpyDatetime64Type.instance(
                unit=None if unit == "generic" else unit,
                step_size=step_size
            )
        elif np.issubdtype(input_dtype, "m8"):
            unit, step_size = np.datetime_data(input_dtype)
            result = types.NumpyTimedelta64Type.instance(
                unit=None if unit == "generic" else unit,
                step_size=step_size
            )
        elif np.issubdtype(input_dtype, "U"):
            result = types.StringType.instance()

    # general case
    if result is None:
        result = registry.aliases[input_dtype].instance()

    # re-wrap with sparse/categorical
    if categorical:
        result = types.CategoricalType(result, **categorical)
    if sparse:
        result = types.SparseType(result, **sparse)
    return result


cdef types.AtomicType resolve_typespec_type(type input_type):
    """Resolve a runtime type definition, returning a corresponding AtomicType.
    """
    cdef dict aliases = types.AtomicType.registry.aliases
    cdef type result = aliases.get(input_type, types.ObjectType)

    if result is types.ObjectType:
        return result.instance(type_def=input_type)
    return result.instance()
