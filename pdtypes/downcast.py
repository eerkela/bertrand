from __future__ import annotations
import struct

import numpy as np

from pdtypes.error import error_trace


"""
TODO: vectorize these using np.vectorize
"""


def float_bits(observation: float, endian: str = ">") -> str:
    dtype = np.dtype(type(observation))
    try:
        byte_str = struct.pack(f"{endian}{dtype.char}", observation)
    except struct.error as err:
        err_msg = (f"[{error_trace()}] could not interpret struct format: "
                   f"{endian}{dtype.char}")
        raise struct.error(err_msg) from err
    return "".join([f"{b:08b}" for b in list(byte_str)])


def downcast_float(f: float) -> float:
    # IEEE 754 floating point bit layouts:
    #   fp16 -> sign (1), exponent (5), mantissa (10)
    #   fp32 -> sign (1), exponent (8), mantissa (23)
    #   fp64 -> sign (1), exponent (11), mantissa (52)
    bit_str = float_bits(f)
    if len(bit_str) == 64:
        exponent = int(bit_str[1:12], 2) - (2**10 - 1)  # bias
        mantissa = int(bit_str[12:], 2)
        if not exponent >> 5 and not mantissa & int("0" * 10 + "1" * 42, 2):
            return np.float16(f)
        if not exponent >> 8 and not mantissa & int("0" * 23 + "1" * 29, 2):
            return np.float32(f)
    elif len(bit_str) == 32:
        exponent = int(bit_str[1:9], 2) - (2**7 - 1)  # bias
        mantissa = int(bit_str[9:], 2)
        if not exponent >> 5 and not mantissa & int("0" * 10 + "1" * 13, 2):
            return np.float16(f)
    return f


def downcast_int(i: int, signed: bool = True) -> int:
    if signed or i < 0:
        signed_types = (np.int8, np.int16, np.int32, np.int64)
        for int_type in signed_types:
            if abs(i) < 2**(np.dtype(int_type).itemsize * 8 - 1):
                return int_type(i)
    else:
        unsigned_types = (np.uint8, np.uint16, np.uint32, np.uint64)
        for int_type in unsigned_types:
            if i < 2**(np.dtype(int_type).itemsize * 8):
                return int_type(i)
    return i


def downcast_complex(c: complex) -> complex:
    real = downcast_float(c.real)
    imag = downcast_float(c.imag)
    down = (np.float32, np.float16)
    if type(real) in down and type(imag) in down:
        return np.complex64(f"{real}+{imag}j")
    return c


def _downcast_series(series: pd.Series) -> pd.Series:
    series_dtype = parse_dtype(series.dtype)

    if series_dtype == int:
        extension_type = pd.api.types.is_extension_array_dtype(series_dtype)
        series_min = series.min()  # faster than series.quantile([0, 1])
        series_max = series.max()
        if series_min >= 0:  # unsigned
            if extension_type:
                integer_types = (pd.UInt8Dtype(), pd.UInt16Dtype(),
                                 pd.UInt32Dtype(), pd.UInt64Dtype())
            else:
                integer_types = (np.dtype(np.uint8), np.dtype(np.uint16),
                                 np.dtype(np.uint32), np.dtype(np.uint64))
            for typespec in integer_types:
                max_int = 2 ** (typespec.itemsize * 8)
                if series_max < max_int:
                    return series.astype(typespec)
        else:  # signed
            if extension_type:
                integer_types = (pd.Int8Dtype(), pd.Int16Dtype(),
                                 pd.Int32Dtype(), pd.Int64Dtype())
            else:
                integer_types = (np.dtype(np.int8), np.dtype(np.int16),
                                 np.dtype(np.int32), np.dtype(np.int64))
            for typespec in integer_types:
                max_int = 2 ** (typespec.itemsize * 8 - 1)
                if series_max < max_int:
                    return series.astype(typespec)
        err_msg = (f"[{error_trace()}] could not downcast series "
                    f"(head: {list(series.head())})")
        raise ValueError(err_msg)

    if series_dtype == float:
        extension_type = pd.api.types.is_extension_array_dtype(series_dtype)
        series_min = series.min()  # faster than series.quantile([0, 1])
        series_max = series.max()
