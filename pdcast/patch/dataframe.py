from __future__ import annotations

import pandas as pd

from pdcast.check import typecheck as typecheck_standalone
from pdcast.convert import cast as cast_standalone
from pdcast.detect import detect_type
from pdcast.types import AdapterType, AtomicType, CompositeType

from pdcast.util.type_hints import type_specifier


# TODO: if this is a .pyx file, then there is no need for _ignore_object_frame
# -> this causes a headache with bound/unbound methods though.


# ignore this file when doing string-based object lookups in resolve_type()
_ignore_frame_objects = True


########################
####    ADAPTERS    ####
########################


def cast(
    self,
    dtype: type_specifier | dict[str, type_specifier] = None,
    *args,
    **kwargs
) -> pd.DataFrame:
    """Cast multiple columns of a pandas DataFrame to another data type."""
    # if a mapping is provided, check that all columns are valid
    if isinstance(dtype, dict):
        validate_typespec_map(dtype, self.columns)
    else:  # broadcast across columns
        dtype = dict.fromkeys(self.columns, dtype)

    # cast selected columns
    result = self.copy()
    for k, v in dtype.items():
        result[k] = cast_standalone(result[k], v, *args, **kwargs)
    return result


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


############################
####    MONKEY PATCH    ####
############################


def attach() -> None:
    pd.DataFrame.cast = cast
    pd.DataFrame.typecheck = typecheck
    pd.DataFrame.element_type = property(element_type)


def detach() -> None:
    """Return `pd.DataFrame` objects back to their original state, before the
    patch was applied.
    """
    del pd.DataFrame.cast
    del pd.DataFrame.typecheck
    del pd.DataFrame.element_type


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
