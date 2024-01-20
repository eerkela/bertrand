"""Mypy stubs for pdcast/types/boolean.pyx"""
from pdcast.types import ScalarType, AbstractType

class BooleanType(AbstractType):
    ...

class NumpyBooleanType(ScalarType):
    ...

class PandasBooleanType(ScalarType):
    ...

class PythonBooleanType(ScalarType):
    ...
