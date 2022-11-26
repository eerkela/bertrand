import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE, PYARROW_INSTALLED

from .base cimport compute_hash, ElementType, resolve_dtype, shared_registry


##########################
####    SUPERTYPES    ####
##########################


cdef class StringType(ElementType):
    """string supertype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        str storage = None
    ):
        # sort out string storage backend
        self.is_default = storage is None
        if self.is_default:
            pandas_type = DEFAULT_STRING_DTYPE
            slug = "string"
            self.storage = DEFAULT_STRING_DTYPE.storage
        else:
            pandas_type = pd.StringDtype(storage)
            slug = f"string[{storage}]"
            self.storage = storage

        # pass to ElementType constructor
        super(StringType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=str,
            numpy_type=np.dtype(str),
            pandas_type=pandas_type,
            slug=slug,
            supertype=None,
            subtypes=None  # lazy-loaded
        )

        # compute hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__,
            unit=None,
            step_size=1,
            storage=storage
        )

    @property
    def subtypes(self) -> frozenset:
        # cached
        if self._subtypes is not None:
            return self._subtypes

        # uncached
        subtypes = {self}
        if self.is_default:
            backends = ["python"]
            if PYARROW_INSTALLED:
                backends.append("pyarrow")

            subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    storage=storage
                )
                for storage in backends
            }

        self._subtypes = frozenset(subtypes)
        return self._subtypes

    @classmethod
    def instance(
        cls,
        bint sparse = False,
        bint categorical = False,
        str storage = None
    ) -> StringType:
        """Flyweight constructor."""
        # hash arguments
        cdef long long _hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=cls,
            unit=None,
            step_size=1,
            storage=storage
        )

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

    def __repr__(self) -> str:
        cdef str storage

        if self.is_default:
            storage = None
        else:
            storage = repr(self.storage)

        return (
            f"{self.__class__.__name__}("
            f"sparse={self.sparse}, "
            f"categorical={self.categorical}, "
            f"storage={storage}"
            f")"
        )
