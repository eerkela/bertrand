"""This module contains all the prepackaged decimal types for the ``pdcast``
type system.
"""
import decimal
import sys
from typing import Callable

cimport pdcast.resolve as resolve
import pdcast.resolve as resolve
from pdcast.util.type_hints import numeric

from .base cimport AtomicType, CompositeType
from .base import generic, register


# https://github.com/pandas-dev/pandas/blob/e246c3b05924ac1fe083565a765ce847fcad3d91/pandas/tests/extension/decimal/array.py


#######################
####    GENERIC    ####
#######################


@register
@generic
class DecimalType(AtomicType):

    # internal root fields - all subtypes/backends inherit these
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
class PythonDecimalType(AtomicType):

    aliases = {decimal.Decimal}
    type_def = decimal.Decimal
    itemsize = sys.getsizeof(decimal.Decimal(0))
    na_value = decimal.Decimal("nan")
