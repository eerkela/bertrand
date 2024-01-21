from __future__ import annotations

from typing import Any, TypeVar


T = TypeVar("T", bound="Meta")


class Meta(type):

    def __new__(
        mcs,
        name: str,
        bases: tuple[T, ...],
        namespace: dict[str, Any]
    ) -> T:
        cls = type.__new__(mcs, name, bases, namespace)
        return cls.__new__(cls)



class Foo(metaclass=Meta):

    @property
    def prop(self) -> str:
        return "abc"



class Bar(type(Foo)):
    pass

