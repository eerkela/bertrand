"""Mypy stubs for pdcast/util/round/float.pyx"""

from pdcast.util.type_hints import array_like

def round_float(val: float | array_like, rule: str, decimals: int) -> float: ...
