import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport (
    CompositeType, compute_hash, ElementType, resolve_dtype, shared_registry
)


##########################
####    SUPERTYPES    ####
##########################


cdef class IntegerType(ElementType):
    """Integer supertype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        super(IntegerType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            atomic_type=int,
            numpy_type=np.dtype(np.int64),
            pandas_type=pd.Int64Dtype(),
            slug="nullable[int]" if nullable else "int",
            supertype=None,
            subtypes=None  # lazy-loaded
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # min/max representable values
        self.min = -np.inf
        self.max = np.inf

    @property
    def subtypes(self) -> CompositeType:
        # cached
        if self._subtypes is not None:
            return self._subtypes

        # uncached
        subtype_categories = (
            SignedIntegerType, Int8Type, Int16Type, Int32Type, Int64Type,
            UnsignedIntegerType, UInt8Type, UInt16Type, UInt32Type, UInt64Type
        )
        subtypes = {self} | {
            t.instance(
                sparse=self.sparse,
                categorical=self.categorical,
                nullable=self.nullable
            )
            for t in subtype_categories
        }
        if not self.nullable:
            subtypes |= {
                t.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
                for t in subtype_categories + (self.__class__,)
            }

        self._subtypes = CompositeType(subtypes, immutable=True)
        return self._subtypes

    @classmethod
    def instance(
        cls,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ) -> IntegerType:
        """Flyweight constructor."""
        # hash arguments
        cdef long long _hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=cls
        )

        # get previous flyweight, if one exists
        cdef IntegerType result = shared_registry.get(_hash, None)

        if result is None:
            # construct new flyweight
            result = cls(
                sparse=sparse,
                categorical=categorical,
                nullable=nullable
            )
    
            # add flyweight to registry
            shared_registry[_hash] = result

        # return flyweight
        return result

    def __repr__(self) -> str:
        return (
            f"{self.__class__.__name__}("
            f"sparse={self.sparse}, "
            f"categorical={self.categorical}, "
            f"nullable={self.nullable}"
            f")"
        )


cdef class SignedIntegerType(IntegerType):
    """Signed integer supertype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        super(IntegerType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            atomic_type=None,
            numpy_type=np.dtype(np.int64),
            pandas_type=pd.Int64Dtype(),
            slug="nullable[signed int]" if nullable else "signed int",
            supertype=None,  # lazy-loaded
            subtypes=None  # lazy-loaded
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # min/max representable values
        self.min = -2**63
        self.max = 2**63 - 1

    @property
    def subtypes(self) -> CompositeType:
        # cached
        if self._subtypes is not None:
            return self._subtypes

        # uncached
        subtype_categories = (Int8Type, Int16Type, Int32Type, Int64Type)
        subtypes = {self} | {
            t.instance(
                sparse=self.sparse,
                categorical=self.categorical,
                nullable=self.nullable
            )
            for t in subtype_categories
        }
        if not self.nullable:
            subtypes |= {
                t.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
                for t in subtype_categories + (self.__class__,)
            }

        self._subtypes = CompositeType(subtypes, immutable=True)
        return self._subtypes

    @property
    def supertype(self) -> IntegerType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = IntegerType.instance(
            sparse=self.sparse,
            categorical=self.categorical,
            nullable=self.nullable
        )
        return self._supertype

    @classmethod
    def instance(
        cls,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ) -> SignedIntegerType:
        """Flyweight constructor."""
        # hash arguments
        cdef long long _hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=cls
        )

        # get previous flyweight, if one exists
        cdef SignedIntegerType result = shared_registry.get(_hash, None)

        if result is None:
            # construct new flyweight
            result = cls(
                sparse=sparse,
                categorical=categorical,
                nullable=nullable
            )
    
            # add flyweight to registry
            shared_registry[_hash] = result

        # return flyweight
        return result


cdef class UnsignedIntegerType(IntegerType):
    """Unsigned integer supertype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        super(IntegerType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            atomic_type=None,
            numpy_type=np.dtype(np.uint64),
            pandas_type=pd.UInt64Dtype(),
            slug="nullable[unsigned int]" if nullable else "unsigned int",
            supertype=None,  # lazy-loaded
            subtypes=None  # lazy-loaded
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # min/max representable values
        self.min = 0
        self.max = 2**64 - 1

    @property
    def subtypes(self) -> CompositeType:
        # cached
        if self._subtypes is not None:
            return self._subtypes

        # uncached
        subtype_categories = (UInt8Type, UInt16Type, UInt32Type, UInt64Type)
        subtypes = {self} | {
            t.instance(
                sparse=self.sparse,
                categorical=self.categorical,
                nullable=self.nullable
            )
            for t in subtype_categories
        }
        if not self.nullable:
            subtypes |= {
                t.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
                for t in subtype_categories + (self.__class__,)
            }

        self._subtypes = CompositeType(subtypes, immutable=True)
        return self._subtypes

    @property
    def supertype(self) -> IntegerType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = IntegerType.instance(
            sparse=self.sparse,
            categorical=self.categorical,
            nullable=self.nullable
        )
        return self._supertype

    @classmethod
    def instance(
        cls,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ) -> UnsignedIntegerType:
        """Flyweight constructor."""
        # hash arguments
        cdef long long _hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=cls
        )

        # get previous flyweight, if one exists
        cdef UnsignedIntegerType result = shared_registry.get(_hash, None)

        if result is None:
            # construct new flyweight
            result = cls(
                sparse=sparse,
                categorical=categorical,
                nullable=nullable
            )
    
            # add flyweight to registry
            shared_registry[_hash] = result

        # return flyweight
        return result


########################
####    SUBTYPES    ####
########################


cdef class Int8Type(SignedIntegerType):
    """8-bit integer subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        super(IntegerType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            atomic_type=np.int8,
            numpy_type=np.dtype(np.int8),
            pandas_type=pd.Int8Dtype(),
            slug="nullable[int8]" if nullable else "int8",
            supertype=None,  # lazy-loaded
            subtypes=None  # lazy-loaded
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # min/max representable values
        self.min = -2**7
        self.max = 2**7 - 1

    @property
    def subtypes(self) -> CompositeType:
        # cached
        if self._subtypes is not None:
            return self._subtypes

        # uncached
        subtypes = {self}
        if not self.nullable:
            self.subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
            }
        self._subtypes = CompositeType(subtypes, immutable=True)
        return self._subtypes

    @property
    def supertype(self) -> SignedIntegerType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = SignedIntegerType.instance(
            sparse=self.sparse,
            categorical=self.categorical,
            nullable=self.nullable
        )
        return self._supertype


cdef class Int16Type(SignedIntegerType):
    """16-bit integer subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        super(IntegerType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            atomic_type=np.int16,
            numpy_type=np.dtype(np.int16),
            pandas_type=pd.Int16Dtype(),
            slug="nullable[int16]" if nullable else "int16",
            supertype=None,  # lazy-loaded
            subtypes=None  # lazy-loaded
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # min/max representable values
        self.min = -2**15
        self.max = 2**15 - 1

    @property
    def subtypes(self) -> CompositeType:
        # cached
        if self._subtypes is not None:
            return self._subtypes

        # uncached
        subtypes = {self}
        if not self.nullable:
            self.subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
            }
        self._subtypes = CompositeType(subtypes, immutable=True)
        return self._subtypes

    @property
    def supertype(self) -> SignedIntegerType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = SignedIntegerType.instance(
            sparse=self.sparse,
            categorical=self.categorical,
            nullable=self.nullable
        )
        return self._supertype


cdef class Int32Type(SignedIntegerType):
    """32-bit integer subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        super(IntegerType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            atomic_type=np.int32,
            numpy_type=np.dtype(np.int32),
            pandas_type=pd.Int32Dtype(),
            slug="nullable[int32]" if nullable else "int32",
            supertype=None,  # lazy-loaded
            subtypes=None  # lazy-loaded
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # min/max representable values
        self.min = -2**31
        self.max = 2**31 - 1

    @property
    def subtypes(self) -> CompositeType:
        # cached
        if self._subtypes is not None:
            return self._subtypes

        # uncached
        subtypes = {self}
        if not self.nullable:
            self.subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
            }
        self._subtypes = CompositeType(subtypes, immutable=True)
        return self._subtypes

    @property
    def supertype(self) -> SignedIntegerType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = SignedIntegerType.instance(
            sparse=self.sparse,
            categorical=self.categorical,
            nullable=self.nullable
        )
        return self._supertype


cdef class Int64Type(SignedIntegerType):
    """64-bit integer subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        super(IntegerType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            atomic_type=np.int64,
            numpy_type=np.dtype(np.int64),
            pandas_type=pd.Int64Dtype(),
            slug="nullable[int64]" if nullable else "int64",
            supertype=None,  # lazy-loaded
            subtypes=None  # lazy-loaded
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # min/max representable values
        self.min = -2**63
        self.max = 2**63 - 1

    @property
    def subtypes(self) -> CompositeType:
        # cached
        if self._subtypes is not None:
            return self._subtypes

        # uncached
        subtypes = {self}
        if not self.nullable:
            self.subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
            }
        self._subtypes = CompositeType(subtypes, immutable=True)
        return self._subtypes

    @property
    def supertype(self) -> SignedIntegerType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = SignedIntegerType.instance(
            sparse=self.sparse,
            categorical=self.categorical,
            nullable=self.nullable
        )
        return self._supertype


cdef class UInt8Type(UnsignedIntegerType):
    """8-bit unsigned integer subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        super(IntegerType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            atomic_type=np.uint8,
            numpy_type=np.dtype(np.uint8),
            pandas_type=pd.UInt8Dtype(),
            slug="nullable[uint8]" if nullable else "uint8",
            supertype=None,  # lazy-loaded
            subtypes=None  # lazy-loaded
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # min/max representable values
        self.min = 0
        self.max = 2**8 - 1

    @property
    def subtypes(self) -> CompositeType:
        # cached
        if self._subtypes is not None:
            return self._subtypes

        # uncached
        subtypes = {self}
        if not self.nullable:
            self.subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
            }
        self._subtypes = CompositeType(subtypes, immutable=True)
        return self._subtypes

    @property
    def supertype(self) -> UnsignedIntegerType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = UnsignedIntegerType.instance(
            sparse=self.sparse,
            categorical=self.categorical,
            nullable=self.nullable
        )
        return self._supertype


cdef class UInt16Type(UnsignedIntegerType):
    """16-bit unsigned integer subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        super(IntegerType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            atomic_type=np.uint16,
            numpy_type=np.dtype(np.uint16),
            pandas_type=pd.UInt16Dtype(),
            slug="nullable[uint16]" if nullable else "uint16",
            supertype=None,  # lazy-loaded
            subtypes=None  # lazy-loaded
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # min/max representable values
        self.min = 0
        self.max = 2**16 - 1

    @property
    def subtypes(self) -> CompositeType:
        # cached
        if self._subtypes is not None:
            return self._subtypes

        # uncached
        subtypes = {self}
        if not self.nullable:
            self.subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
            }
        self._subtypes = CompositeType(subtypes, immutable=True)
        return self._subtypes

    @property
    def supertype(self) -> UnsignedIntegerType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = UnsignedIntegerType.instance(
            sparse=self.sparse,
            categorical=self.categorical,
            nullable=self.nullable
        )
        return self._supertype


cdef class UInt32Type(UnsignedIntegerType):
    """32-bit unsigned integer subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        super(IntegerType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            atomic_type=np.uint32,
            numpy_type=np.dtype(np.uint32),
            pandas_type=pd.UInt32Dtype(),
            slug="nullable[uint32]" if nullable else "uint32",
            supertype=None,  # lazy-loaded
            subtypes=None  # lazy-loaded
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # min/max representable values
        self.min = 0
        self.max = 2**32 - 1

    @property
    def subtypes(self) -> CompositeType:
        # cached
        if self._subtypes is not None:
            return self._subtypes

        # uncached
        subtypes = {self}
        if not self.nullable:
            self.subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
            }
        self._subtypes = CompositeType(subtypes, immutable=True)
        return self._subtypes

    @property
    def supertype(self) -> UnsignedIntegerType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = UnsignedIntegerType.instance(
            sparse=self.sparse,
            categorical=self.categorical,
            nullable=self.nullable
        )
        return self._supertype


cdef class UInt64Type(UnsignedIntegerType):
    """32-bit unsigned integer subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        super(IntegerType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            atomic_type=np.uint64,
            numpy_type=np.dtype(np.uint64),
            pandas_type=pd.UInt64Dtype(),
            slug="nullable[uint64]" if nullable else "uint64",
            supertype=None,  # lazy-loaded
            subtypes=None  # lazy-loaded
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # min/max representable values
        self.min = 0
        self.max = 2**64 - 1

    @property
    def subtypes(self) -> CompositeType:
        # cached
        if self._subtypes is not None:
            return self._subtypes

        # uncached
        subtypes = {self}
        if not self.nullable:
            self.subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
            }
        self._subtypes = CompositeType(subtypes, immutable=True)
        return self._subtypes

    @property
    def supertype(self) -> UnsignedIntegerType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = UnsignedIntegerType.instance(
            sparse=self.sparse,
            categorical=self.categorical,
            nullable=self.nullable
        )
        return self._supertype
