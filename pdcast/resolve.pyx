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

cimport pdcast.detect as detect
import pdcast.detect as detect
import pdcast.types as types
cimport pdcast.types as types

from pdcast.util.type_hints import type_specifier


# TODO: pdcast.resolve_type(pd.CategoricalDtype([False, 1, 2.]))
# -> CategoricalType must be able to wrap CompositeTypes


#####################
####   PUBLIC    ####
#####################


def resolve_type(typespec: type_specifier) -> types.BaseType:
    """Interpret types from manual specifiers.

    A type specifier can be any of the following:

        #.  A ``dtype`` or ``ExtensionDtype`` object.
        #.  A raw python type.  If the type has not been registered as an
            AtomicType alias, a new ``ObjectType`` will be built around its
            class definition.
        #.  An appropriate string in the
            :ref:`type specification mini-language <mini_language>`.
        #.  An iterable containing any combination of the above.

    Arguments
    ---------
    typespec : type specifier
        The type specifier to resolve.

    Returns
    -------
    BaseType
        A type object corresponding to the given specifier.  If the specifier
        is an iterable, this will always be a :class:`CompositeType` object.

    Raises
    ------
    ValueError
        If the type specifier could not be resolved.

    See Also
    --------
    AtomicType.resolve : Delegated method for resolve operations.
    AdapterType.resolve : Delegated method for resolve operations.
    TypeRegistry.aliases : Up-to-date map of all aliases recognized by this function.

    Notes
    -----
    This function's behavior can be customized by overriding
    :meth:`AtomicType.resolve` or :meth:`AdapterType.resolve` in individual
    type definitions.  See those functions for a guide on how to do this.

    :attr:`TypeRegistry.aliases` provides an up-to-date mapping of all the
    aliases that are recognized by this function to their corresponding type
    definitions.

    See the :ref:`type specification mini language <mini_language>` for help on
    how to construct string specifiers for arbitrary types.

    Examples
    --------
    .. testsetup::

        import numpy as np
        import pandas as pd
        import pdcast

    Raw classes:

    >>> pdcast.resolve_type(int)
    IntegerType()
    >>> class CustomObj:
    ...     pass
    >>> pdcast.resolve_type(np.float32)
    NumpyFloat32Type()
    >>> pdcast.resolve_type(CustomObj)
    ObjectType(type_def=<class 'CustomObj'>)

    dtype objects:

    >>> pdcast.resolve_type(np.dtype("?"))
    NumpyBooleanType()
    >>> pdcast.resolve_type(np.dtype("M8[30s]"))
    NumpyDatetime64Type(unit='s', step_size=30)
    >>> pdcast.resolve_type(pd.UInt8Dtype())
    PandasUInt8Type()

    Strings:

    >>> pdcast.resolve_type("string")
    StringType()
    >>> pdcast.resolve_type("timedelta[pandas], timedelta[python]")   # doctest: +SKIP
    CompositeType({timedelta[pandas], timedelta[python]})
    >>> pdcast.resolve_type("datetime[pandas, US/Pacific]")
    PandasTimestampType(tz=<DstTzInfo 'US/Pacific' LMT-1 day, 16:07:00 STD>)
    >>> pdcast.resolve_type("sparse[decimal]")
    SparseType(wrapped=DecimalType(), fill_value=Decimal('NaN'))
    >>> pdcast.resolve_type("sparse[bool, False]")
    SparseType(wrapped=BooleanType(), fill_value=False)
    >>> pdcast.resolve_type("categorical[str[pyarrow]]")
    CategoricalType(wrapped=PyArrowStringType(), levels=None)
    >>> pdcast.resolve_type("sparse[categorical[int]]")
    SparseType(wrapped=CategoricalType(wrapped=IntegerType(), levels=None), fill_value=<NA>)
    >>> pdcast.resolve_type()

    Iterables:

    >>> pdcast.resolve_type(["int"])
    CompositeType({int})
    >>> pdcast.resolve_type((float, complex))   # doctest: +SKIP
    CompositeType({float, complex})
    >>> pdcast.resolve_type({bool, np.dtype("i2"), "decimal"})   # doctest: +SKIP
    CompositeType({bool, int16[numpy], decimal})
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

    # sparse
    if isinstance(input_dtype, pd.SparseDtype):
        sparse = {"fill_value": input_dtype.fill_value}
        input_dtype = input_dtype.subtype

    # categorical
    if isinstance(input_dtype, pd.CategoricalDtype):
        categories = input_dtype.categories
        detected = detect.detect_type(categories)
        # if isinstance(detected, types.CompositeType):
        #     result = types.
        result = types.CategoricalType(detected, levels=categories.tolist())
    else:
        # special cases for M8/m8/U/DatetimeTZDtype
        if isinstance(input_dtype, pd.DatetimeTZDtype):
            result = types.PandasTimestampType.instance(tz=input_dtype.tz)
        elif isinstance(input_dtype, np.dtype):
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

        # base case
        if result is None:
            result = registry.aliases[input_dtype].instance()

    # re-wrap with sparse if applicable
    if sparse:
        result = types.SparseType(result, **sparse)
    return result


cdef types.AtomicType resolve_typespec_type(type input_type):
    """Resolve a runtime type definition, returning a corresponding AtomicType.
    """
    cdef dict aliases = types.AtomicType.registry.aliases
    cdef type result = aliases.get(input_type, None)

    if result is None:
        return types.ObjectType.instance(type_def=input_type)
    return result.instance()
