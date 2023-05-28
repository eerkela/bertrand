"""This module describes the ``typecheck()`` function, which enables fast,
``isinstance()``-like type checks for data in the ``pdcast`` type system.
"""
from typing import Any

import pandas as pd

from pdcast.decorators.attachable import attachable
from pdcast.detect import detect_type
from pdcast.resolve import resolve_type
from pdcast.types cimport Type, CompositeType
from pdcast.util.type_hints import type_specifier


@attachable
def typecheck(
    data: Any,
    dtype: type_specifier ,
    include_subtypes: bool = True,
    ignore_adapters: bool = False
) -> bool:
    """Check whether example data contains elements of a specified type.

    Parameters
    ----------
    data : Any
        The example data whose type will be checked.  This can be in any format
        recognized by :func:`detect_type`.
    dtype : type specifier
        The type to compare against.  This can be in any format accepted by
        :func:`resolve_type`.
    include_subtypes : bool, default True
        Specifies whether to include :func:`subtypes <subtype>` in comparisons
        (True), or only check for backend matches (False).
    ignore_adapters : bool, default False
        Specifies whether to ignore :class:`adapters <AdapterType>` that are
        detected in example data.  By default, the comparison type must match
        these exactly.  Setting this ``True`` eliminates that requirement,
        allowing specifiers like ``"int"`` to also match decorated
        alternatives, like :class:`sparse <SparseType>` and
        :class:`categorical <CategoricalType>` equivalents.

    Returns
    -------
    bool
        ``True`` if the data matches the specified type, ``False`` otherwise.

    Raises
    ------
    ValueError
        If ``dtype`` could not be :func:`resolved <ressolve_type>`.

    See Also
    --------
    AtomicType.contains : Customizable membership checks.
    AdapterType.contains : Customizable membership checks.
    """
    # DataFrame (columnwise) case
    if isinstance(data, pd.DataFrame):
        columns = data.columns
        if isinstance(dtype, dict):
            bad = [col for col in dtype if col not in columns]
            if bad:
                raise ValueError(f"column not found: {repr(bad)}")
        else:
            dtype = dict.fromkeys(columns, dtype)

        # pass each column individually
        return all(
            typecheck(
                data[col],
                typespec,
                include_subtypes=include_subtypes,
                ignore_adapters=ignore_adapters
            )
            for col, typespec in dtype.items()
        )

    cdef CompositeType data_type = CompositeType(detect_type(data))
    cdef CompositeType target_type = resolve_type([dtype])

    # strip adapters if directed
    if ignore_adapters:
        target_type = CompositeType(x.unwrap() for x in target_type)
        data_type = CompositeType(x.unwrap() for x in data_type)

    return target_type.contains(data_type, include_subtypes=include_subtypes)
