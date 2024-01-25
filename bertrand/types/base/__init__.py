"""This package contains all the internal machinery and basic building blocks that
comprise the bertrand type system.
"""
from .meta import (
    ALIAS, DEBUG, DTYPE, EMPTY, META, POINTER_SIZE, REGISTRY, TYPESPEC, Aliases,
    BaseMeta, Decorator, DecoratorMeta, DecoratorPrecedence, Edges, Empty, StructuredMeta,
    StructuredUnion, Type, TypeMeta, TypeRegistry, Union, UnionMeta, detect, resolve
)
