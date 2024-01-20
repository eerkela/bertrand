"""This module contains helper functions for conversions to and from string
representations.
"""


# TODO: deprecate this function in favor of full regex matching
cpdef char boolean_match(
    str val,
    dict lookup,
    bint ignore_case,
    char fill
) except -1:
    """Convert a scalar string into a boolean value using the given lookup
    dictionary.

    Notes
    -----
    The ``fill`` argument is implemented as an 8-bit integer as an
    optimization.  It can take on the following values:

        *   ``1``, signaling that ``KeyErrors`` should be taken as ``True``
        *   ``0``, signaling that ``KeyErrors`` should be taken as ``False``
        *   ``-1``, signaling that ``KeyErrors`` should be raised up the stack.

    """
    if ignore_case:
        val = val.lower()

    if fill == -1:
        try:
            return lookup[val]
        except KeyError as err:
            err_msg = f"encountered non-boolean value: '{val}'"
            raise ValueError(err_msg) from err

    return lookup.get(val, fill)


cdef Py_UNICODE[36] base_lookup = (
    [chr(ord("0") + i) for i in range(10)] + 
    [chr(ord("A") + i) for i in range(26)]
)


cpdef str int_to_base(object val, unsigned char base):
    """Convert an integer into a string with the given base."""
    cdef bint negative
    cdef list chars
    cdef str result

    if not val:
        return "0"

    negative = val < 0
    if negative:
        val = abs(val)

    chars = []
    while val:
        chars.append(base_lookup[val % base])
        val //= base

    result = "".join(chars[::-1])
    if negative:
        result = "-" + result
    return result
