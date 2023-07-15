"""Mypy stubs for pdcast/types/timedelta.pyx"""
from pdcast.types import ScalarType, AbstractType

class TimedeltaType(AbstractType):
    ...

class PandasTimedeltaType(ScalarType):
    ...

class PythonTimeDeltaType(ScalarType):
    ...

class NumpyTimedelta64Type(ScalarType):
    unit: str | None
    step_size: int

    def __init__(self, unit: str | None = ..., step_size: int = ...) -> None: ...
