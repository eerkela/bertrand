import decimal
from types import MappingProxyType
from typing import Union, Sequence

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AtomicType

cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve


# TODO: add datetime_like `since` hint to to_datetime, to_timedelta


class BooleanType(AtomicType):
    """Boolean supertype."""

    name = "bool"
    aliases = {
        # type
        bool: {},
        np.bool_: {"backend": "numpy"},

        # dtype
        np.dtype(np.bool_): {"backend": "numpy"},
        pd.BooleanDtype(): {"backend": "pandas"},

        # string
        "bool": {},
        "boolean": {},
        "bool_": {},
        "bool8": {},
        "b1": {},
        "?": {},
        "Boolean": {"backend": "pandas"},
    }
    _backends = ("python", "numpy", "pandas")

    def __init__(self, backend: str = None):
        # "bool"
        if backend is None:
            type_def = bool
            dtype = np.dtype(bool)

        # "bool[python]"
        elif backend == "python":
            type_def = bool
            dtype = np.dtype("O")

        # "bool[numpy]"
        elif backend == "numpy":
            type_def = np.bool_
            dtype = np.dtype(bool)

        # "bool[pandas]"
        elif backend == "pandas":
            type_def = np.bool_
            dtype = pd.BooleanDtype()

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend

        # call AtomicType constructor
        super(BooleanType, self).__init__(
            type_def=type_def,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=1,
            slug=self.slugify(backend=backend)
        )

    ########################
    ####    REQUIRED    ####
    ########################

    @classmethod
    def slugify(cls, backend: str = None) -> str:
        slug = cls.name
        if backend is not None:
            slug += f"[{backend}]"
        return slug

    @property
    def kwargs(self) -> MappingProxyType:
        return MappingProxyType({
            "backend": self.backend
        })

    ##############################
    ####    CUSTOMIZATIONS    ####
    ##############################

    def _generate_subtypes(self, types: set) -> frozenset:
        # treat backend=None as wildcard
        kwargs = [self.kwargs]
        if self.backend is None:
            kwargs.extend([
                {**kw, **{"backend": b}}
                for kw in kwargs
                for b in self._backends
            ])

        # build result, skipping invalid kwargs
        result = set()
        for t in types:
            for kw in kwargs:
                try:
                    result.add(t.instance(**kw))
                except TypeError:
                    continue

        # return as frozenset
        return frozenset(result)

    def parse(self, input_str: str):
        if input_str in resolve.na_strings:
            return resolve.na_strings[input_str]
        if input_str not in ("True", "False"):
            raise TypeError(
                f"could not interpret boolean string: {input_str}"
            )
        return self.type_def(input_str == "True")

    def to_float(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        downcast: bool = False,
        **unused
    ) -> pd.Series:
        """Convert boolean data to a floating point data type."""
        # downcast float dtype if directed
        if downcast:
            dtype = dtype.downcast(min=0, max=1)

        # astype(float) fails when given pd.NA in an object series
        if pd.api.types.is_object_dtype(series):
            series.series = self.to_boolean(series, self)

        return series.astype(dtype.dtype)

    def to_complex(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        downcast: bool = False,
        **unused
    ) -> pd.Series:
        """Convert boolean data to a complex data type."""
        if downcast:
            dtype = dtype.downcast(min=0, max=1)

        return series.astype(dtype.dtype)

    def to_decimal(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        **unused
    ) -> pd.Series:
        """Convert boolean data to a decimal data type."""
        with series.exclude_na(dtype.na_value):
            series.series += decimal.Decimal(0)
        return series.series

    def to_datetime(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str = "ns",
        since: str = "UTC"
    ):
        """Convert boolean data to a datetime data type."""
        pass


#######################
####    PRIVATE    ####
#######################


# StringType.to_boolean(
#     self,
#     series: cast.SeriesWrapper,
#     dtype: AtomicType,
#     true: Sequence[str] = ("true", "t", "yes", "y", "on", "1"),
#     false: Sequence[str] = ("false", "f", "no", "n", "off", "0"),
#     errors: str = "raise"
# ):
#     fill_value, true, false = parse_true_false_strings(dtype, true, false)
#     if not pd.isna(fill_value):
#         errors = "coerce"
# 
#     lookup = dict.fromkeys(true, True)
#     lookup |= dict.fromkeys(false, False)
# 
#     result, hasnans = string_to_boolean(
#         series.to_numpy(),
#         lookup=lookup,
#         errors=errors,
#         fill_value=fill_value
#     )
# 
#     if (series.hasnans or hasnans) and not dtype.is_nullable:
#         dtype = dtype.replace(backend="pandas")
# 
#     return pd.Series(result, index=series.index, dtype=dtype.dtype)


cdef tuple parse_true_false_strings(AtomicType dtype, tuple true, tuple false):
    """Remove wildcards from `true`/`false` and find an appropriate
    `fill_value` for BooleanType.parse()
    """
    fill_value = dtype.na_value

    # check for '*' in `true`
    if "*" in true:
        fill_value = dtype.type_def(True)
        while "*" in true:
            true.remove("*")

    # check for '*' in `false`
    if "*" in false:
        if fill_value == True:
            raise ValueError(
                "wildcard '*' can be in either `true` or `false`, not both"
            )
        fill_value = dtype.type_def(False)
        while "*" in false:
            false.remove("*")

    # unescape '\*' literals
    true = tuple(s.replace(r"\*", "*") for s in true)
    false = tuple(s.replace(r"\*", "*") for s in false)

    # return
    return (fill_value, true, false)


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] string_to_boolean_vector(
    np.ndarray[str] arr,
    tuple true,
    tuple false,
    str errors,
    object fill_value
):
    """Convert boolean strings ('True', 'False', 'yes', 'no', 'on', 'off',
    etc.) into their equivalent boolean value.
    """
    cdef unsigned int arr_length = arr.shape[0]
    cdef unsigned int i
    cdef dict lookup = {}
    cdef np.ndarray[object] result = np.full(arr_length, fill_value, dtype="O")

    lookup = dict.fromkeys(true, True)
    lookup |= dict.fromkeys(false, False)
    lookup |= {k.lower(): v for k, v in resolve.na_strings.items()}

    # step through array
    for i in range(arr_length):
        try:
            result[i] = lookup[arr[i].strip().lower()]
        except KeyError as err:
            if errors == "coerce":
                continue  # np.full(...) implicitly fills with `fill_value`
            err_msg = (f"non-boolean value encountered at index {i}: "
                       f"{repr(arr[i])}")
            raise ValueError(err_msg) from err

    return result
