"""This package contains all the internal machinery and basic building blocks that
comprise the bertrand type system.
"""
from .decorator import DecoratorType
from .meta import (
    ALIAS, DEBUG, DTYPE, EMPTY, META, POINTER_SIZE, REGISTRY, TYPESPEC, Aliases,
    BaseMeta, DecoratorMeta, DecoratorPrecedence, Edges, Empty, StructuredMeta,
    StructuredUnion, TypeMeta, TypeRegistry, Union, UnionMeta, detect, resolve
)
from .type import Type
