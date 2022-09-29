import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType


# TODO: IntegerType should be nullable by default


##########################
####    SUPERTYPES    ####
##########################


cdef class IntegerType(ElementType):
    """Integer supertype"""

    def __init__(
        self,
        bint is_categorical = False,
        bint is_sparse = False,
        bint is_nullable = False
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_nullable = is_nullable
        self.supertype = None
        self.subtypes = (
            Int8Type, Int16Type, Int32Type, Int64Type, UInt8Type, UInt16Type,
            UInt32Type, UInt64Type
        )
        self.atomic_type = int
        self.extension_type = None
        self.min = -np.inf
        self.max = np.inf
        self.slug = "int"

    def __repr__(self) -> str:
        return (
            f"{self.__class__.__name__}("
            f"is_categorical={self.is_categorical}, "
            f"is_sparse={self.is_sparse}, "
            f"is_nullable={self.is_nullable}"
            f")"
        )

    def __str__(self) -> str:
        cdef str result = self.slug

        # append extensions
        if self.is_nullable:
            result = f"nullable[{result}]"
        if self.is_categorical:
            result = f"categorical[{result}]"
        if self.is_sparse:
            result = f"sparse[{result}]"

        return result


cdef class SignedIntegerType(IntegerType):
    """Signed integer supertype"""

    def __init__(
        self,
        bint is_categorical = False,
        bint is_sparse = False,
        bint is_nullable = False
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_nullable = is_nullable
        self.supertype = IntegerType
        self.subtypes = (Int8Type, Int16Type, Int32Type, Int64Type)
        self.atomic_type = None
        self.extension_type = None
        self.min = -2**63
        self.max = 2**63 - 1
        self.slug = "signed"


cdef class UnsignedIntegerType(IntegerType):
    """Unsigned integer supertype"""

    def __init__(
        self,
        bint is_categorical = False,
        bint is_sparse = False,
        bint is_nullable = False
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_nullable = is_nullable
        self.supertype = IntegerType
        self.subtypes = (UInt8Type, UInt16Type, UInt32Type, UInt64Type)
        self.atomic_type = None
        self.extension_type = None
        self.min = 0
        self.max = 2**64 - 1
        self.slug = "unsigned"


########################
####    SUBTYPES    ####
########################


cdef class Int8Type(SignedIntegerType):
    """8-bit integer subtype"""

    def __init__(
        self,
        bint is_categorical = False,
        bint is_sparse = False,
        bint is_nullable = False
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_nullable = is_nullable
        self.supertype = SignedIntegerType
        self.subtypes = ()
        self.atomic_type = np.int8
        self.extension_type = pd.Int8Dtype()
        self.min = -2**7
        self.max = 2**7 - 1
        self.slug = "int8"


cdef class Int16Type(SignedIntegerType):
    """16-bit integer subtype"""

    def __init__(
        self,
        bint is_categorical = False,
        bint is_sparse = False,
        bint is_nullable = False
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_nullable = is_nullable
        self.supertype = SignedIntegerType
        self.subtypes = ()
        self.atomic_type = np.int16
        self.extension_type = pd.Int16Dtype()
        self.min = -2**15
        self.max = 2**15 - 1
        self.slug = "int16"


cdef class Int32Type(SignedIntegerType):
    """32-bit integer subtype"""

    def __init__(
        self,
        bint is_categorical = False,
        bint is_sparse = False,
        bint is_nullable = False
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_nullable = is_nullable
        self.supertype = SignedIntegerType
        self.subtypes = ()
        self.atomic_type = np.int32
        self.extension_type = pd.Int32Dtype()
        self.min = -2**31
        self.max = 2**31 - 1
        self.slug = "int32"


cdef class Int64Type(SignedIntegerType):
    """64-bit integer subtype"""

    def __init__(
        self,
        bint is_categorical = False,
        bint is_sparse = False,
        bint is_nullable = False
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_nullable = is_nullable
        self.supertype = SignedIntegerType
        self.subtypes = ()
        self.atomic_type = np.int64
        self.extension_type = pd.Int64Dtype()
        self.min = -2**63
        self.max = 2**63 - 1
        self.slug = "int64"


cdef class UInt8Type(UnsignedIntegerType):
    """8-bit unsigned integer subtype"""

    def __init__(
        self,
        bint is_categorical = False,
        bint is_sparse = False,
        bint is_nullable = False
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_nullable = is_nullable
        self.supertype = UnsignedIntegerType
        self.subtypes = ()
        self.atomic_type = np.uint8
        self.extension_type = pd.UInt8Dtype()
        self.min = 0
        self.max = 2**8 - 1
        self.slug = "uint8"


cdef class UInt16Type(UnsignedIntegerType):
    """16-bit unsigned integer subtype"""

    def __init__(
        self,
        bint is_categorical = False,
        bint is_sparse = False,
        bint is_nullable = False
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_nullable = is_nullable
        self.supertype = UnsignedIntegerType
        self.subtypes = ()
        self.atomic_type = np.uint16
        self.extension_type = pd.UInt16Dtype()
        self.min = 0
        self.max = 2**16 - 1
        self.slug = "uint16"


cdef class UInt32Type(UnsignedIntegerType):
    """32-bit unsigned integer subtype"""

    def __init__(
        self,
        bint is_categorical = False,
        bint is_sparse = False,
        bint is_nullable = False
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_nullable = is_nullable
        self.supertype = UnsignedIntegerType
        self.subtypes = ()
        self.atomic_type = np.uint32
        self.extension_type = pd.UInt32Dtype()
        self.min = 0
        self.max = 2**32 - 1
        self.slug = "uint32"


cdef class UInt64Type(UnsignedIntegerType):
    """32-bit unsigned integer subtype"""

    def __init__(
        self,
        bint is_categorical = False,
        bint is_sparse = False,
        bint is_nullable = False
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_nullable = is_nullable
        self.supertype = UnsignedIntegerType
        self.subtypes = ()
        self.atomic_type = np.uint64
        self.extension_type = pd.UInt64Dtype()
        self.min = 0
        self.max = 2**64 - 1
        self.slug = "uint64"
