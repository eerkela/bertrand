import datetime
import decimal
from typing import runtime_checkable, Protocol, Union

import numpy as np
import pandas as pd


#########################
####    ITERABLES    ####
#########################


array_like = Union[
    np.ndarray,
    pd.Series
]


list_like = Union[
    list,
    tuple,
    np.ndarray,
    pd.Series
]


#######################
####    SCALARS    ####
#######################


datetime_like = Union[
    pd.Timestamp,
    datetime.datetime,
    np.datetime64
]


numeric = Union[
    int,
    np.integer,
    float,
    np.floating,
    complex,
    np.complexfloating,
    decimal.Decimal
]


timedelta_like = Union[
    pd.Timedelta,
    datetime.timedelta,
    np.timedelta64
]


type_specifier = Union[
    type,
    str,
    np.dtype,
    pd.api.extensions.ExtensionDtype
]


#########################
####    PROTOCOLS    ####
#########################


@runtime_checkable
class Descriptor(Protocol):
    def __get__(self, instance, owner): ...
