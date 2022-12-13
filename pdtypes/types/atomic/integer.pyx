import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AtomicType


# TODO: these are added to alias map along with everything else.  Resolve from
# the alias map until the value is no longer in it.
platform_specific_aliases = {
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


######################
####    MIXINS    ####
######################


class MinMaxMixin:

    @property
    def max(self) -> int:
        return self._max

    @property
    def min(self) -> int:
        return self._min


#####################
####    TYPES    ####
#####################


class IntegerType(AtomicType, MinMaxMixin):
    """Integer supertype"""

    _name = "int"
    _backends = ("python", "numpy", "pandas")
    aliases = {
        # type
        int: {},
        np.integer: {"backend": "numpy"},

        # string
        "int": {},
        "integer": {},
    }

    def __init__(self, backend: str):
        slug = f"{self._name}"

        # int
        if backend is None:
            object_type = None
            dtype = None
            itemsize = None
            self._min = -np.inf
            self._max = np.inf

        # int[python]
        elif backend == "python":
            slug += f"[{backend}]"
            object_type = int
            dtype = np.dtype(np.object_)
            itemsize = None
            self._min = -np.inf
            self._max = np.inf

        # int[numpy]
        elif backend == "numpy":
            slug += f"[{backend}]"
            object_type = np.int64
            dtype = np.dtype(np.int64)
            itemsize = 8
            self._min = -2**63
            self._max = 2**63 - 1

        # int[pandas]
        elif backend == "pandas":
            slug += f"[{backend}]"
            object_type = np.int64
            dtype = pd.Int64Dtype()
            itemsize = 8
            self._min = -2**63
            self._max = 2**63 - 1

        # unrecognized
        else:
            raise TypeError(f"{slug} backend not recognized: {repr(backend)}")

        super(IntegerType, self).__init__(
            backend=backend,
            object_type=object_type,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=itemsize,
            slug=slug
        )


class SignedIntegerType(IntegerType):
    """Signed integer type."""

    _name = "signed"
    aliases = {
        # type
        np.signedinteger: {"backend": "numpy"},

        # string
        "signed": {},
        "signed int": {},
        "signed integer": {},
        "i": {},
    }

    def __init__(self, backend: str = None):
        # Internally, this is an exact copy of IntegerType
        super(SignedIntegerType, self).__init__(backend=backend)


class Int8Type(AtomicType, MinMaxMixin):
    """8-bit integer subtype"""

    _name = "int8"
    _backends = ("numpy", "pandas")
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

    def __init__(self, backend: str = None):
        slug = f"{self._name}"

        # unsigned
        if backend is None:
            object_type = None
            dtype = None

        # unsigned[numpy]
        elif backend == "numpy":
            slug += f"[{backend}]"
            object_type = np.int8
            dtype = np.dtype(np.int8)

        # unsigned[pandas]
        elif backend == "pandas":
            slug += f"[{backend}]"
            object_type = np.int8
            dtype = pd.Int8Dtype()

        # unrecognized
        else:
            raise TypeError(f"{slug} backend not recognized: {repr(backend)}")

        super(Int8Type, self).__init__(
            backend=backend,
            object_type=object_type,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=1,
            slug=slug
        )

        # min/max representable values
        self._min = -2**7
        self._max = 2**7 - 1


class Int16Type(AtomicType, MinMaxMixin):
    """16-bit integer subtype"""

    _name = "int16"
    _backends = ("numpy", "pandas")
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

    def __init__(self, backend: str = None):
        slug = f"{self._name}"

        # unsigned
        if backend is None:
            object_type = None
            dtype = None

        # unsigned[numpy]
        elif backend == "numpy":
            slug += f"[{backend}]"
            object_type = np.int16
            dtype = np.dtype(np.int16)

        # unsigned[pandas]
        elif backend == "pandas":
            slug += f"[{backend}]"
            object_type = np.int16
            dtype = pd.Int16Dtype()

        # unrecognized
        else:
            raise TypeError(f"{slug} backend not recognized: {repr(backend)}")

        super(Int16Type, self).__init__(
            backend=backend,
            object_type=object_type,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=2,
            slug=slug
        )

        # min/max representable values
        self._min = -2**15
        self._max = 2**15 - 1


class Int32Type(AtomicType, MinMaxMixin):
    """32-bit integer subtype"""

    _name = "int32"
    _backends = ("numpy", "pandas")
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

    def __init__(self, backend: str = None):
        slug = f"{self._name}"

        # unsigned
        if backend is None:
            object_type = None
            dtype = None

        # unsigned[numpy]
        elif backend == "numpy":
            slug += f"[{backend}]"
            object_type = np.int32
            dtype = np.dtype(np.int32)

        # unsigned[pandas]
        elif backend == "pandas":
            slug += f"[{backend}]"
            object_type = np.int32
            dtype = pd.Int32Dtype()

        # unrecognized
        else:
            raise TypeError(f"{slug} backend not recognized: {repr(backend)}")

        super(Int32Type, self).__init__(
            backend=backend,
            object_type=object_type,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=4,
            slug=slug
        )

        # min/max representable values
        self._min = -2**31
        self._max = 2**31 - 1


class Int64Type(AtomicType, MinMaxMixin):
    """64-bit integer subtype"""

    _name = "int64"
    _backends = ("numpy", "pandas")
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

    def __init__(self, backend: str = None):
        slug = f"{self._name}"

        # unsigned
        if backend is None:
            object_type = None
            dtype = None

        # unsigned[numpy]
        elif backend == "numpy":
            slug += f"[{backend}]"
            object_type = np.int64
            dtype = np.dtype(np.int64)

        # unsigned[pandas]
        elif backend == "pandas":
            slug += f"[{backend}]"
            object_type = np.int64
            dtype = pd.Int64Dtype()

        # unrecognized
        else:
            raise TypeError(f"{slug} backend not recognized: {repr(backend)}")

        super(Int64Type, self).__init__(
            backend=backend,
            object_type=object_type,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=8,
            slug=slug
        )

        # min/max representable values
        self._min = -2**63
        self._max = 2**63 - 1


class UnsignedIntegerType(AtomicType, MinMaxMixin):
    """Unsigned integer supertype"""

    _name = "unsigned"
    _backends = ("numpy", "pandas")
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

    def __init__(self, backend: str = None):
        slug = f"{self._name}"

        # unsigned
        if backend is None:
            object_type = None
            dtype = None
            itemsize = None

        # unsigned[numpy]
        elif backend == "numpy":
            slug += f"[{backend}]"
            object_type = np.uint64
            dtype = np.dtype(np.uint64)
            itemsize = 8

        # unsigned[pandas]
        elif backend == "pandas":
            slug += f"[{backend}]"
            object_type = np.uint64
            dtype = pd.UInt64Dtype()
            itemsize = 8

        # unrecognized
        else:
            raise TypeError(f"{slug} backend not recognized: {repr(backend)}")

        super(UnsignedIntegerType, self).__init__(
            backend=backend,
            object_type=object_type,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=itemsize,
            slug=slug
        )

        # min/max representable values
        self._min = 0
        self._max = 2**64 - 1


class UInt8Type(AtomicType, MinMaxMixin):
    """8-bit unsigned integer subtype"""

    _name = "uint8"
    _backends = ("numpy", "pandas")
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

    def __init__(self, backend: str = None):
        slug = f"{self._name}"

        # unsigned
        if backend is None:
            object_type = None
            dtype = None

        # unsigned[numpy]
        elif backend == "numpy":
            slug += f"[{backend}]"
            object_type = np.uint8
            dtype = np.dtype(np.uint8)

        # unsigned[pandas]
        elif backend == "pandas":
            slug += f"[{backend}]"
            object_type = np.uint8
            dtype = pd.UInt8Dtype()

        # unrecognized
        else:
            raise TypeError(f"{slug} backend not recognized: {repr(backend)}")

        super(UInt8Type, self).__init__(
            backend=backend,
            object_type=object_type,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=1,
            slug=slug
        )

        # min/max representable values
        self._min = 0
        self._max = 2**8 - 1


class UInt16Type(AtomicType, MinMaxMixin):
    """16-bit unsigned integer subtype"""

    _name = "uint16"
    _backends = ("numpy", "pandas")
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

    def __init__(self, backend: str = None):
        slug = f"{self._name}"

        # unsigned
        if backend is None:
            object_type = None
            dtype = None

        # unsigned[numpy]
        elif backend == "numpy":
            slug += f"[{backend}]"
            object_type = np.uint16
            dtype = np.dtype(np.uint16)

        # unsigned[pandas]
        elif backend == "pandas":
            slug += f"[{backend}]"
            object_type = np.uint16
            dtype = pd.UInt16Dtype()

        # unrecognized
        else:
            raise TypeError(f"{slug} backend not recognized: {repr(backend)}")

        super(UInt16Type, self).__init__(
            backend=backend,
            object_type=object_type,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=2,
            slug=slug
        )

        # min/max representable values
        self._min = 0
        self._max = 2**16 - 1

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


class UInt32Type(AtomicType, MinMaxMixin):
    """32-bit unsigned integer subtype"""

    _name = "uint32"
    _backends = ("numpy", "pandas")
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

    def __init__(self, backend: str = None):
        slug = f"{self._name}"

        # unsigned
        if backend is None:
            object_type = None
            dtype = None

        # unsigned[numpy]
        elif backend == "numpy":
            slug += f"[{backend}]"
            object_type = np.uint32
            dtype = np.dtype(np.uint32)

        # unsigned[pandas]
        elif backend == "pandas":
            slug += f"[{backend}]"
            object_type = np.uint32
            dtype = pd.UInt32Dtype()

        # unrecognized
        else:
            raise TypeError(f"{slug} backend not recognized: {repr(backend)}")

        super(UInt32Type, self).__init__(
            backend=backend,
            object_type=object_type,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=4,
            slug=slug
        )

        # min/max representable values
        self._min = 0
        self._max = 2**32 - 1


class UInt64Type(AtomicType, MinMaxMixin):
    """32-bit unsigned integer subtype"""

    _name = "uint64"
    _backends = ("numpy", "pandas")
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

    def __init__(self, backend: str = None):
        slug = f"{self._name}"

        # unsigned
        if backend is None:
            object_type = None
            dtype = None

        # unsigned[numpy]
        elif backend == "numpy":
            slug += f"[{backend}]"
            object_type = np.uint64
            dtype = np.dtype(np.uint64)

        # unsigned[pandas]
        elif backend == "pandas":
            slug += f"[{backend}]"
            object_type = np.uint64
            dtype = pd.UInt64Dtype()

        # unrecognized
        else:
            raise TypeError(f"{slug} backend not recognized: {repr(backend)}")

        super(UInt64Type, self).__init__(
            backend=backend,
            object_type=object_type,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=8,
            slug=slug
        )

        # min/max representable values
        self._min = 0
        self._max = 2**64 - 1


##########################
#####    HIERARCHY    ####
##########################


# integer subtypes
SignedIntegerType.register_supertype(IntegerType)
UnsignedIntegerType.register_supertype(IntegerType)


# signed subtypes
# Int8Type.register_supertype(SignedIntegerType)
Int16Type.register_supertype(SignedIntegerType)
Int32Type.register_supertype(SignedIntegerType)
Int64Type.register_supertype(SignedIntegerType)


# unsigned subtypes
UInt8Type.register_supertype(UnsignedIntegerType)
UInt16Type.register_supertype(UnsignedIntegerType)
UInt32Type.register_supertype(UnsignedIntegerType)
UInt64Type.register_supertype(UnsignedIntegerType)
