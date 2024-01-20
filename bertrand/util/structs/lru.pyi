"""Mypy stubs for pdcast/util/structs/lru.pyx"""
from __future__ import annotations
from typing import Any, Hashable, Iterable, Mapping, Tuple

from .list import HashedList

dict_like = Mapping[Hashable, Any] | Iterable[Tuple[Hashable, Any]]

class LRUDict(dict[Any, Any]):
    maxsize: int
    order: HashedList

    def __init__(self, *args: Any, maxsize: int = 128, **kwargs: Any) -> None: ...

    ##################################
    ####    DICTIONARY METHODS    ####
    ##################################

    def get(self, key: Hashable, default: Any | None = None) -> Any: ...
    def setdefault(self, key: Hashable, default: Any | None = None) -> Any: ...
    def update(self, other: dict_like, /, **kwargs: Any) -> None: ...  # type: ignore
    def pop(self, key: Hashable, default: Any | None = None) -> Any: ...
    def popitem(self) -> tuple[Hashable, Any]: ...
    def clear(self) -> None: ...
    def copy(self) -> LRUDict: ...

    ###########################
    ####    NEW METHODS    ####
    ###########################

    def sort(self) -> None: ...

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __getitem__(self, key: Hashable) -> Any: ...
    def __setitem__(self, key: Hashable, value: Any) -> None: ...
    def __delitem__(self, key: Hashable) -> None: ...
