import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType, object_registry, resolve_dtype


cdef str generate_slug(
    type atomic_type,
    bint sparse,
    bint categorical
):
    """Return a unique slug string associated with the given `base_type`,
    accounting for `atomic_type`, `sparse`, and `categorical` flags.
    """
    cdef str slug = atomic_type.__name__

    if categorical:
        slug = f"categorical[{slug}]"
    if sparse:
        slug = f"sparse[{slug}]"

    return slug


##########################
####    SUPERTYPES    ####
##########################


cdef class ObjectType(ElementType):
    """Object supertype"""

    def __init__(
        self,
        type atomic_type = object,
        bint sparse = False,
        bint categorical = False
    ):
        super(ObjectType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=atomic_type,
            numpy_type=np.dtype("O"),
            pandas_type=None,
            slug=generate_slug(
                atomic_type=atomic_type,
                sparse=sparse,
                categorical=categorical
            )
        )
        self._subtypes = frozenset({self})

    @classmethod
    def instance(
        cls,
        type atomic_type = object,
        bint sparse = False,
        bint categorical = False
    ):
        """Flyweight constructor."""
        # generate slug
        cdef str slug = generate_slug(
            atomic_type=atomic_type,
            sparse=sparse,
            categorical=categorical
        )

        # compute hash
        cdef long long _hash = hash(slug)

        # get previous flyweight, if one exists
        cdef ObjectType result = object_registry.get(_hash, None)

        if result is None:
            # construct new flyweight
            result = cls(
                atomic_type=atomic_type,
                sparse=sparse,
                categorical=categorical,
            )
    
            # add flyweight to registry
            object_registry[_hash] = result

        # return flyweight
        return result

    def __contains__(self, other) -> bool:
        """Test whether the given type specifier is a subtype of this
        ElementType.
        """
        other = resolve_dtype(other)
        return (
            isinstance(other, self.__class__) and
            issubclass(other.atomic_type, self.atomic_type)
        )

    def __repr__(self) -> str:
        return (
            f"{self.__class__.__name__}("
            f"atomic_type={self.atomic_type}, "
            f"sparse={self.sparse}, "
            f"categorical={self.categorical}"
            f")"
        )
