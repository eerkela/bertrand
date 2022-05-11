from __future__ import annotations
from functools import cache, lru_cache, partial
import struct
import sys

import numpy as np

from pdtypes.parse import parse_dtype
from pdtypes.error import error_trace


"""
TODO: vectorize these using np.vectorize
"""



# def integer_bits(integer: int,
#                  byte_order: str = "big",
#                  bit_order: str = "big",
#                  twos_complement: bool = True) -> str:
#     """Return the bit representation of an integer.

#     Parameters
#     ----------------
#     integer : int
#         integer to be converted.  Can be either a built-in int or one of
#         numpy's various integer classes, signed or unsigned.
#     byte_order : str
#         byte endian-ness.  Can be `'big'` or `'little'`.  If `'big'`, the most
#         significant byte will always be on the left.  If `'little'`, it will be
#         on the right.
#     bit_order : str
#         endian-ness of bits within each byte.  Can be `'big'` or `'little'`.
#         If `'big'`, the most significant bit within each byte will always be on
#         the left.  If `'little'`, it will be on the right.
#     twos_complement : bool
#         determines how to represent negative integers in a signed binary number
#         system.  If `True`, negative integers are represented by taking the
#         so-called two's complement of their positive counterparts.  If `False`,
#         they are shown as naive sign-magnitude strings, where the most
#         significant bit is labelled with a `'+'` or `'-'` to mark the sign of
#         `integer`.

#     Returns
#     ----------------
#     str
#         the bit representation of `integer`, arranged according to `byte_order`,
#         `bit_order`, and `twos_complement`.
#     """
#     dtype = np.dtype(type(integer))

#     # naive sign-magnitude representation
#     if not twos_complement:
#         # get explicit sign-magnitude string, padded to memory alignment
#         bit_string = np.binary_repr(integer)
#         if bit_string.startswith("-"):
#             sign = "-"
#             bit_string = bit_string[1:]
#         else:
#             sign = "+"
#         padded = f"{sign}{bit_string:0>{dtype.itemsize*8 - 1}}"

#         # convert to byte strings and sort out endian-ness
#         byte_strings = [padded[i:i+8]
#                         for i in range(0, dtype.itemsize * 8, 8)]
#         if byte_order == "big":
#             if bit_order == "big":
#                 return padded
#             return "".join(b[::-1] for b in byte_strings)
#         if bit_order == "big":
#             return "".join(b for b in reversed(byte_strings))
#         return "".join(b[::-1] for b in reversed(byte_strings))

#     # twos-complement, matches underlying memory array
#     dtype_to_struct_char = {
#         "b": "b",  # 1-byte signed integer
#         "B": "B",  # 1-byte unsigned integer
#         "h": "h",  # 2-byte signed integer
#         "H": "H",  # 2-byte unsigned integer
#         "i": "i",  # 4-byte signed integer
#         "I": "I",  # 4-byte unsigned integer
#         "l": "q",  # 8-byte signed integer
#         "L": "Q"  # 8-byte unsigned integer
#     }
#     if byte_order == "big":  # endian-ness handled by struct package
#         format_string = f">{dtype_to_struct_char[dtype.char]}"
#     else:
#         format_string = f"<{dtype_to_struct_char[dtype.char]}"
#     byte_array = list(struct.pack(format_string, integer))
#     if bit_order == "big":
#         return "".join(f"{b:08b}" for b in byte_array)
#     return "".join(f"{b:08b}"[::-1] for b in byte_array)


# def float_bits(floating_point: float,
#                byte_order: str = "big",
#                bit_order: str = "big",
#                decompose: bool = False) -> str | dict[str, int]:
#     """Return the bit representation of a floating point number.

#     Parameters
#     ----------------
#     floating_point : float
#         floating point number to be converted.  Can be either a built-in float
#         or one of numpy's various float classes, including `numpy.longdouble`.
#     byte_order : str
#         byte endian-ness.  Can be `'big'` or `'little'`.  If `'big'`, the most
#         significant byte will always be on the left.  If `'little'`, it will be
#         on the right.
#     bit_order : str
#         endian-ness of bits within each byte.  Can be `'big'` or `'little'`.
#         If `'big'`, the most significant bit within each byte will always be on
#         the left.  If `'little'`, it will be on the right.
#     decompose : bool
#         if `True`, decompose the resulting bit string into sign, exponent, and
#         mantissa.  If `floating_point` is a long double, it will decompose into
#         sign, exponent, integer, and fraction instead.

#     Returns
#     ----------------
#     str
#         if `decompose=False`.  Contains the underlying bit representation of
#         `floating_point`, organized according to `byte_order` and `bit_order`
#     dict[str, int]
#         if `decompose=True`.  Contains the individual components of
#         `floating_point`, converted to integers to allow bit shift operations.
#     """
#     # struct package cannot interpret long doubles
#     if type(floating_point) == np.longdouble:
#         byte_strings = [f"{b:08b}" for b in list(bytes(floating_point))]

#         if decompose:  # decompose into constituent parts
#             if sys.byteorder == "big":
#                 big_endian = "".join(byte_strings)
#             else:
#                 big_endian = "".join(reversed(byte_strings))
#             size = len(big_endian)
#             return {  # long double is really an 80-bit float, padded to 96/128
#                 "sign": big_endian[size-80],
#                 "exponent": big_endian[size-79:size-64],
#                 "integer": big_endian[size-64:size-63],
#                 "fraction": big_endian[size-63:]
#             }

#         # return as raw bit string
#         if sys.byteorder == "big":
#             if bit_order == "big":
#                 return "".join(byte_strings)
#             return "".join(b[::-1] for b in byte_strings)
#         if bit_order == "big":
#             return "".join(reversed(byte_strings))
#         return "".join(b[::-1] for b in reversed(byte_strings))

#     # 64-bit and under
#     dtype = np.dtype(type(floating_point))
#     if decompose:
#         byte_array = list(struct.pack(f">{dtype.char}", floating_point))
#         big_endian = "".join(f"{b:08b}" for b in byte_array)
#         exp_length = {
#             16: 5,
#             32: 8,
#             64: 11
#         }
#         return {
#             "sign": big_endian[0],
#             "exponent": big_endian[1:1+exp_length[len(big_endian)]],
#             "mantissa": big_endian[1+exp_length[len(big_endian)]:]
#         }
#     if byte_order == "big":  # endian-ness handled by struct package
#         format_string = f">{dtype.char}"
#     else:
#         format_string = f"<{dtype.char}"
#     byte_array = list(struct.pack(format_string, floating_point))
#     if bit_order == "big":
#         return "".join(f"{b:08b}" for b in byte_array)
#     return "".join(f"{b:08b}"[::-1] for b in byte_array)


def bits(
    number: int | float | complex,
    byte_order: str = "big",
    bit_order: str = "big",
    twos_complement: bool = True,
    decompose: bool = False
) -> (str | dict[str, int] | 
      tuple[str, str] | tuple[dict[str, int], dict[str, int]]):
    """Return the bit representation of any numeric value, be it an integer,
    float, or complex number.

    Parameters
    ----------------
    number : int | float | complex
        integer, float, or complex number to be converted.  Can be either
        built-in or one of numpy's various numeric classes.
    byte_order : str
        byte endian-ness.  Can be `'big'` or `'little'`.  If `'big'`, the most
        significant byte will always be on the left.  If `'little'`, it will be
        on the right.
    bit_order : str
        endian-ness of bits within each byte.  Can be `'big'` or `'little'`.
        If `'big'`, the most significant bit within each byte will always be on
        the left.  If `'little'`, it will be on the right.
    twos_complement : bool
        only applies to integer values of `number`.  If `True`, negative
        integers are represented by taking the so-called two's complement of
        their positive counterparts.  If `False`, they are shown as naive
        sign-magnitude strings, where the most significant bit is labelled
        with a `'+'` or `'-'` to mark the sign of `number`.
    decompose : bool
        only applies to floating-point and complex values of `number`.
        If `True` and `number` is a float, the resulting bit string is
        decomposed into sign, exponent, and mantissa (sign, exponent, integer,
        and fraction if `number` is a long double).  If `number` is
        a complex number, this is done for both its real and imaginary
        components.

    Returns
    ----------------
    str
        if `number` is an integer or float with `decompose=False`.  This
        is the bit representation as it is stored in memory.
    dict[str, int]
        if `number` is a float and `decompose=True`.  These are the
        individual components of the IEEE floating-point standard, converted
        to integers to allow for bitwise operations.
    tuple[str, str]
        if `number` is a complex number and `decompose=False`.  The first
        element represents the number's real component.  The second is its
        imaginary component.
    tuple[dict[str, int], dict[str, int]]
        if `number` is a complex number and `decompose=True`.  The first
        element represents the decomposed parts of the number's real component.
        The second is the decomposed parts of its imaginary component.

    Raises
    ----------------
    ValueError
        if byte_order or bit_order is not one of the allowed values (`'big'`,
        `'little'`).
    TypeError
        if `number` is not an integer, float, or complex number.
    """
    if byte_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `byte_order` must be either 'big' or "
                   f"'little', not {repr(byte_order)}")
        raise ValueError(err_msg)
    if bit_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `bit_order` must be either 'big' or "
                   f"'little', not {repr(bit_order)}")
        raise ValueError(err_msg)

    # integers
    if np.issubdtype(type(number), np.integer):
        return integer_bits(number,
                            byte_order=byte_order,
                            bit_order=bit_order,
                            twos_complement=twos_complement)

    # floats
    if np.issubdtype(type(number), np.floating):
        return float_bits(number,
                          byte_order=byte_order,
                          bit_order=bit_order,
                          decompose=decompose)

    # complex numbers
    if np.iscomplexobj(number):
        real = float_bits(number.real,
                          byte_order=byte_order,
                          bit_order=bit_order,
                          decompose=decompose)
        imag = float_bits(number.imag,
                          byte_order=byte_order,
                          bit_order=bit_order,
                          decompose=decompose)
        return (real, imag)

    err_msg = (f"[{error_trace()}] could not interpret `number` type: "
               f"must be int, float, or complex (received: "
               f"{type(number)})")
    raise TypeError(err_msg)


# def bits_to_float(bit_string: str,
#                   byte_order: str = "big",
#                   bit_order: str = "big") -> float:
#     """Convert a floating-point bit string (sign, exponent, mantissa) to the
#     corresponding floating-point value.

#     Parameters
#     ----------------
#     bit_string : str
#         the floating-point bit string to convert to the corresponding value.
#         Can be 16, 32, or 64 bit (the struct package does not support long
#         doubles, unfortunately).
#     byte_order : str
#         byte endian-ness.  Can be `'big'` or `'little'`.  If `'big'`, the most
#         significant byte will always be on the left.  If `'little'`, it will be
#         on the right.
#     bit_order : str
#         endian-ness of bits within each byte.  Can be `'big'` or `'little'`.
#         If `'big'`, the most significant bit within each byte will always be on
#         the left.  If `'little'`, it will be on the right.

#     Returns
#     ----------------
#     float
#         the floating-point value associated with the given `bit_string`.

#     Raises
#     ----------------
#     TypeError
#         if `bit_string` is not a string.
#     ValueError
#         if byte_order or bit_order is not one of the allowed values (`'big'`,
#         `'little'`).
#     KeyError
#         if `bit_string` is not of a recognized length (16, 32, 64).
#     """
#     if not isinstance(bit_string, str):
#         err_msg = (f"[{error_trace()}] `bit_string` must be a string "
#                    f"containing the bitwise representation of a "
#                    f"floating-point number (received: {type(bit_string)})")
#         raise TypeError(err_msg)
#     if byte_order not in ("big", "little"):
#         err_msg = (f"[{error_trace()}] `byte_order` must be either 'big' or "
#                    f"'little', not {repr(byte_order)}")
#         raise ValueError(err_msg)
#     if bit_order not in ("big", "little"):
#         err_msg = (f"[{error_trace()}] `bit_order` must be either 'big' or "
#                    f"'little', not {repr(bit_order)}")
#         raise ValueError(err_msg)

#     # struct package cannot interpret long doubles
#     if len(bit_string) > 64:
#         # convert to big endian format
#         if byte_order == "big":
#             if bit_order == "big":
#                 big_endian = bit_string
#             else:
#                 byte_strings = [bit_string[i:i+8]
#                                 for i in range(0, len(bit_string), 8)]
#                 big_endian = "".join(b[::-1] for b in byte_strings)
#         else:
#             byte_strings = [bit_string[i:i+8]
#                             for i in range(0, len(bit_string), 8)]
#             if bit_order == "big":
#                 big_endian = "".join(b for b in reversed(byte_strings))
#             else:
#                 big_endian = "".join(b[::-1] for b in reversed(byte_strings))
#         print(big_endian)

#         # separate into constituent parts
#         size = len(big_endian)
#         sign = int(big_endian[size - 80], 2)
#         exponent = int(big_endian[size - 79:size - 64], 2)
#         integer = int(big_endian[size - 64: size - 63], 2)
#         fraction = int(big_endian[size - 63:], 2)

#         # combine into final float
#         mantissa = np.longdouble(fraction) / np.longdouble(2.**63) + integer
#         magnitude = np.longdouble(2.**(exponent - (2**14 - 1))) * mantissa
#         if sign:
#             return -1 * magnitude
#         return magnitude

#     # collect bits into bytes
#     if bit_order == "big":
#         byte_array = [int(bit_string[i:i+8], 2)
#                       for i in range(0, len(bit_string), 8)]
#     else:
#         byte_array = [int(bit_string[i+8:i:-1], 2)
#                       for i in range(0, len(bit_string), 8)]

#     # unpack into numpy float using struct package
#     float_char = {
#         16: "e",
#         32: "f",
#         64: "d"
#     }
#     if byte_order == "big":
#         format_string = f">{float_char[len(bit_string)]}"
#     else:
#         format_string = f"<{float_char[len(bit_string)]}"
#     result = struct.unpack(format_string, bytes(byte_array))[0]
#     float_types = {
#         16: np.float16,
#         32: np.float32,
#         64: np.float64
#     }
#     return float_types[len(bit_string)](result)


def downcast_int(i: int, signed: bool = True) -> int:
    if signed or i < 0:
        signed_types = (np.int8, np.int16, np.int32)
        for int_type in signed_types:
            if abs(i) < 2**(np.dtype(int_type).itemsize * 8 - 1):
                return int_type(i)
    else:
        unsigned_types = (np.uint8, np.uint16, np.uint32)
        for int_type in unsigned_types:
            if i < 2**(np.dtype(int_type).itemsize * 8):
                return int_type(i)
    err_msg = (f"[{error_trace()}] could not downcast int: {i}")
    raise ValueError(err_msg)


def downcast_float(f: float) -> float:
    # IEEE 754 floating point bit layouts:
    #   fp16 -> sign (1), exponent (5), mantissa (10)
    #   fp32 -> sign (1), exponent (8), mantissa (23)
    #   fp64 -> sign (1), exponent (11), mantissa (52)
    bit_str = float_bits(f, endian=">")
    if len(bit_str) == 64:
        exponent = int(bit_str[1:12], 2) - (2**10 - 1)  # accounting for bias
        mantissa = int(bit_str[12:], 2)
        if not exponent >> 5 and not mantissa & int("0" * 10 + "1" * 42, 2):
            return np.float16(f)
        if not exponent >> 8 and not mantissa & int("0" * 23 + "1" * 29, 2):
            return np.float32(f)
    elif len(bit_str) == 32:
        exponent = int(bit_str[1:9], 2) - (2**7 - 1)  # accounting for bias
        mantissa = int(bit_str[9:], 2)
        if not exponent >> 5 and not mantissa & int("0" * 10 + "1" * 13, 2):
            return np.float16(f)
    err_msg = (f"[{error_trace()}] could not downcast float: {f}")
    raise ValueError(err_msg)


def downcast_complex(c: complex) -> complex:
    try:
        real = downcast_float(c.real)
        imag = downcast_float(c.imag)
        return np.complex64(f"{real}+{imag}j")
    except ValueError as err:
        err_msg = (f"[{error_trace()}] could not downcast complex: {c}")
        raise ValueError(err_msg) from err


def downcast(n: int | float | complex,
             signed: bool = True) -> int | float | complex:
    cast_map = {
        int: partial(downcast_int, signed=signed),
        float: downcast_float,
        complex: downcast_complex
    }
    return cast_map[parse_dtype(type(n))](n)


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
