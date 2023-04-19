"""This module describes the ``resolve_type()`` function, which can parse
type specifiers in any of the following types:
    * Numpy/pandas dtype objects.
    * Raw python types (e.g. ``int``, ``float``, etc.).
    * Strings that conform to the type resolution mini-language.
    * Other AtomicType/CompositeType objects.
    * An iterable containing any of the above.

Custom aliases can be added or removed from the pool that is recognized by this
function through the ``register_alias()``, ``remove_alias()``, and
``clear_aliases()`` methods that are attached to every ``AtomicType`` and
``AdapterType`` definition.
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

import pdcast.util.array as array 
from pdcast.util.type_hints import type_specifier


# TODO: pdcast.resolve_type(pd.CategoricalDtype([False, 1, 2.]))
# -> CategoricalType must be able to wrap CompositeTypes


# TODO: for np.dtype inputs, iterate through aliases for instances of np.dtype,
# then do an np.issubdtype() check on each one and use the associated type.
# ExtensionDtypes should be checked by their type().  These can be separated
# into different helpers.


#####################
####   PUBLIC    ####
#####################


def resolve_type(typespec: type_specifier) -> types.BaseType:
    """Interpret types from manual
    :ref:`type specifiers <resolve_type.type_specifiers>`.

    Arguments
    ---------
    typespec : type specifier
        The :ref:`type specifier <resolve_type.type_specifiers>` to resolve.

    Returns
    -------
    AtomicType | AdapterType | CompositeType
        A type object corresponding to the given specifier.  If the specifier
        is an iterable, this will always be a :class:`CompositeType` object.

    Raises
    ------
    ValueError
        If the type specifier could not be resolved.

    See Also
    --------
    AtomicType.from_dtype : customizable resolution of numpy/pandas data types.
    AdapterType.from_dtype : customizable resolution of numpy/pandas data types.
    AtomicType.resolve : customizable semantics for the
        :ref:`type specification mini-language <resolve_type.mini_language>`.
    AdapterType.resolve : customizable semantics for the
        :ref:`type specification mini-language <resolve_type.mini_language>`.
    """
    if isinstance(typespec, types.BaseType):
        result = typespec
    elif isinstance(typespec, str):
        result = resolve_typespec_string(typespec)
    elif isinstance(typespec, type):
        result = resolve_typespec_type(typespec)
    elif isinstance(typespec, np.dtype):
        result = resolve_typespec_dtype_numpy(typespec)
    elif isinstance(typespec, pd.api.extensions.ExtensionDtype):
        result = resolve_typespec_dtype_pandas(typespec)
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
        if m.get("sized_unicode"):
            base = types.StringType
        else:
            base = registry.aliases[m["type"]]

        # tokenize args and pass to base.resolve()
        args = () if not m["args"] else tokenize(m["args"])
        instance = base.resolve(*args)

        # add to result set
        result.add(instance)

    # return either as single AtomicType or as CompositeType with 2+ types
    if len(result) == 1:
        return result.pop()
    return types.CompositeType(result)


cdef types.ScalarType resolve_typespec_dtype_numpy(object input_dtype):
    """Resolve a numpy/pandas dtype object, returning a corresponding
    AtomicType.
    """
    for k, v in types.AtomicType.registry.aliases.items():
        if isinstance(k, np.dtype) and np.issubdtype(input_dtype, k):
            return v.from_dtype(input_dtype)

    raise ValueError(f"numpy dtype not recognized: {input_dtype}")


cdef types.ScalarType resolve_typespec_dtype_pandas(object input_dtype):
    """Resolve a numpy/pandas dtype object, returning a corresponding
    AtomicType.
    """
    # fastpath for AbstractDtypes
    if isinstance(input_dtype, array.AbstractDtype):
        return input_dtype._atomic_type

    cdef types.TypeRegistry registry = types.AtomicType.registry

    # look up ExtensionDtype and pass example
    return registry.aliases[type(input_dtype)].from_dtype(input_dtype)


cdef types.ScalarType resolve_typespec_type(type input_type):
    """Resolve a python type, returning a corresponding AtomicType."""
    cdef dict aliases = types.AtomicType.registry.aliases
    cdef type result = aliases.get(input_type, None)

    if result is None:
        return types.ObjectType.instance(type_def=input_type)
    if issubclass(result, types.AdapterType):
        return result()
    return result.instance()
