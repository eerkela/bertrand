"""This module provides PEP 484-style type hints for ``pdcast`` constructs.
"""
import datetime
import decimal
import numbers
from typing import runtime_checkable, Iterable, List, Protocol, Tuple, Union

import numpy as np
import numpy.typing
import pandas as pd


#######################
####    SCALARS    ####
#######################


numeric = Union[
    np.number,
    numbers.Complex,
    decimal.Decimal
]


datetime_like = Union[
    pd.Timestamp,
    datetime.datetime,
    np.datetime64
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
    pd.api.extensions.ExtensionDtype,
    Iterable[
        Union[
            type,
            str,
            np.dtype,
            pd.api.extensions.ExtensionDtype
        ]
    ]
]


#########################
####    ITERABLES    ####
#########################


array_like = numpy.typing.ArrayLike


list_like = Union[
    List,
    Tuple,
    array_like
]


#########################
####    PROTOCOLS    ####
#########################


@runtime_checkable
class Descriptor(Protocol):
    def __get__(self, instance, owner): ...
