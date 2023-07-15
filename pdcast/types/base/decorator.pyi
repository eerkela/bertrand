"""Mypy stubs for pdcast/types/base/decorator.pyx"""
from typing import Any, Iterator, Mapping

import pandas as pd  # type: ignore

from pdcast.types import ScalarType, VectorType
from pdcast.util.type_hints import array_like, dtype_like, type_specifier


class DecoratorType(VectorType):
    def __init__(self, wrapped: VectorType | None = None, **kwargs: Any) -> None: ...

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def from_string(self, wrapped: str | None = ..., *args: str) -> DecoratorType: ...
    def from_dtype(
        self, dtype: dtype_like, array: array_like | None = ...
    ) -> DecoratorType:
        ...
    def replace(self, **kwargs: Any) -> DecoratorType: ...

    ##########################
    ####    MEMBERSHIP    ####
    ##########################

    def contains(self, other: type_specifier) -> bool: ...

    #################################
    ####    DECORATOR PATTERN    ####
    #################################

    @property
    def wrapped(self) -> VectorType: ...
    @property
    def decorators(self) -> Iterator[DecoratorType]: ...
    def unwrap(self) -> ScalarType | None: ...
    def __getattr__(self, name: str) -> Any: ...

    ####################################
    ####    DATA TRANSFORMATIONS    ####
    ####################################

    def transform(self, series: pd.Series) -> pd.Series: ...
    def inverse_transform(self, series: pd.Series) -> pd.Series: ...

    ##########################
    ####    OVERRIDDEN    ####
    ##########################

    _cache_size: int

    @property
    def implementations(self) -> Mapping[str, DecoratorType]: ...
    def __dir__(self) -> list[str]: ...
    def __call__(
        self, wrapped: VectorType, *args: Any, **kwargs: Any
    ) -> DecoratorType:
        ...
    def __lt__(self, other: type_specifier) -> bool: ...
    def __gt__(self, other: type_specifier) -> bool: ...
