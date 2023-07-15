"""Mypy stubs for pdcast/check.pyx"""
from typing import Any

from pdcast.util.type_hints import type_specifier


def typecheck(
    data: Any,
    target: type_specifier,
    ignore_decorators: bool = ...
) -> bool:
    ...
