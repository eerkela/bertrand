"""Mypy stubs for pdcast/util/round/integer.pyx"""

from pdcast.util.type_hints import array_like

def round_div(
    numerator: int | array_like, denominator: int | array_like, rule: str
) -> int | array_like:
    ...
