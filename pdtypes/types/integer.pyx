import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport base_slugs, ElementType, resolve_dtype, shared_registry


# TODO: add is_signed, is_unsigned attributes


# TODO: these are added to alias map along with everything else.  Resolve from
# the alias map until the value is no longer in it.
platform_specific = {
    # C char
    "char": np.dtype(np.byte),
    "signed char": "char",
    "byte": "char",
    "b": "char",

    # C short
    "short": np.dtype(np.short),
    "signed short int": "short",
    "signed short": "short",
    "short int": "short",
    "h": "short",

    # C int
    "intc": np.dtype(np.intc),
    "signed intc": "intc",

    # C long
    "long": np.dtype(np.int_),
    "long int": "long",
    "signed long": "long",
    "signed long int": "long",
    "l": "long",

    # C long long
    "long long": np.dtype(np.longlong),
    "longlong": "long long",
    "long long int": "long long",
    "signed long long": "long long",
    "signed longlong": "long long",
    "signed long long int": "long long",
    "q": "long long",

    # C ssize_t
    "ssize_t": np.dtype(np.intp),
    "intp": "ssize_t",
    "int0": "ssize_t",
    "p": "ssize_t",

    # C unsigned char
    "unsigned char": np.dtype(np.ubyte),
    "unsigned byte": "unsigned char",
    "ubyte": "unsigned char",
    "B": "unsigned char",

    # C unsigned short
    "unsigned short": np.dtype(np.ushort),
    "unsigned short int": "unsigned short",
    "ushort": "unsigned short",
    "H": "unsigned short",

    # C unsigned int
    "unsigned intc": np.dtype(np.uintc),
    "uintc": "unsigned intc",
    "I": "unsigned intc",

    # C unsigned long
    "unsigned long": np.dtype(np.uint),
    "unsigned long int": "unsigned long",
    "ulong": "unsigned long",
    "L": "unsigned long",

    # C unsigned long long
    "unsigned long long": np.dtype(np.ulonglong),
    "unsigned longlong": "unsigned long long",
    "unsigned long long int": "unsigned long long",
    "ulonglong": "unsigned long long",
    "Q": "unsigned long long",

    # C size_t
    "size_t": np.dtype(np.uintp),
    "uintp": "size_t",
    "uint0": "size_t",
    "P": "size_t",
}
# while key in aliases:
#     key = aliases[key]




cdef str generate_slug(
    type base_type,
    bint sparse,
    bint categorical,
    bint nullable
):
    """Return a unique slug string associated with the given `base_type`,
    accounting for `sparse`, `categorical`, and `nullable` flags.
    """
    cdef str slug = base_slugs[base_type]

    if nullable:
        slug = f"nullable[{slug}]"
    if categorical:
        slug = f"categorical[{slug}]"
    if sparse:
        slug = f"sparse[{slug}]"

    return slug


##########################
####    SUPERTYPES    ####
##########################


cdef class IntegerType(ElementType):
    """Integer supertype"""

    _base_slug = "int"
    aliases = {
        # type
        int: {},
        np.integer: {"backend": "numpy"},

        # string
        "int": {},
        "integer": {},
    }

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
            na_value=pd.NA,
            itemsize=None,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical,
                nullable=nullable
            )
        )

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
        # generate slug
        cdef str slug = generate_slug(
            base_type=cls,
            sparse=sparse,
            categorical=categorical,
            nullable=nullable
        )

        # compute hash
        cdef long long _hash = hash(slug)

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

    @property
    def subtypes(self) -> frozenset:
        if self._subtypes is None:
            self._subtypes = frozenset({self})
            self._subtypes |= {
                t.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=self.nullable
                )
                for t in (
                    SignedIntegerType, Int8Type, Int16Type, Int32Type,
                    Int64Type, UnsignedIntegerType, UInt8Type, UInt16Type,
                    UInt32Type, UInt64Type
                )
            }
            if not self.nullable:
                self._subtypes |= {
                    type(x).instance(
                        sparse=self.sparse,
                        categorical=self.categorical,
                        nullable=True
                    ) for x in self._subtypes
                }
        return self._subtypes

    def __repr__(self) -> str:
        return (
            f"{type(self).__name__}("
            f"sparse={self.sparse}, "
            f"categorical={self.categorical}, "
            f"nullable={self.nullable}"
            f")"
        )


cdef class SignedIntegerType(IntegerType):
    """Signed integer supertype"""

    _base_slug = "signed"
    aliases = {
        # type
        np.signedinteger: {"backend": "numpy"},

        # string
        "signed": {},
        "signed int": {},
        "signed integer": {},
        "i": {},
    }

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
            na_value=pd.NA,
            itemsize=None,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical,
                nullable=nullable
            )
        )

        # min/max representable values
        self.min = -2**63
        self.max = 2**63 - 1

    @property
    def subtypes(self) -> frozenset:
        if self._subtypes is None:
            self._subtypes = frozenset({self})
            self._subtypes |= {
                t.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=self.nullable
                )
                for t in (Int8Type, Int16Type, Int32Type, Int64Type)
            }
            if not self.nullable:
                self._subtypes |= {
                    type(x).instance(
                        sparse=self.sparse,
                        categorical=self.categorical,
                        nullable=True
                    ) for x in self._subtypes
                }
        return self._subtypes

    @property
    def supertype(self) -> IntegerType:
        if self._supertype is None:
            self._supertype = IntegerType.instance(
                sparse=self.sparse,
                categorical=self.categorical,
                nullable=self.nullable
            )
        return self._supertype


cdef class UnsignedIntegerType(IntegerType):
    """Unsigned integer supertype"""

    _base_slug = "unsigned"
    aliases = {
        # type
        np.unsignedinteger: {"backend": "numpy"},

        # string
        "unsigned": {},
        "unsigned int": {},
        "unsigned integer": {},
        "uint": {},
        "u": {},
    }

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
            na_value=pd.NA,
            itemsize=None,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical,
                nullable=nullable
            )
        )

        # min/max representable values
        self.min = 0
        self.max = 2**64 - 1

    @property
    def subtypes(self) -> frozenset:
        if self._subtypes is None:
            self._subtypes = frozenset({self})
            self._subtypes |= {
                t.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=self.nullable
                )
                for t in (UInt8Type, UInt16Type, UInt32Type, UInt64Type)
            }
            if not self.nullable:
                self._subtypes |= {
                    type(x).instance(
                        sparse=self.sparse,
                        categorical=self.categorical,
                        nullable=True
                    ) for x in self._subtypes
                }
        return self._subtypes

    @property
    def supertype(self) -> IntegerType:
        if self._supertype is None:
            self._supertype = IntegerType.instance(
                sparse=self.sparse,
                categorical=self.categorical,
                nullable=self.nullable
            )
        return self._supertype


########################
####    SUBTYPES    ####
########################


cdef class Int8Type(SignedIntegerType):
    """8-bit integer subtype"""

    _base_slug = "int8"
    aliases = {
        # type
        np.int8: {"backend": "numpy"},

        # dtype
        np.dtype(np.int8): {"backend": "numpy"},
        pd.Int8Dtype(): {"backend": "pandas"},

        # string
        "int8": {},
        "i1": {},
        "Int8": {"backend": "pandas"},
    }

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
            na_value=pd.NA,
            itemsize=1,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical,
                nullable=nullable
            )
        )

        # min/max representable values
        self.min = -2**7
        self.max = 2**7 - 1

    @property
    def subtypes(self) -> frozenset:
        if self._subtypes is None:
            self._subtypes = frozenset({self})
            if not self.nullable:
                self._subtypes |= {
                    type(x).instance(
                        sparse=self.sparse,
                        categorical=self.categorical,
                        nullable=True
                    ) for x in self._subtypes
                }
        return self._subtypes

    @property
    def supertype(self) -> IntegerType:
        if self._supertype is None:
            self._supertype = SignedIntegerType.instance(
                sparse=self.sparse,
                categorical=self.categorical,
                nullable=self.nullable
            )
        return self._supertype


cdef class Int16Type(SignedIntegerType):
    """16-bit integer subtype"""

    _base_slug = "int16"
    aliases = {
        # type
        np.int16: {"backend": "numpy"},

        # dtype
        np.dtype(np.int16): {"backend": "numpy"},
        pd.Int16Dtype(): {"backend": "pandas"},

        # string
        "int16": {},
        "i2": {},
        "Int16": {"backend": "pandas"},
    }

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
            na_value=pd.NA,
            itemsize=2,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical,
                nullable=nullable
            )
        )

        # min/max representable values
        self.min = -2**15
        self.max = 2**15 - 1

    @property
    def subtypes(self) -> frozenset:
        if self._subtypes is None:
            self._subtypes = frozenset({self})
            if not self.nullable:
                self._subtypes |= {
                    type(x).instance(
                        sparse=self.sparse,
                        categorical=self.categorical,
                        nullable=True
                    ) for x in self._subtypes
                }
        return self._subtypes

    @property
    def supertype(self) -> IntegerType:
        if self._supertype is None:
            self._supertype = SignedIntegerType.instance(
                sparse=self.sparse,
                categorical=self.categorical,
                nullable=self.nullable
            )
        return self._supertype


cdef class Int32Type(SignedIntegerType):
    """32-bit integer subtype"""

    _base_slug = "int32"
    aliases = {
        # type
        np.int32: {"backend": "numpy"},

        # dtype
        np.dtype(np.int32): {"backend": "numpy"},
        pd.Int8Dtype(): {"backend": "pandas"},

        # string
        "int32": {},
        "i4": {},
        "Int32": {"backend": "pandas"},
    }

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
            na_value=pd.NA,
            itemsize=4,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical,
                nullable=nullable
            )
        )

        # min/max representable values
        self.min = -2**31
        self.max = 2**31 - 1

    @property
    def subtypes(self) -> frozenset:
        if self._subtypes is None:
            self._subtypes = frozenset({self})
            if not self.nullable:
                self._subtypes |= {
                    type(x).instance(
                        sparse=self.sparse,
                        categorical=self.categorical,
                        nullable=True
                    ) for x in self._subtypes
                }
        return self._subtypes

    @property
    def supertype(self) -> IntegerType:
        if self._supertype is None:
            self._supertype = SignedIntegerType.instance(
                sparse=self.sparse,
                categorical=self.categorical,
                nullable=self.nullable
            )
        return self._supertype


cdef class Int64Type(SignedIntegerType):
    """64-bit integer subtype"""

    _base_slug = "int64"
    aliases = {
        # type
        np.int64: {"backend": "numpy"},

        # dtype
        np.dtype(np.int64): {"backend": "numpy"},
        pd.Int64Dtype(): {"backend": "pandas"},

        # string
        "int64": {},
        "i8": {},
        "Int64": {"backend": "pandas"},
    }

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
            na_value=pd.NA,
            itemsize=8,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical,
                nullable=nullable
            )
        )

        # min/max representable values
        self.min = -2**63
        self.max = 2**63 - 1

    @property
    def subtypes(self) -> frozenset:
        if self._subtypes is None:
            self._subtypes = frozenset({self})
            if not self.nullable:
                self._subtypes |= {
                    type(x).instance(
                        sparse=self.sparse,
                        categorical=self.categorical,
                        nullable=True
                    ) for x in self._subtypes
                }
        return self._subtypes

    @property
    def supertype(self) -> IntegerType:
        if self._supertype is None:
            self._supertype = SignedIntegerType.instance(
                sparse=self.sparse,
                categorical=self.categorical,
                nullable=self.nullable
            )
        return self._supertype


cdef class UInt8Type(UnsignedIntegerType):
    """8-bit unsigned integer subtype"""

    _base_slug = "uint8"
    aliases = {
        # type
        np.uint8: {"backend": "numpy"},

        # dtype
        np.dtype(np.uint8): {"backend": "numpy"},
        pd.UInt8Dtype(): {"backend": "pandas"},

        # string
        "uint8": {},
        "u1": {},
        "UInt8": {"backend": "pandas"},
    }

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
            na_value=pd.NA,
            itemsize=1,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical,
                nullable=nullable
            )
        )

        # min/max representable values
        self.min = 0
        self.max = 2**8 - 1

    @property
    def subtypes(self) -> frozenset:
        if self._subtypes is None:
            self._subtypes = frozenset({self})
            if not self.nullable:
                self._subtypes |= {
                    type(x).instance(
                        sparse=self.sparse,
                        categorical=self.categorical,
                        nullable=True
                    ) for x in self._subtypes
                }
        return self._subtypes

    @property
    def supertype(self) -> IntegerType:
        if self._supertype is None:
            self._supertype = UnsignedIntegerType.instance(
                sparse=self.sparse,
                categorical=self.categorical,
                nullable=self.nullable
            )
        return self._supertype


cdef class UInt16Type(UnsignedIntegerType):
    """16-bit unsigned integer subtype"""

    _base_slug = "uint16"
    aliases = {
        # type
        np.uint16: {"backend": "numpy"},

        # dtype
        np.dtype(np.uint16): {"backend": "numpy"},
        pd.UInt16Dtype(): {"backend": "pandas"},

        # string
        "uint16": {},
        "u2": {},
        "UInt16": {"backend": "pandas"},
    }

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
            na_value=pd.NA,
            itemsize=2,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical,
                nullable=nullable
            )
        )

        # min/max representable values
        self.min = 0
        self.max = 2**16 - 1

    @property
    def subtypes(self) -> frozenset:
        if self._subtypes is None:
            self._subtypes = frozenset({self})
            if not self.nullable:
                self._subtypes |= {
                    type(x).instance(
                        sparse=self.sparse,
                        categorical=self.categorical,
                        nullable=True
                    ) for x in self._subtypes
                }
        return self._subtypes

    @property
    def supertype(self) -> IntegerType:
        if self._supertype is None:
            self._supertype = UnsignedIntegerType.instance(
                sparse=self.sparse,
                categorical=self.categorical,
                nullable=self.nullable
            )
        return self._supertype


cdef class UInt32Type(UnsignedIntegerType):
    """32-bit unsigned integer subtype"""

    _base_slug = "uint32"
    aliases = {
        # type
        np.uint32: {"backend": "numpy"},

        # dtype
        np.dtype(np.uint32): {"backend": "numpy"},
        pd.UInt32Dtype(): {"backend": "pandas"},

        # string
        "uint32": {},
        "u4": {},
        "UInt32": {"backend": "pandas"},
    }

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
            na_value=pd.NA,
            itemsize=4,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical,
                nullable=nullable
            )
        )

        # min/max representable values
        self.min = 0
        self.max = 2**32 - 1

    @property
    def subtypes(self) -> frozenset:
        if self._subtypes is None:
            self._subtypes = frozenset({self})
            if not self.nullable:
                self._subtypes |= {
                    type(x).instance(
                        sparse=self.sparse,
                        categorical=self.categorical,
                        nullable=True
                    ) for x in self._subtypes
                }
        return self._subtypes

    @property
    def supertype(self) -> IntegerType:
        if self._supertype is None:
            self._supertype = UnsignedIntegerType.instance(
                sparse=self.sparse,
                categorical=self.categorical,
                nullable=self.nullable
            )
        return self._supertype


cdef class UInt64Type(UnsignedIntegerType):
    """32-bit unsigned integer subtype"""

    _base_slug = "uint64"
    aliases = {
        # type
        np.uint64: {"backend": "numpy"},

        # dtype
        np.dtype(np.uint64): {"backend": "numpy"},
        pd.UInt64Dtype(): {"backend": "pandas"},

        # string
        "uint64": {},
        "u8": {},
        "UInt64": {"backend": "pandas"},
    }

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
            na_value=pd.NA,
            itemsize=8,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical,
                nullable=nullable
            )
        )

        # min/max representable values
        self.min = 0
        self.max = 2**64 - 1

    @property
    def subtypes(self) -> frozenset:
        if self._subtypes is None:
            self._subtypes = frozenset({self})
            if not self.nullable:
                self._subtypes |= {
                    type(x).instance(
                        sparse=self.sparse,
                        categorical=self.categorical,
                        nullable=True
                    ) for x in self._subtypes
                }
        return self._subtypes

    @property
    def supertype(self) -> IntegerType:
        if self._supertype is None:
            self._supertype = UnsignedIntegerType.instance(
                sparse=self.sparse,
                categorical=self.categorical,
                nullable=self.nullable
            )
        return self._supertype
