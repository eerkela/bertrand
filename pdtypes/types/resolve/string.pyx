import re

from ..atomic.base cimport AtomicType, ElementType, CompositeType


cdef object directive_regex
cdef object tokenize_regex = re.compile(r",\s*")


cdef list tokenize(str input_str):
    """Splits a comma-separated input string into its constituent parts."""
    return re.split(tokenize_regex, input_str)


def resolve_atomic_type(str input_str) -> AtomicType:
    """Given an atomic type specifier `"{type}[{arg1}, {arg2}, ...]"`, return
    the specified AtomicType instance.
    """
    # TODO: check for multiple matches ("intfloat" for example)
    match = re.match(AtomicType.registry.regex, input_str)
    if not match:
        raise ValueError(
            f"could not interpret type specifier: {repr(input_str)}"
        )

    # get named regex groups
    groups = match.groupdict()
    info = AtomicType.registry.aliases[groups["type"]]

    # if no args are given, use the default ones defined in alias
    args = groups.get("args", None)
    if args is None:
        return info.type.instance(**info.default_kwargs)

    # if args are given, pass them to AtomicType.from_typespec()
    return info.type.from_typespec(*tokenize(args))

