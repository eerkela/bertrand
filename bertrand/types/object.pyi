"""Mypy stubs for pdcast/types/object.pyx"""
from pdcast.types import ScalarType

class ObjectType(ScalarType):
    def __init__(self, type_def: type = ...) -> None: ...
