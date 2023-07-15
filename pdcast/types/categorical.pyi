"""Mypy stubs for pdcast/types/categorical.pyx"""
from typing import Any

from pdcast.types import DecoratorType

class CategoricalType(DecoratorType):
    levels: list | None

    def __init__(
        self, wrapped: VectorType | None = ..., levels: list[Any] | None = ...
    ) -> None:
        ...
