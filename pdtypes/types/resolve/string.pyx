from typing import Iterator

import numpy as np
import pandas as pd
import regex as re  # using alternate python regex

from ..atomic.base cimport AtomicType, CompositeType


cdef str nested(str opener, str closer, str name):
    opener = re.escape(opener)
    closer = re.escape(closer)
    return (
        rf"(?P<{name}>{opener}"
        rf"(?P<content>([^{opener}{closer}]|(?&{name}))*)"
        rf"{closer})"
    )


cdef str parens = nested("(", ")", "parens")
cdef str brackets = nested("[", "]", "brackets")
cdef str curlies = nested("{", "}", "curlies")  # broken
cdef str call = rf"(?P<name>[^,]*)({parens}|{brackets}|{curlies})"
cdef object tokenize_regex = re.compile(rf"{call}|[^,]+")


cdef list tokenize(str input_str):
    return [x.group().strip() for x in tokenize_regex.finditer(input_str)]


def resolve_typespec_string(str input_str) -> AtomicType | CompositeType:
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
