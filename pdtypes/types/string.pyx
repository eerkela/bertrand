import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE, PYARROW_INSTALLED

from .base cimport base_slugs, ElementType, resolve_dtype, shared_registry


cdef str generate_slug(
    type base_type,
    str storage,
    bint sparse,
    bint categorical
):
    cdef str slug = base_slugs[base_type]

    if storage:
        slug = f"{slug}[{storage}]"
    if categorical:
        slug = f"categorical[{slug}]"
    if sparse:
        slug = f"sparse[{slug}]"

    return slug


##########################
####    SUPERTYPES    ####
##########################


cdef class StringType(ElementType):
    """string supertype"""

    def __init__(
        self,
        str storage = None,
        bint sparse = False,
        bint categorical = False
    ):
        # sort out string storage backend
        self.is_default = storage is None
        if self.is_default:
            pandas_type = DEFAULT_STRING_DTYPE
            self.storage = DEFAULT_STRING_DTYPE.storage
        else:
            pandas_type = pd.StringDtype(storage)
            self.storage = storage

        # pass to ElementType constructor
        super(StringType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=str,
            numpy_type=np.dtype(str),
            pandas_type=pandas_type,
            na_value=pd.NA,
            itemsize=None,
            slug=generate_slug(
                base_type=type(self),
                storage=storage,
                sparse=sparse,
                categorical=categorical
            )
        )

    @classmethod
    def instance(
        cls,
        str storage = None,
        bint sparse = False,
        bint categorical = False
    ) -> StringType:
        """Flyweight constructor."""
        # generate slug
        cdef str slug = generate_slug(
            base_type=cls,
            storage=storage,
            sparse=sparse,
            categorical=categorical
        )

        # compute hash
        cdef long long _hash = hash(slug)

        # get previous flyweight, if one exists
        cdef StringType result = shared_registry.get(_hash, None)

        if result is None:
            # construct new flyweight
            result = cls(
                sparse=sparse,
                categorical=categorical,
                storage=storage
            )
    
            # add flyweight to registry
            shared_registry[_hash] = result

        # return flyweight
        return result

    @property
    def subtypes(self) -> frozenset:
        if self._subtypes is None:
            self._subtypes = frozenset({self})
            if self.is_default:
                backends = ["python"]
                if PYARROW_INSTALLED:
                    backends.append("pyarrow")
                self._subtypes |= {
                    type(self).instance(
                        sparse=self.sparse,
                        categorical=self.categorical,
                        storage=storage
                    )
                    for storage in backends
                }
        return self._subtypes

    def __repr__(self) -> str:
        cdef str storage = None if self.is_default else repr(self.storage)

        return (
            f"{type(self).__name__}("
            f"storage={storage}, "
            f"sparse={self.sparse}, "
            f"categorical={self.categorical}"
            f")"
        )
