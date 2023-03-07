from typing import Any

from pdcast.detect import detect_type
from pdcast.resolve import resolve_type
from pdcast.types cimport BaseType, CompositeType
from pdcast.util.type_hints import type_specifier


def typecheck(
    data: Any,
    dtype: type_specifier ,
    exact: bool = False
) -> bool:
    """Check whether example data contains elements of the given type.

    Parameters
    ----------
    :param data: The example data whose type will be checked.

    Attached
    --------
    If ``pdcast.attach`` is imported, this function is directly attached to
    ``pd.Series`` objects, allowing users to omit the ``data`` argument.

    .. testsetup:: typecheck_test

        import decimal

    .. doctest:: typecheck_test

        >>> import pandas as pd
        >>> import pdcast.attach
        >>> pd.Series([1, 2, 3]).typecheck(int)
        True
        >>> pd.Series([1, 2, 3]).typecheck(float)
        False
        >>> pd.Series([1, 2, 3]).typecheck(int, exact=True)
        False
        >>> pd.Series([1, 2, 3]).typecheck("int64[numpy]", exact=True)
        True
    """
    cdef BaseType data_type = detect_type(data)
    cdef CompositeType target_type = resolve_type([dtype])
    cdef set exact_target

    # enforce strict match
    if exact:
        exact_target = set(target_type)
        if isinstance(data_type, CompositeType):
            return all(t in exact_target for t in data_type)
        return data_type in exact_target

    # include subtypes
    return target_type.contains(data_type)
