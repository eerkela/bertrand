"""Mypy stubs for pdcast/resolve.pyx"""
from pdcast.types import Type
from pdcast.util.type_hints import dtype_like, type_specifier


def resolve_type(target: type_specifier) -> Type:
    ...


class Resolver:
    aliases: dict[str, type_specifier]

    def __init__(self) -> None:
        ...

    def __call__(self) -> Type:
        ...


class ClassResolver(Resolver):
    specifier: type


class DtypeResolver(Resolver):
    dtype: dtype_like


class StringResolver(Resolver):
    specifier: str
