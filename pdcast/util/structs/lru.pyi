"""Mypy stubs for pdcast/util/structs/lru.pyx"""
from typing import Any, Iterable, Mapping, Tuple

dict_like = Mapping[Any, Any] | Iterable[Tuple[Any, Any]]

class LRUDict(dict[Any, Any]):
    maxsize: int
    order: list[Any]

