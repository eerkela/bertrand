import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport (
    compute_hash, ElementType, resolve_dtype, shared_registry
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
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = nullable
        self.supertype = None
        self.atomic_type = int
        self.numpy_type = None
        self.pandas_type = None
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # generate slug
        self.slug = "int"
        if self.nullable:
            self.slug = f"nullable[{self.slug}]"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        subtype_categories = (
            SignedIntegerType, Int8Type, Int16Type, Int32Type, Int64Type,
            UnsignedIntegerType, UInt8Type, UInt16Type, UInt32Type, UInt64Type
        )
        self.subtypes = frozenset((self,))
        self.subtypes |= {
            t.instance(
                sparse=self.sparse,
                categorical=self.categorical,
                nullable=self.nullable
            )
            for t in subtype_categories
        }
        if not self.nullable:
            self.subtypes |= {
                t.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
                for t in subtype_categories + (self.__class__,)
            }

        # min/max representable values
        self.min = -np.inf
        self.max = np.inf

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
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = nullable
        self.supertype = IntegerType
        self.atomic_type = None
        self.numpy_type = None
        self.pandas_type = None
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # generate slug
        self.slug = "signed int"
        if self.nullable:
            self.slug = f"nullable[{self.slug}]"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        subtype_categories = (Int8Type, Int16Type, Int32Type, Int64Type)
        self.subtypes = frozenset((self,))
        self.subtypes |= {
            t.instance(
                sparse=self.sparse,
                categorical=self.categorical,
                nullable=self.nullable
            )
            for t in subtype_categories
        }
        if not self.nullable:
            self.subtypes |= {
                t.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
                for t in subtype_categories + (self.__class__,)
            }

        # min/max representable values
        self.min = -2**63
        self.max = 2**63 - 1

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
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = nullable
        self.supertype = IntegerType
        self.atomic_type = None
        self.numpy_type = None
        self.pandas_type = None
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # generate slug
        self.slug = "unsigned int"
        if self.nullable:
            self.slug = f"nullable[{self.slug}]"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        subtype_categories = (UInt8Type, UInt16Type, UInt32Type, UInt64Type)
        self.subtypes = frozenset((self,))
        self.subtypes |= {
            t.instance(
                sparse=self.sparse,
                categorical=self.categorical,
                nullable=self.nullable
            )
            for t in subtype_categories
        }
        if not self.nullable:
            self.subtypes |= {
                t.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
                for t in subtype_categories + (self.__class__,)
            }

        # min/max representable values
        self.min = 0
        self.max = 2**64 - 1

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
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = nullable
        self.supertype = SignedIntegerType
        self.atomic_type = np.int8
        self.numpy_type = np.dtype(np.int8)
        self.pandas_type = pd.Int8Dtype()
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # generate slug
        self.slug = "int8"
        if self.nullable:
            self.slug = f"nullable[{self.slug}]"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))
        if not self.nullable:
            self.subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
            }

        # min/max representable values
        self.min = -2**7
        self.max = 2**7 - 1


cdef class Int16Type(SignedIntegerType):
    """16-bit integer subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = nullable
        self.supertype = SignedIntegerType
        self.atomic_type = np.int16
        self.numpy_type = np.dtype(np.int16)
        self.pandas_type = pd.Int16Dtype()
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # generate slug
        self.slug = "int16"
        if self.nullable:
            self.slug = f"nullable[{self.slug}]"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))
        if not self.nullable:
            self.subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
            }

        # min/max representable values
        self.min = -2**15
        self.max = 2**15 - 1


cdef class Int32Type(SignedIntegerType):
    """32-bit integer subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = nullable
        self.supertype = SignedIntegerType
        self.atomic_type = np.int32
        self.numpy_type = np.dtype(np.int32)
        self.pandas_type = pd.Int32Dtype()
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # generate slug
        self.slug = "int32"
        if self.nullable:
            self.slug = f"nullable[{self.slug}]"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))
        if not self.nullable:
            self.subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
            }

        # min/max representable values
        self.min = -2**31
        self.max = 2**31 - 1


cdef class Int64Type(SignedIntegerType):
    """64-bit integer subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = nullable
        self.supertype = SignedIntegerType
        self.atomic_type = np.int64
        self.numpy_type = np.dtype(np.int64)
        self.pandas_type = pd.Int64Dtype()
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # generate slug
        self.slug = "int64"
        if self.nullable:
            self.slug = f"nullable[{self.slug}]"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))
        if not self.nullable:
            self.subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
            }

        # min/max representable values
        self.min = -2**63
        self.max = 2**63 - 1


cdef class UInt8Type(UnsignedIntegerType):
    """8-bit unsigned integer subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = nullable
        self.supertype = UnsignedIntegerType
        self.atomic_type = np.uint8
        self.numpy_type = np.dtype(np.uint8)
        self.pandas_type = pd.UInt8Dtype()
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # generate slug
        self.slug = "uint8"
        if self.nullable:
            self.slug = f"nullable[{self.slug}]"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))
        if not self.nullable:
            self.subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
            }

        # min/max representable values
        self.min = 0
        self.max = 2**8 - 1


cdef class UInt16Type(UnsignedIntegerType):
    """16-bit unsigned integer subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = nullable
        self.supertype = UnsignedIntegerType
        self.atomic_type = np.uint16
        self.numpy_type = np.dtype(np.uint16)
        self.pandas_type = pd.UInt16Dtype()
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # generate slug
        self.slug = "uint16"
        if self.nullable:
            self.slug = f"nullable[{self.slug}]"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))
        if not self.nullable:
            self.subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
            }

        # min/max representable values
        self.min = 0
        self.max = 2**16 - 1


cdef class UInt32Type(UnsignedIntegerType):
    """32-bit unsigned integer subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = nullable
        self.supertype = UnsignedIntegerType
        self.atomic_type = np.uint32
        self.numpy_type = np.dtype(np.uint32)
        self.pandas_type = pd.UInt32Dtype()
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # generate slug
        self.slug = "uint32"
        if self.nullable:
            self.slug = f"nullable[{self.slug}]"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))
        if not self.nullable:
            self.subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
            }

        # min/max representable values
        self.min = 0
        self.max = 2**32 - 1


cdef class UInt64Type(UnsignedIntegerType):
    """32-bit unsigned integer subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = nullable
        self.supertype = UnsignedIntegerType
        self.atomic_type = np.uint64
        self.numpy_type = np.dtype(np.uint64)
        self.pandas_type = pd.UInt64Dtype()
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # generate slug
        self.slug = "uint64"
        if self.nullable:
            self.slug = f"nullable[{self.slug}]"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))
        if not self.nullable:
            self.subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
            }

        # min/max representable values
        self.min = 0
        self.max = 2**64 - 1
