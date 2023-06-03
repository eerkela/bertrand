"""This module contains all the prepackaged decimal types for the ``pdcast``
type system.
"""
import decimal
import sys

from pdcast.util.type_hints import numeric

from .base cimport AtomicType, ParentType, CompositeType
from .base import register


# https://github.com/pandas-dev/pandas/blob/e246c3b05924ac1fe083565a765ce847fcad3d91/pandas/tests/extension/decimal/array.py


#######################
####    GENERIC    ####
#######################


@register
class DecimalType(ParentType):

    name = "decimal"
    aliases = {"decimal"}


##############################
####    PYTHON DECIMAL    ####
##############################


@register
@DecimalType.default
@DecimalType.implementation("python")
class PythonDecimalType(AtomicType):

    aliases = {decimal.Decimal}
    type_def = decimal.Decimal
    itemsize = sys.getsizeof(decimal.Decimal(0))
    na_value = decimal.Decimal("nan")
    is_numeric = True
