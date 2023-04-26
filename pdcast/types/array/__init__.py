"""This package contains factories for ``dtype: object``-backed
ExtensionArrays, as well as specific implementations for particular data types.

Modules
-------
abstract
    Automatic generation of object-backed ``ExtensionDtype`` definitions.
"""
from .abstract import (
    AbstractArray, AbstractDtype, construct_array_type,
    construct_extension_dtype
)
