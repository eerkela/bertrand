"""This module contains all the prepackaged decimal types for the ``pdcast``
type system.
"""
import decimal
import sys

import numpy as np
cimport numpy as np
import pandas as pd
import pytz

from pdcast import convert
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve

from pdcast.util cimport wrapper
from pdcast.util.error import shorten_list
from pdcast.util.round cimport Tolerance
from pdcast.util.round import round_decimal
from pdcast.util.time cimport Epoch
from pdcast.util.time import as_ns, round_months_to_ns, round_years_to_ns
from pdcast.util.type_hints import numeric

from .base cimport AtomicType, CompositeType
from .base import dispatch, generic, register


# https://github.com/pandas-dev/pandas/blob/e246c3b05924ac1fe083565a765ce847fcad3d91/pandas/tests/extension/decimal/array.py


######################
####    MIXINS    ####
######################


class DecimalMixin:

    conversion_func = convert.to_decimal


#######################
####    GENERIC    ####
#######################


@register
@generic
class DecimalType(DecimalMixin, AtomicType):

    # internal root fields - all subtypes/backends inherit these
    _family = "decimal"
    _is_numeric = True

    name = "decimal"
    aliases = {"decimal"}
    type_def = decimal.Decimal
    itemsize = sys.getsizeof(decimal.Decimal(0))
    na_value = decimal.Decimal("nan")


##############################
####    PYTHON DECIMAL    ####
##############################


@register
@DecimalType.register_backend("python")
class PythonDecimalType(DecimalMixin, AtomicType):

    aliases = {decimal.Decimal}
    type_def = decimal.Decimal
    itemsize = sys.getsizeof(decimal.Decimal(0))
    na_value = decimal.Decimal("nan")
