import decimal
from typing import Union, Sequence

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AtomicType
from .base import generic

cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve
from pdtypes.util.round import round_decimal


######################
####    MIXINS    ####
######################


class DecimalMixin:

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    def round(
        self,
        series: cast.SeriesWrapper,
        rule: str = "half_even",
        decimals: int = 0
    ) -> pd.Series:
        """Round a decimal series to the given number of decimal places using
        the specified rounding rule.
        """
        return round_decimal(series.series, rule=rule, decimals=decimals)


#######################
####    GENERIC    ####
#######################


@generic
class DecimalType(DecimalMixin, AtomicType):

    conversion_func = cast.to_boolean  # all subtypes/backends inherit this
    name = "decimal"
    aliases = {"decimal"}

    def __init__(self):
        super().__init__(
            type_def=decimal.Decimal,
            dtype=np.dtype(np.object_),
            na_value=pd.NA,
            itemsize=None
        )


##############################
####    PYTHON DECIMAL    ####
##############################


@DecimalType.register_backend("python")
class PythonDecimalType(DecimalMixin, AtomicType):

    aliases = {decimal.Decimal}

    def __init__(self):
        super().__init__(
            type_def=decimal.Decimal,
            dtype=np.dtype(np.object_),
            na_value=pd.NA,
            itemsize=None
        )
