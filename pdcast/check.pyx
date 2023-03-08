from typing import Any

from pdcast.detect import detect_type
from pdcast.resolve import resolve_type
from pdcast.types cimport BaseType, CompositeType
from pdcast.util.type_hints import type_specifier


# TODO: typecheck(1, "int[python]", exact=True) == False !
# -> maybe base python types should always be in their python backends.  Just
# specify this in resolve_type() docs.


def typecheck(
    data: Any,
    dtype: type_specifier ,
    exact: bool = False
) -> bool:
    """Check whether example data contains elements of a specified type.

    This function is a direct analogue for ``isinstance()`` checks, but
    extended to vectorized data.  It will return ``True`` if and only if the
    example data is described by the given type specifier or one of its
    subtypes (depending on the value of the ``exact`` argument).  Just like
    ``isinstance()``, it can also accept multiple type specifiers to compare
    against, and will return ``True`` if **any** of them match the example
    data.

    Parameters
    ----------
    data : Any
        The example data whose type will be checked.
    dtype : type_specifier
        The type to compare against.  This can be in any format accepted by
        :py:func:`pdcast.resolve_type`.
    exact : bool, default False
        Specifies whether to include subtypes in comparisons (False), or check
        only for exact matches (True).

    Returns
    -------
    bool
        True if the data matches the specified type, False otherwise.

    Raises
    ------
    ValueError
        If the comparison type could not be resolved.

    See Also
    --------
    detect_type : Vectorized type inference.
    resolve_type : Generalized type resolution.
    CompositeType.contains : Type membership checks.

    Notes
    -----
    If ``pdcast.attach`` is imported, this function is directly attached to
    ``pd.Series`` objects, allowing users to omit the ``data`` argument.

    >>> import decimal
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

    Examples
    --------
    If ``exact=False`` (the default), then subtypes will be included in
    membership tests.  For example:

    >>> pd.Series([1, 2, 3], dtype="i2").typecheck("int", exact=False)
    True
    >>> pd.Series([1., 2., 3.], dtype="f8").typecheck("float, complex", exact=False)
    True

    Return ``True`` because ``np.int16`` and ``np.float64`` objects are
    subtypes of ``"int"`` and ``"float"`` respectively.  Conversely,

    >>> pd.Series([1, 2, 3], dtype="i2").typecheck("int", exact=True)
    False
    >>> pd.Series([1., 2., 3.], dtype="f8").typecheck("float, complex", exact=True)
    False

    Return ``False`` because ``np.int16`` and ``np.float64`` objects do not
    **exactly** match the ``"int"`` or ``"float"`` specifiers.
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
