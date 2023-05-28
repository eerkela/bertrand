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

cimport numpy as np
import numpy as np
import pandas as pd

import pdcast.types as types
cimport pdcast.types as types
import pdcast.types.array.abstract as abstract

from pdcast.util.type_hints import type_specifier


# TODO: pdcast.resolve_type(pd.CategoricalDtype([False, 1, 2.]))
# -> CategoricalType must be able to wrap CompositeTypes


#####################
####   PUBLIC    ####
#####################


def resolve_type(typespec: type_specifier) -> types.Type:
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
    # trivial case
    if isinstance(typespec, types.Type):
        return typespec

    # build factory
    if isinstance(typespec, type):
        factory = ClassResolver(typespec)

    elif isinstance(typespec, str):
        factory = StringResolver(typespec)

    elif isinstance(typespec, (np.dtype, pd.api.extensions.ExtensionDtype)):
        if isinstance(typespec, abstract.AbstractDtype):
            return typespec._atomic_type

        factory = DtypeResolver(typespec)

    elif hasattr(typespec, "__iter__"):
        return types.CompositeType(resolve_type(x) for x in typespec)

    else:
        raise invalid_specifier(typespec)

    # execute factory
    return factory()


#######################
####    PRIVATE    ####
#######################


cdef Exception invalid_specifier(object typespec):
    """Standardized exception for invalid type specifiers."""
    return ValueError(f"invalid specifier: {repr(typespec)}")


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


cdef class Resolver:
    """A factory that returns type objects."""

    def __init__(self):
        self.aliases = dict(types.registry.aliases)

    def __call__(self, typespec: type_specifier) -> types.Type:
        raise NotImplementedError(f"{type(self)} does not implement __call__")


cdef class ClassResolver(Resolver):
    """A factory that constructs types from Python class objects."""

    def __init__(self, type specifier):
        super().__init__()
        self.specifier = specifier

    def __call__(self) -> types.Type:
        if self.specifier in self.aliases:
            return self.aliases[self.specifier]

        return types.ObjectType(self.specifier)


cdef class DtypeResolver(Resolver):
    """A factory that constructs types from numpy/pandas dtype objects."""

    def __init__(self, object specifier):
        super().__init__()
        self.specifier = specifier

    def __call__(self) -> types.Type:
        cdef types.Type instance

        instance = self.aliases[type(self.specifier)]
        if hasattr(instance, "from_dtype"):
            return instance.from_dtype(self.specifier)
        return instance


cdef class StringResolver(Resolver):
    """A factory that constructs types from strings in the type specification
    mini-language.
    """

    def __init__(self, str specifier):
        super().__init__()

        # strip leading/trailing whitespace
        specifier = specifier.strip()

        # ensure string contains valid specifiers
        resolvable = types.registry.resolvable.fullmatch(specifier)
        if not resolvable:
            raise invalid_specifier(specifier)

        # strip prefix/suffix if present
        self.specifier = resolvable.group("body")

    cdef types.Type process_match(self, object match):
        """Construct a type from a regex match."""
        cdef dict match_dict
        cdef types.Type instance
        cdef str args
        cdef list tokens

        match_dict = match.groupdict()

        if match_dict.get("sized_unicode"):  # special case for U32, U17, ...
            instance = types.StringType
        else:
            instance = self.aliases[match_dict["type"]]

        if hasattr(instance, "resolve"):
            args = match_dict["args"]
            tokens = [] if not args else tokenize(args)
            instance = instance.resolve(*tokens)

        return instance

    def __call__(self) -> types.Type:
        cdef list matches
        cdef types.CompositeType composite

        matches = list(types.registry.regex.finditer(self.specifier))

        if len(matches) > 1:
            composite = types.CompositeType(
                self.process_match(match) for match in matches
            )
            if len(composite) == 1:
                return composite.pop()
            return composite

        return self.process_match(matches[0])  # guaranteed to have at least 1
