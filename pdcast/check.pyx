from typing import Any

from pdcast.detect import detect_type
from pdcast.resolve import resolve_type
from pdcast.types cimport BaseType, CompositeType
from pdcast.util.type_hints import type_specifier


def typecheck(
    data: Any,
    dtype: type_specifier ,
    include_subtypes: bool = True,
    ignore_adapters: bool = False
) -> bool:
    """Check whether example data contains elements of a specified type.

    This function is a direct analogue for ``isinstance()`` checks, but
    extended to vectorized data.  It returns ``True`` if and only if the
    example data is described by the given type specifier or one of its
    subtypes.  Just like ``isinstance()``, it can also accept multiple type
    specifiers to compare against, and will return ``True`` if **any** of them
    match the example data.

    Parameters
    ----------
    data : Any
        The example data whose type will be checked.  This can be a scalar or
        list-like iterable of any kind.
    dtype : type specifier
        The type to compare against.  This can be in any format accepted by
        :func:`resolve_type`.
    include_subtypes : bool, default True
        Specifies whether to include subtypes in comparisons (True), or only
        check for backend matches (False).
    ignore_adapters : bool, default False
        Specifies whether to ignore adapters (sparse, categorical, etc.) that
        are detected in example data.  By default, the comparison type must
        match these exactly.  Setting this to ``True`` eliminates that
        requirement, allowing specifiers like ``"int"`` to match sparse and
        categorical alternatives.

    Returns
    -------
    bool
        ``True`` if the data matches the specified type, ``False`` otherwise.

    Raises
    ------
    ValueError
        If the comparison type could not be resolved.

    See Also
    --------
    AtomicType.contains : Customizable membership checks.
    AdapterType.contains : Customizable membership checks.

    Notes
    -----
    If :func:`pdcast.attach()` is invoked, this function is directly attached
    to ``pandas.Series`` objects, allowing users to omit the ``data`` argument.

    >>> import pandas as pd
    >>> import pdcast; pdcast.attach()
    >>> pd.Series([1, 2, 3]).typecheck("int")
    True
    >>> pd.Series([1, 2, 3]).typecheck("int64", include_subtypes=False)
    True

    Examples
    --------
    If ``include_subtypes=False``, then subtypes will be excluded from membership tests.

    >>> import pandas as pd
    >>> import pdcast
    >>> series = pd.Series([1, 2, 3], dtype="i2")
    >>> series
    0    1
    1    2
    2    3
    dtype: int16
    >>> pdcast.typecheck(series, "int", include_subtypes=True)
    True
    >>> pdcast.typecheck(series, "int", include_subtypes=False)
    False
    >>> pdcast.typecheck(series, "int16", include_subtypes=False)
    True
    """
    cdef CompositeType data_type = CompositeType(detect_type(data))
    cdef CompositeType target_type = resolve_type([dtype])

    # strip adapters if directed
    if ignore_adapters:
        target_type = CompositeType(x.unwrap() for x in target_type)
        data_type = CompositeType(x.unwrap() for x in data_type)

    return target_type.contains(data_type, include_subtypes=include_subtypes)
