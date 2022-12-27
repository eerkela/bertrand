from typing import Iterator

import numpy as np
import pandas as pd
import regex as re  # using alternate python regex

from ..atomic.base cimport AtomicType, CompositeType


# TODO: tokenize() needs to account for types that have spaces in their
# aliases.  Just put an AtomicType.registry.regex check before any
# call signatures


# TODO: AtomicType regex should allow for nested brackets.  I think the only
# reason why it doesn't is that (?R) is trying to match the name part of a
# type specifer as well, rather than just its args.  Maybe nested() needs
# to be in base?
# -> if that's the case, just put all resolve_* functions in there as well.
# That way, SparseType can just import everything from base.


cdef str nested(str opener, str closer):
    opener = re.escape(opener)
    closer = re.escape(closer)
    return rf"{opener}(?P<content>([^{opener}{closer}]|(?R))*){closer}"


cdef str parens = nested("(", ")")
cdef str brackets = nested("[", "]")
cdef str curlies = nested("{", "}")
cdef str call = rf"(?P<name>\w*)({parens}|{brackets})"
cdef str whitelist = r"[<>\w]+"
cdef object tokenize_regex = re.compile(rf"{call}|{curlies}|{whitelist}")


cdef list tokenize(str input_str):
    return [x.group() for x in tokenize_regex.finditer(input_str)]


def resolve_string_typespec(str input_str) -> AtomicType | CompositeType:
    # strip leading, trailing whitespace from input
    input_str = input_str.strip()

    # retrieve alias/regex registry
    registry = AtomicType.registry

    # ensure input consists only of resolvable type specifiers
    valid =  registry.resolvable.fullmatch(input_str)
    if not valid:
        raise ValueError(
            f"could not interpret type specifier: {repr(input_str)}"
        )

    # parse every type specifier contained in the body of the input string
    result = set()
    input_str = valid.group("body")
    matches = [x.groupdict() for x in registry.regex.finditer(input_str)]
    for m in matches:
        # get type definition and default kwargs for the given alias
        info = registry.aliases[m["type"]]

        # if no args are provided, use the default kwargs
        if not m["args"]:  # empty string or None
            instance = info.type.instance(**info.default_kwargs)

        # tokenize args and pass to info.type.from_string()
        else:
            instance = info.type.from_string(*tokenize(m["args"]))

        # add to result set
        result.add(instance)

    # return either as single AtomicType or as CompositeType with 2+ types
    if len(result) == 1:
        return result.pop()
    return CompositeType(result)
