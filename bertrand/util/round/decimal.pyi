"""Mypy stubs for pdcast/util/round/decimal.pyx"""""
from decimal import Decimal

from pdcast.util.type_hints import array_like

def round_decimal(
    val: Decimal | array_like, rule: str, decimals: int
) -> Decimal | array_like:
    ...

