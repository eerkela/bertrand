from __future__ import annotations
import sys

import pandas as pd

from pdcast.types import AdapterType, AtomicType, CompositeType

from pdcast.util.type_hints import type_specifier

from . import series  # attaches pd.Series.cast/pd.Series.check_type


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
        result[k] = result[k].cast(v, *args, **kwargs)
    return result


def check_type(
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
        self[k].check_type(v, *args, **kwargs)
        for k, v in dtype.items()
    )


def element_type(self) -> dict[str, AdapterType | AtomicType | CompositeType]:
    """Retrieve the element type of every column in a pandas DataFrame."""
    result = {}
    for col_name in self.columns:
        result[col_name] = self[col_name].element_type
    return result


############################
####    MONKEY PATCH    ####
############################


pd.DataFrame.cast = cast
pd.DataFrame.check_type = check_type
pd.DataFrame.element_type = property(element_type)


#######################
####    UNPATCH    ####
#######################


def detach() -> None:
    """Return `pd.DataFrame` objects back to their original state, before the
    patch was applied.
    """
    del pd.DataFrame.cast
    del pd.DataFrame.check_type
    del pd.DataFrame.element_type

    # prepare to reimport
    sys.modules.pop("pdcast.attach.dataframe")


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
