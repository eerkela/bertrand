"""Mypy stubs for pdcast/types/datetime.pyx"""
from datetime import tzinfo

from pdcast.types import ScalarType, AbstractType

class DatetimeType(AbstractType):
    ...

class PandasTimestampType(ScalarType):
    tz: tzinfo | None

    def __init__(self, tz: tzinfo | str | None = ...) -> None: ...

class PythonDatetimeType(ScalarType):
    tz: tzinfo | None

    def __init__(self, tz: tzinfo | str | None = ...) -> None: ...

class NumpyDatetime64Type(ScalarType):
    unit: str | None
    step_size: int

    def __init__(self, unit: str | None = ..., step_size: int = ...) -> None: ...
