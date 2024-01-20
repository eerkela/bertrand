"""Mypy stubs for pdcast/detect.pyx"""
from typing import Any, Iterable

import numpy as np

from pdcast.types import Type, ScalarType
from pdcast.util.type_hints import array_like, type_specifier


def detect_type(data: Any, drop_na: bool = ...) -> Type | dict[str, Type]:
    ...


class Detector:
    aliases: dict[str, type_specifier]

    def __init__(self) -> None:
        ...

    def __call__(self) -> Type:
        ...


class ScalarDetector(Detector):
    example: Any
    example_type: type

    def __init__(self, example: Any, example_type: type) -> None:
        ...

    def __call__(self) -> ScalarType:
        ...


class ArrayDetector(Detector):
    data: array_like
    drop_na: bool

    def __init__(self, data: array_like, drop_na: bool) -> None:
        ...


class ElementwiseDetector(Detector):
    data: np.ndarray[Any, np.dtype[object]]
    missing: np.ndarray[np.bool_, np.dtype[bool]]
    drop_na: bool
    hasnans: bool

    def __init__(self, data: Iterable[Any], drop_na: bool) -> None:
        ...
