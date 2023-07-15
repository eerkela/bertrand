"""Mypy stubs for pdcast/types/sparse.pyx"""
from typing import Any

from pdcast.types import DecoratorType

class SparseType(DecoratorType):
    fill_value: Any

    def __init__(
        self, wrapped: VectorType | None = ..., levels: list[Any] | None = ...
    ) -> None:
        ...
