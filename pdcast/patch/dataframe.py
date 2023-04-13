"""This module describes the logic necessary to patch ``pdcast`` functionality
into ``pandas.DataFrame`` objects.
"""
from __future__ import annotations

import pandas as pd

from pdcast.check import typecheck as typecheck_standalone
from pdcast.convert import cast
from pdcast.detect import detect_type
from pdcast.types import AdapterType, AtomicType, CompositeType

from pdcast.util.type_hints import type_specifier


# ignore this file when doing string-based object lookups in resolve_type()
_ignore_frame_objects = True


######################
####    PUBLIC    ####
######################


def attach() -> None:
    cast.attach_to(pd.DataFrame)
    pd.DataFrame.typecheck = typecheck
    pd.DataFrame.element_type = property(element_type)


def detach() -> None:
    """Return `pd.DataFrame` objects back to their original state, before the
    patch was applied.
    """
    del pd.DataFrame.cast
    del pd.DataFrame.typecheck
    del pd.DataFrame.element_type


########################
####    ADAPTERS    ####
########################


def typecheck(
    self,
    dtype: type_specifier | dict[str, type_specifier] = None,
    *args,
    **kwargs
) -> bool:
    """Do a schema validation check on a pandas DataFrame."""
    # if a mapping is provided, check that all column names are valid
    if isinstance(dtype, dict):
        validate_typespec_map(dtype, self.columns)
    else:  # broadcast across columns
        dtype = dict.fromkeys(self.columns, dtype)

    # check selected columns
    return all(
        typecheck_standalone(self[k], v, *args, **kwargs)
        for k, v in dtype.items()
    )


def element_type(self) -> dict[str, AdapterType | AtomicType | CompositeType]:
    """Retrieve the element type of every column in a pandas DataFrame."""
    result = {}
    for col_name in self.columns:
        result[col_name] = detect_type(self[col_name])
    return result


#######################
####    PRIVATE    ####
#######################


def validate_typespec_map(
    mapping: dict[str, type_specifier],
    columns: pd.Index
) -> None:
    """Given a user-defined map from columns to type specifiers, ensure that
    each column name is present in the DataFrame.
    """
    bad = [col_name for col_name in mapping if col_name not in columns]
    if bad:
        if len(bad) == 1:  # singular
            err_msg = f"could not find column {repr(bad[0])}"
        else:
            err_msg = f"could not find columns {repr(bad)}"
        raise ValueError(err_msg)
