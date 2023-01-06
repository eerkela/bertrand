import numpy as np
import pandas as pd

from pdtypes.types import ElementType, resolve_dtype

from ..base import NumericSeries


def demote_integer_supertypes(
    series: NumericSeries,
    dtype: ElementType
) -> ElementType:
    """Attempt to demote integer supertypes into 64-bit or lower alternatives,
    fitting the observed range of `series`.
    """
    # NOTE: comparison between floats and ints can be inconsistent when the
    # integer exceeds the bit width of the corresponding float significand.
    # Casting to longdouble mitigates this by ensuring a full 64-bit
    # significand.
    min_signed = -2**63
    max_signed = 2**63 - 1
    min_unsigned = 0
    max_unsigned = 2**64 - 1

    # general integer supertype - can be arbitrarily large
    if dtype == resolve_dtype(int, nullable=dtype.nullable):
        min_val = series.min()
        max_val = series.max()
        if isinstance(min_val, (float, np.floating)):
            min_signed = np.longdouble(min_signed)
            min_unsigned = np.longdouble(min_unsigned)
        if isinstance(max_val, (float, np.floating)):
            max_signed = np.longdouble(max_signed)
            max_unsigned = np.longdouble(max_unsigned)

        if min_val < min_signed or max_val > max_signed:  # > int64
            if min_val >= min_unsigned and max_val <= max_unsigned:  # < uint64
                return resolve_dtype(np.uint64, nullable=dtype.nullable)

            # > int64 and > uint64, return as built-in python ints
            return dtype

        # extended range isn't needed, demote to int64
        return resolve_dtype(np.int64, nullable=dtype.nullable)

    # signed integer supertype - suppress conversion to uint64
    if dtype == resolve_dtype("signed", nullable=dtype.nullable):
        min_val = series.min()
        max_val = series.max()
        if isinstance(min_val, (float, np.floating)):
            min_signed = np.longdouble(min_signed)
        if isinstance(max_val, (float, np.floating)):
            max_signed = np.longdouble(max_signed)

        if min_val < min_signed or max_val > max_signed:  # > int64
            return dtype

        # extended range isn't needed, demote to int64
        return resolve_dtype(np.int64, nullable=dtype.nullable)

    # unsigned integer supertype - demote to uint64
    if dtype == resolve_dtype("unsigned", nullable=dtype.nullable):
        return resolve_dtype(np.uint64, nullable=dtype.nullable)

    return dtype


def downcast_integer_dtype(
    series: NumericSeries,
    dtype: ElementType
) -> ElementType:
    """Find the smallest integer ElementType that can fully represent the
    series values.
    """
    resolve = lambda t: resolve_dtype(
        t,
        sparse=dtype.sparse,
        categorical=dtype.categorical,
        nullable=dtype.nullable
    )

    # resolve all possible dtypes that are smaller than given
    if dtype in resolve_dtype("unsigned"):
        smaller = [np.uint8, np.uint16, np.uint32, np.uint64]
    else:
        smaller = [np.int8, np.int16, np.int32, np.int64]
    smaller = [resolve(t) for t in smaller]

    # TODO: make dtype.min/max np.longdouble rather than series.min()/max()
    # -> converts this into a float-float comparison rather than float-int

    # NOTE: comparison between floats and ints can be inconsistent when the
    # value exceeds the size of the floating point significand.  Casting to
    # longdouble mitigates this by ensuring a full 64-bit significand.
    min_val = np.longdouble(series.min())
    max_val = np.longdouble(series.max())

    # search for smaller dtypes that can represent series
    for small in smaller[:smaller.index(dtype)]:
        if min_val >= small.min and max_val <= small.max:
            dtype = small
            break  # stop at smallest

    return dtype


def downcast_float_series(
    series: pd.Series,
    dtype: ElementType
) -> pd.Series:
    """Convert `series` to the smallest float ElementType that can fully
    represent its values.
    """
    resolve = lambda t: resolve_dtype(
        t,
        sparse=dtype.sparse,
        categorical=dtype.categorical
    )

    # resolve all possible dtypes that are smaller than given
    smaller = [
        resolve(t) for t in (
            np.float16, np.float32, np.float64, float, np.longdouble
        )
    ]

    # search for smaller dtypes that can represent series
    for small in smaller[:smaller.index(dtype)]:
        attempt = series.astype(small.numpy_type)
        if (attempt == series).all():
            return attempt  # stop at smallest

    return series


def downcast_complex_series(
    series: pd.Series,
    dtype: ElementType
) -> pd.Series:
    """Convert `series` to the smallest complex ElementType that can fully
    represent its values.
    """
    resolve = lambda t: resolve_dtype(
        t,
        sparse=dtype.sparse,
        categorical=dtype.categorical
    )

    # resolve all possible dtypes that are smaller than given
    smaller = [
        resolve(t) for t in (
            np.complex64, np.complex128, complex, np.clongdouble
        )
    ]

    # search for smaller dtypes that can represent series
    for small in smaller[:smaller.index(dtype)]:
        attempt = series.astype(small.numpy_type)
        if (attempt == series).all():
            return attempt  # stop at smallest

    return series
