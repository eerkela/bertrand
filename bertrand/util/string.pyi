"""Mypy stubs for pdcast/util/string.pyx"""

# TODO: deprecate this in function in favor of full regex matching
def boolean_match(
    val: str, lookup: dict[str, int], ignore_case: bool, fill: int
) -> int:
    ...

def int_to_base(val: int, base: int, width: int) -> str: ...
