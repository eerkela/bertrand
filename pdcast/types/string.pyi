"""Mypy stubs for pdcast/types/string.pyx"""
from pdcast.types import ScalarType, AbstractType

class StringType(AbstractType):
    ...

class PythonStringType(ScalarType):
    ...

class PyArrowStringType(ScalarType):
    ...
