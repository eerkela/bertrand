from __future__ import annotations
from functools import lru_cache
import struct
import sys

import numpy as np
import pandas as pd

from pdtypes.error import error_trace


CACHE_SIZE = 2**10


######################
####    Scalar    ####
######################


def big_endian(bit_string: str,
               byte_order: str,
               bit_order: str) -> str:
    """Convert `bit_string` to big-endian format, given initial `byte_order`
    and `bit_order`.
    """
    if byte_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `byte_order` must be either 'big' "
                   f"or 'little', not {repr(byte_order)}")
        raise ValueError(err_msg)
    if bit_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `bit_order` must be either 'big' "
                   f"or 'little', not {repr(bit_order)}")
        raise ValueError(err_msg)

    # convert to byte strings and reorder
    byte_strings = [bit_string[i:i+8] for i in range(0, len(bit_string), 8)]
    if byte_order in ("big", ">"):
        if bit_order in ("big", ">"):
            return bit_string
        return "".join(b[::-1] for b in byte_strings)
    if bit_order in ("big", ">"):
        return "".join(b for b in reversed(byte_strings))
    return "".join(b[::-1] for b in reversed(byte_strings))


def bits(value: int | float | complex,
         byte_order: str = "big",
         bit_order: str = "big",
         scheme: str = "sign magnitude") -> str | dict[str, str]:
    """Return the bit representation of any numeric input.

    Parameters
    ----------------
    value : int | float | complex
        numeric value to be converted into binary form.  Can be any of python's
        built-in numeric classes (int, float, complex), or a numpy equivalent
        (np.uint[size], np.int[size], np.float[size], np.complex[size]), with
        the returned bit string(s) matching the footprint of these types in
        working memory.
    byte_order : str
        byte endian-ness.  Can be `'big'` or `'little'`.  If `'big'`, the most
        significant byte will always be on the left.  If `'little'`, it will be
        on the right.
    bit_order : str
        endian-ness of bits within each byte.  Can be `'big'` or `'little'`.
        If `'big'`, the most significant bit within each byte will always be on
        the left.  If `'little'`, it will be on the right.
    scheme : str
        algorithm used to represent signed negative integers.  Can be
        'sign magnitude', 'ones complement', or 'twos complement'.  See
        Wikipedia for more information on how these work.

    Returns
    ----------------
    str
        if `value` is an integer or float.  This bit string always has a length
        which matches the bit count of `value`'s underlying type.  For example,
        `bits(np.int8(5))` gives the output `+0000101`, which matches the
        8-bit limit of `np.int8`.  Running `bits(1 / 3)` will return a 64-bit
        IEEE 754 floating point bit string, since that is the default for
        built-in python floats.
    dict[str, str]
        if `value` is a complex number.  The keys of this dictionary are always
        'real' and 'imag', with the associated values matching the binary
        representations of the respective floats that comprise the complex
        number in memory.

    Raises
    ----------------
    TypeError
        if `value` is not either an integer, float, or complex number.
    ValueError
        if `byte_order`/`bit_order`, or `scheme` is not one of the allowed
        values ('big', 'little') or ('sign magnitude', 'ones complement',
        'twos complement'), respectively.
    """
    if byte_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `byte_order` must be either 'big' "
                   f"or 'little', not {repr(byte_order)}")
        raise ValueError(err_msg)
    if bit_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `bit_order` must be either 'big' "
                   f"or 'little', not {repr(bit_order)}")
        raise ValueError(err_msg)
    if scheme not in ("sign magnitude", "ones complement", "twos complement"):
        err_msg = (f"[{error_trace()}] `scheme` must be one of "
                   f"['sign magnitude', 'ones complement', 'twos complement], "
                   f"not {repr(scheme)}")
        raise ValueError(err_msg)

    # integers
    if np.issubdtype(type(value), np.integer):
        return integer_to_bits(value,
                               byte_order=byte_order,
                               bit_order=bit_order,
                               scheme=scheme)

    # floats
    if np.issubdtype(type(value), np.floating):
        return float_to_bits(value,
                             byte_order=byte_order,
                             bit_order=bit_order)

    # complex numbers
    if np.issubdtype(type(value), np.complexfloating):
        return {
            "real": float_to_bits(value.real,
                                  byte_order=byte_order,
                                  bit_order=bit_order),
            "imag": float_to_bits(value.imag,
                                  byte_order=byte_order,
                                  bit_order=bit_order)
        }

    err_msg = (f"[{error_trace()}] `value` must be a numeric integer, float, "
               f"or complex number, not {type(value)}")
    raise TypeError(err_msg)


@lru_cache(maxsize=CACHE_SIZE)
def bits_to_integer(bit_string: str,
                    signed: bool = True,
                    byte_order: str = "big",
                    bit_order: str = "big",
                    scheme: str = "sign magnitude") -> int:
    """Convert an integer bit string to the value it is meant to represent.

    Parameters
    -----------------
    bit_string : str
        integer bit string to be converted.  Must contain only ('0', '1',
        '+', '-') characters, and can be of sign-magnitude, ones' complement,
        or twos' complement form, as specified by `scheme`.
    signed : bool
        if False, attempt to return the associated value as an unsigned integer
        (np.uint8, np.uint16, np.uint32, np.uint64, or built-in int if >64 bit).
        If True, return as a signed integer (np.int8, np.int16, np.int32,
        np.int64, or built-in int if >64-bit).
    byte_order : str
        byte endian-ness.  Can be `'big'` or `'little'`.  If `'big'`, the most
        significant byte will always be on the left.  If `'little'`, it will be
        on the right.
    bit_order : str
        endian-ness of bits within each byte.  Can be `'big'` or `'little'`.
        If `'big'`, the most significant bit within each byte will always be on
        the left.  If `'little'`, it will be on the right.
    scheme : str
        algorithm used to represent signed negative integers.  Can be
        'sign magnitude', 'ones complement', or 'twos complement'.  See
        Wikipedia for more information on how these work.

    Returns
    ----------------
    int
        the integer value associated with `bit_string`, obeying the convention
        set out in `scheme`.  Based on the length of the input `bit_string`,
        the type of this return will always match the minimum bit size
        necessary to accurately represent its value, without overflow.  Padding
        `bit_string` with leading zeros also serves to extend the required bit
        count.  For example, `bits_to_integer('-101')` will return a signed
        `np.int8` with value `-5`, while
        `bits_to_integer('0000000000000101', signed=False)` will return an
        unsigned `np.uint16` with value `5`.

    Raises
    ----------------
    ValueError
        if `bit_string` is not a string containing only ('0', '1', '+', '-')
        characters, or if `byte_order`/`bit_order` or `scheme` is not one of
        the allowed values ('big', 'little') or ('sign magnitude',
        'ones complement', 'twos complement'), respectively.
    RuntimeError
        if `signed=False` and `bit_string` has a negative value.
    """
    if (not isinstance(bit_string, str) or
        any(c not in ("0", "1", "+", "-") for c in bit_string)):
        err_msg = (f"[{error_trace()}] `bit_string` must be a string "
                   f"containing only ['0', '1', '+', '-'] characters "
                   f"(received: {repr(bit_string)})")
        raise ValueError(err_msg)
    if byte_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `byte_order` must be either 'big' "
                   f"or 'little', not {repr(byte_order)}")
        raise ValueError(err_msg)
    if bit_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `bit_order` must be either 'big' "
                   f"or 'little', not {repr(bit_order)}")
        raise ValueError(err_msg)
    if scheme not in ("sign magnitude", "ones complement", "twos complement"):
        err_msg = (f"[{error_trace()}] `scheme` must be one of "
                   f"['sign magnitude', 'ones complement', 'twos complement], "
                   f"not {repr(scheme)}")
        raise ValueError(err_msg)

    # convert bit_string to big endian IEEE 754 format
    big = big_endian(bit_string,
                     byte_order=byte_order,
                     bit_order=bit_order)

    # get value as arbitrarily sized python integer
    if scheme == "sign magnitude" or big.startswith(("+", "0")):
        value = int(big, 2)
    else:
        complement = "".join("0" if c in ("1", "-") else "1" for c in big)
        if scheme == "ones complement":
            value = -1 * int(complement, 2)
        else:
            value = -1 * (int(complement, 2) + 1)

    # unsigned integers
    if not signed:
        if value < 0:
            err_msg = (f"[{error_trace()}] could not convert bits to unsigned "
                       f"integer: {value} < 0")
            raise RuntimeError(err_msg)
        maxima = {
            np.uint8: 2**8 - 1,
            np.uint16: 2**16 - 1,
            np.uint32: 2**32 - 1,
            np.uint64: 2**64 - 1
        }
        for uint_type, max_val in maxima.items():
            if value <= max_val:
                return uint_type(value)
        return value

    # signed integers
    extrema = {
        np.int8: (-2**7, 2**7 - 1),
        np.int16: (-2**15, 2**15 - 1),
        np.int32: (-2**31, 2**31 - 1),
        np.int64: (-2**63, 2**63 - 1)
    }
    for int_type, (min_val, max_val) in extrema.items():
        if min_val <= value <= max_val:
            return int_type(value)
    return value


@lru_cache(maxsize=CACHE_SIZE)
def bits_to_float(bit_string: str,
                  byte_order: str = "big",
                  bit_order: str = "big") -> float:
    """Convert an IEEE 754 floating point bit string to the value it is meant
    to represent.

    Parameters
    -----------------
    bit_string : str
        IEEE 754 floating point bit string to be converted.  Must be length
        16, 32, 64, 96, or 128 based on desired precision, and contain only
        ('0', '1') characters.
    byte_order : str
        byte endian-ness.  Can be `'big'` or `'little'`.  If `'big'`, the most
        significant byte will always be on the left.  If `'little'`, it will be
        on the right.
    bit_order : str
        endian-ness of bits within each byte.  Can be `'big'` or `'little'`.
        If `'big'`, the most significant bit within each byte will always be on
        the left.  If `'little'`, it will be on the right.

    Returns
    ----------------
    float
        the floating point value associated with `bit_string`.  The type of
        this return will always match the bit count of the input `bit_string`
        (np.float16 for length 16 bit strings, np.float32 for length 32 strings
        np.float64 for 64-bit strings, and np.longdouble for 96 or 128-bit
        input).

    Raises
    ----------------
    ValueError
        if `bit_string` is not a string of length (16, 32, 64, 96, or 128)
        and containing only ('0', '1') characters, or if
        `byte_order`/`bit_order` is not one of the allowed values ('big',
        'little').
    """
    if (not isinstance(bit_string, str) or
        any(c not in ("0", "1") for c in bit_string) or
        len(bit_string) not in (16, 32, 64, 96, 128)):
        err_msg = (f"[{error_trace()}] `bit_string` must be a 2-, 4-, 8-, 12-, "
                   f"or 16-byte string containing only binary ['0', '1'] "
                   f"characters (received: {repr(bit_string)})")
        raise ValueError(err_msg)
    if byte_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `byte_order` must be either 'big' "
                   f"or 'little', not {repr(byte_order)}")
        raise ValueError(err_msg)
    if bit_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `bit_order` must be either 'big' "
                   f"or 'little', not {repr(bit_order)}")
        raise ValueError(err_msg)

    if len(bit_string) in (96, 128):  # long double, 96 on 32-bit systems
        big = big_endian(bit_string,
                         byte_order=byte_order,
                         bit_order=bit_order)

        # split into consttuent parts
        sign = int(big[len(big) - 80], 2)
        exponent = int(big[len(big) - 79:len(big) - 64], 2)
        integer = int(big[len(big) - 64], 2)
        fraction = int(big[len(big) - 63:], 2)

        # combine into final float
        mantissa = np.longdouble(fraction) / np.longdouble(2**63) + integer
        exponent_bias = 2**14 - 1  # 15-bit exponent width
        magnitude = np.longdouble(2**(exponent - exponent_bias)) * mantissa
        if sign:
            return -1 * magnitude
        return magnitude

    # 16, 32, 64-bit floats -> unpack using struct package
    float_types = {
        16: (np.float16, "e"),
        32: (np.float32, "f"),
        64: (np.float64, "d")
    }
    ftype, fchar = float_types[len(bit_string)]
    forder = ">" if byte_order == "big" else "<"
    if bit_order == "big":
        byte_array = [int(bit_string[i:i+8], 2)
                      for i in range(0, len(bit_string), 8)]
    else:
        byte_array = [int(bit_string[i+8:i:-1], 2)
                      for i in range(0, len(bit_string), 8)]
    result = struct.unpack(f"{forder}{fchar}", bytes(byte_array))[0]
    return ftype(result)


@lru_cache(maxsize=CACHE_SIZE, typed=True)
def integer_to_bits(integer: int,
                    byte_order: str = "big",
                    bit_order: str = "big",
                    scheme: str = "sign magnitude") -> str:
    """Convert an integer into its equivalent binary representation, as it is
    stored in working memory.

    Parameters
    ----------------
    integer : int
        integer to be converted.  Can be either a built-in int or one of
        numpy's various integer classes, signed or unsigned.
    byte_order : str
        byte endian-ness.  Can be `'big'` or `'little'`.  If `'big'`, the most
        significant byte will always be on the left.  If `'little'`, it will be
        on the right.
    bit_order : str
        endian-ness of bits within each byte.  Can be `'big'` or `'little'`.
        If `'big'`, the most significant bit within each byte will always be on
        the left.  If `'little'`, it will be on the right.
    scheme : str
        algorithm used to represent signed negative integers.  Can be
        'sign magnitude', 'ones complement', or 'twos complement'.  See
        Wikipedia for more information on how these work.

    Returns
    ----------------
    str
        binary representation of `integer`, as is actually stored in memory.
        The length of this string reflects how many bits are allocated to store
        the specified integer, and changes depending on the type of the input
        value.  For example, `integer_to_bits(np.int8(1))` will return a
        length-8 bit string, while `integer_to_bits(np.uint64(1))` will return
        a length-64 bit string.  For integers greater than 64 bit, the python
        pattern is followed, with successive 64-bit blocks added to the number
        to represent its true value.  As such, `integer_to_bits(2**65)` will
        return a length-128 bit string.

    Raises
    ----------------
    TypeError
        if `integer` is not a recognizable integer instance
    ValueError
        if `byte_order`/`bit_order`, or `scheme` is not one of the allowed
        values ('big', 'little') or ('sign magnitude', 'ones complement',
        'twos complement'), respectively.
    """
    if not np.issubdtype(type(integer), np.integer):
        err_msg = (f"[{error_trace()}] `integer` must be an integer "
                   f"(received: {type(integer)})")
        raise TypeError(err_msg)
    if byte_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `byte_order` must be either 'big' "
                   f"or 'little', not {repr(byte_order)}")
        raise ValueError(err_msg)
    if bit_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `bit_order` must be either 'big' "
                   f"or 'little', not {repr(bit_order)}")
        raise ValueError(err_msg)
    if scheme not in ("sign magnitude", "ones complement", "twos complement"):
        err_msg = (f"[{error_trace()}] `scheme` must be one of "
                   f"['sign magnitude', 'ones complement', 'twos complement], "
                   f"not {repr(scheme)}")
        raise ValueError(err_msg)

    # unsigned integers
    if np.issubdtype(type(integer), np.unsignedinteger):
        dtype = np.dtype(type(integer))
        final = f"{np.binary_repr(integer):0>{dtype.itemsize * 8}}"

    # signed integers
    else:
        bit_string = np.binary_repr(integer)
        if bit_string.startswith("-"):
            sign = "-"
            magnitude = bit_string[1:]
        else:
            sign = "+"
            magnitude = bit_string

        # pad to fit memory alignment
        if len(magnitude) <= 63:
            dtype = np.dtype(type(integer))
            padded = f"{sign}{magnitude:0>{8 * dtype.itemsize - 1}}"
        else:  # python appends new 64-bit blocks to represent larger ints
            units = 1 + ((1 + len(magnitude)) // 64)
            padded = f"{sign}{magnitude:0>{64 * units - 1}}"

        # convert based on `scheme`
        if scheme == "sign magnitude":
            final = padded
        elif scheme == "ones complement":
            if sign == "+":
                final = f"0{padded[1:]}"
            else:
                final = "".join("0" if c == "1" else "1" for c in padded)
        else:  # twos complement
            if sign == "+":
                final = f"0{padded[1:]}"
            else:
                ones_comp = "".join("0" if c == "1" else "1" for c in padded)
                final = np.binary_repr(int(ones_comp, 2) + 1)

    # convert to byte strings and reorder to match `byte_order` and `bit_order`
    byte_strings = [final[i:i+8] for i in range(0, len(final), 8)]
    if byte_order == "big":
        if bit_order == "big":
            return final
        return "".join(b[::-1] for b in byte_strings)
    if bit_order == "big":
        return "".join(b for b in reversed(byte_strings))
    return "".join(b[::-1] for b in reversed(byte_strings))


@lru_cache(maxsize=CACHE_SIZE, typed=True)
def float_to_bits(floating_point: float,
                  byte_order: str = "big",
                  bit_order: str = "big") -> str:
    """Convert a floating point number into its equivalent IEEE 754 binary
    representation, as stored in working memory.

    Parameters
    ----------------
    floating_point : float
        floating point number to be converted.  Can be either a built-in float
        or one of numpy's various float classes, including `numpy.longdouble`.
    byte_order : str
        byte endian-ness.  Can be `'big'` or `'little'`.  If `'big'`, the most
        significant byte will always be on the left.  If `'little'`, it will be
        on the right.
    bit_order : str
        endian-ness of bits within each byte.  Can be `'big'` or `'little'`.
        If `'big'`, the most significant bit within each byte will always be on
        the left.  If `'little'`, it will be on the right.

    Returns
    ----------------
    str
        binary representation of `floating_point`, as is actually stored in
        memory. The length of this string reflects how many bits are allocated
        to store the specified floating point number, and changes depending on
        the type of the input value.  For example,
        `float_to_bits(np.float16(1 / 3))` will return a length-16 bit string,
        while `float_to_bits(np.float64(1 / 3))` will return a length-64 bit
        string.  For long doubles, which are platform-specific, the 80-bit
        IEEE 754 content is padded to fit the memory alignment of the system
        (96 bits for 32-bit hardware, 128 bits for 64-bit).  In either case,
        the numerical content of the long double is always contained in the
        last 80 bits of the returned bit string.

    Raises
    ----------------
    TypeError
        if `floating_point` is not a recognizable float instance
    ValueError
        if `byte_order` or `bit_order` is not one of the allowed values
        ('big', 'little').
    """
    if not np.issubdtype(type(floating_point), np.floating):
        err_msg = (f"[{error_trace()}] `floating_point` must be a float "
                   f"(received: {type(floating_point)})")
        raise TypeError(err_msg)
    if byte_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `byte_order` must be either 'big' "
                   f"or 'little', not {repr(byte_order)}")
        raise ValueError(err_msg)
    if bit_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `bit_order` must be either 'big' "
                   f"or 'little', not {repr(bit_order)}")
        raise ValueError(err_msg)

    # struct pacakge cannot interpret long doubles
    if np.issubdtype(type(floating_point), np.longdouble):
        if sys.byteorder == "big":
            byte_array = list(bytes(floating_point))
        else:
            byte_array = reversed(list(bytes(floating_point)))
        byte_strings = [f"{b:08b}" for b in byte_array]
        if byte_order == "big":
            if bit_order == "big":
                return "".join(byte_strings)
            return "".join(b[::-1] for b in byte_strings)
        if bit_order == "big":
            return "".join(reversed(byte_strings))
        return "".join(b[::-1] for b in reversed(byte_strings))

    # 64-bit and under
    dtype = np.dtype(type(floating_point))
    if byte_order == "big":
        struct_format_string = f">{dtype.char}"
    else:
        struct_format_string = f"<{dtype.char}"
    byte_array = list(struct.pack(struct_format_string, floating_point))
    if bit_order == "big":
        return "".join(f"{b:08b}" for b in byte_array)
    return "".join(f"{b:08b}"[::-1] for b in byte_array)


def decompose_integer_bits(bit_string: str,
                           byte_order: str = "big",
                           bit_order: str = "big",
                           scheme: str = "sign magnitude") -> dict[str, str]:
    """Split an integer bit string into sign and magnitude components.

    Parameters
    ----------------
    bit_string : str
        integer bit string to be decomposed.  Must contain only ('0', '1',
        '+', '-') characters, and can be of sign-magnitude, ones' complement,
        or twos' complement form, as specified by `scheme`.
    byte_order : str
        byte endian-ness.  Can be `'big'` or `'little'`.  If `'big'`, the most
        significant byte will always be on the left.  If `'little'`, it will be
        on the right.
    bit_order : str
        endian-ness of bits within each byte.  Can be `'big'` or `'little'`.
        If `'big'`, the most significant bit within each byte will always be on
        the left.  If `'little'`, it will be on the right.
    scheme : str
        algorithm used to represent signed negative integers.  Can be
        'sign magnitude', 'ones complement', or 'twos complement'.  See
        Wikipedia for more information on how these work.

    Returns
    ----------------
    dict[str, str]
        a dictionary describing the constituent parts (sign + magnitude) of
        the associated `bit_string`.  This dictionary always contains the keys
        'sign' and 'magnitude', whose values are '+'/'-', and an unsigned
        binary integer string, respectively.

    Raises
    ----------------
    ValueError
        if `bit_string` is not a string containing only ('0', '1', '+', '-')
        characters, or if `byte_order`/`bit_order` or `scheme` is not one of
        the allowed values ('big', 'little') or ('sign magnitude',
        'ones complement', 'twos complement'), respectively.
    """
    if (not isinstance(bit_string, str) or
        any(c not in ("0", "1", "+", "-") for c in bit_string)):
        err_msg = (f"[{error_trace()}] `bit_string` must be a string "
                   f"(received: {type(bit_string)})")
        raise ValueError(err_msg)
    if byte_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `byte_order` must be either 'big' "
                   f"or 'little', not {repr(byte_order)}")
        raise ValueError(err_msg)
    if bit_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `bit_order` must be either 'big' "
                   f"or 'little', not {repr(bit_order)}")
        raise ValueError(err_msg)
    if scheme not in ("sign magnitude", "ones complement", "twos complement"):
        err_msg = (f"[{error_trace()}] `scheme` must be one of "
                   f"['sign magnitude', 'ones complement', 'twos complement], "
                   f"not {repr(scheme)}")
        raise ValueError(err_msg)

    value = bits_to_integer(bit_string,
                            byte_order=byte_order,
                            bit_order=bit_order,
                            scheme=scheme)
    if value < 0:
        return {
            "sign": "-",
            "magnitude": np.binary_repr(abs(value))}
    return {
        "sign": "+",
        "magnitude": np.binary_repr(value)
    }


def decompose_float_bits(bit_string: str,
                         byte_order: str = "big",
                         bit_order: str = "big") -> dict[str, str]:
    """Split a floating point bit string into sign, exponent, and mantissa.

    Parameters
    ----------------
    bit_string : str
        IEEE 754 floating point bit string to be decomposed.  Must contain only
        ('0', '1') characters and be length 16, 32, 64, 96, or 128, matching
        the memory footprint of half, single, double, and extended precision
        floats, respectively.
    byte_order : str
        byte endian-ness.  Can be `'big'` or `'little'`.  If `'big'`, the most
        significant byte will always be on the left.  If `'little'`, it will be
        on the right.
    bit_order : str
        endian-ness of bits within each byte.  Can be `'big'` or `'little'`.
        If `'big'`, the most significant bit within each byte will always be on
        the left.  If `'little'`, it will be on the right.

    Returns
    ----------------
    dict[str, str]
        a dictionary describing the constituent parts ('sign', 'exponent',
        'mantissa') of the associated `bit_string`.  In the case of long
        doubles (96/128-bit strings), 'mantissa' is replaced with separate
        'integer' and 'fraction' components, to match the IEEE 754 standard.
        Each of these components is represented as an unsigned binary integer
        string, as contained in `bit_string`.

    Raises
    ----------------
    ValueError
        if `bit_string` is not a string of length (16, 32, 64, 96, or 128)
        and containing only ('0', '1') characters, or if
        `byte_order`/`bit_order` is not one of the allowed values ('big',
        'little').
    """
    if (not isinstance(bit_string, str) or
        any(c not in ("0", "1") for c in bit_string) or
        len(bit_string) not in (16, 32, 64, 96, 128)):
        err_msg = (f"[{error_trace()}] `bit_string` must be a 2-, 4-, 8-, 12-, "
                   f"or 16-byte string containing only binary ['0', '1'] "
                   f"characters (received: {repr(bit_string)})")
        raise ValueError(err_msg)
    if byte_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `byte_order` must be either 'big' "
                   f"or 'little', not {repr(byte_order)}")
        raise ValueError(err_msg)
    if bit_order not in ("big", "little"):
        err_msg = (f"[{error_trace()}] `bit_order` must be either 'big' "
                   f"or 'little', not {repr(bit_order)}")
        raise ValueError(err_msg)

    # convert bit_string to big endian IEEE 754 format
    big = big_endian(bit_string,
                     byte_order=byte_order,
                     bit_order=bit_order)

    # IEEE 754 long double is padded and uses an explicit 'integer' bit
    if len(big) > 64:
        return {  # only the last 80 bits are relevant
            "sign": big[len(big) - 80],  # sign (1)
            "exponent": big[len(big) - 79:len(big) - 64],  # exponent (15)
            "integer": big[len(big) - 64],  # integer (1)
            "fraction": big[len(big) - 63:]  # fraction (63)
        }

    # IEEE 754 floating point bit widths:
    #   fp16 -> sign (1), exponent (5), mantissa (10)
    #   fp32 -> sign (1), exponent (8), mantissa (23)
    #   fp64 -> sign (1), exponent (11), mantissa (52)
    exponent_length = {
        16: 5,
        32: 8,
        64: 11
    }
    return {
        "sign": big[0],
        "exponent": big[1:1 + exponent_length[len(big)]],
        "mantissa": big[1 + exponent_length[len(big)]:]
    }


def downcast(value: int | float | complex,
             signed: bool = True) -> int | float | complex:
    # integers
    if np.issubdtype(type(value), np.integer):
        return downcast_integer(value, signed)

    # floats
    if np.issubdtype(type(value), np.floating):
        return downcast_float(value)

    # complex numbers
    if np.issubdtype(type(value), np.complexfloating):
        return downcast_complex(value)

    err_msg = (f"[{error_trace()}] `value` must be a numeric integer, float, "
               f"or complex number, not {type(value)}")
    raise TypeError(err_msg)


@lru_cache(maxsize=CACHE_SIZE, typed=True)
def downcast_integer(integer: int, signed: bool = True) -> int:
    if not np.issubdtype(type(integer), np.integer):
        err_msg = (f"[{error_trace()}] `integer` must be an integer, not "
                   f"{type(integer)}")
        raise TypeError(err_msg)

    # unsigned
    if not signed:
        if integer < 0:
            err_msg = (f"[{error_trace()}] when `signed=False`, `integer` "
                       f"must be positive (received: {integer})")
            raise ValueError(err_msg)
        maxima = {
            np.uint8: 2**8 - 1,
            np.uint16: 2**16 - 1,
            np.uint32: 2**32 - 1,
            np.uint64: 2**64 - 1
        }
        for uint_type, max_val in maxima.items():
            if integer < max_val:
                return uint_type(integer)
        err_msg = (f"[{error_trace()}] could not downcast integer: {integer}")
        raise ValueError(err_msg)

    # signed
    extrema = {
        np.int8: (-2**7, 2**7 - 1),
        np.int16: (-2**15, 2**15 - 1),
        np.int32: (-2**31, 2**31 - 1),
        np.int64: (-2**63, 2**63 - 1)
    }
    for int_type, (min_val, max_val) in extrema.items():
        if min_val <= integer <= max_val:
            return int_type(integer)
    err_msg = (f"[{error_trace()}] could not downcast integer: {integer}")
    raise ValueError(err_msg)


@lru_cache(maxsize=CACHE_SIZE, typed=True)
def downcast_float(floating_point: float) -> float:
    """test"""
    if not np.issubdtype(type(floating_point), np.floating):
        err_msg = (f"[{error_trace()}] `floating_point` must be a float, not "
                   f"{type(floating_point)}")
        raise TypeError(err_msg)

    # 0, Inf, nan special cases
    if (not floating_point or
        np.isinf(floating_point) or
        np.isnan(floating_point)):
        return np.float16(floating_point)

    # convert exponent and mantissa to integers, correcting for bias
    bit_string = float_to_bits(floating_point)
    components = decompose_float_bits(bit_string)
    if len(bit_string) > 64:  # long double special case
        if components["integer"] == "0":
            # renormalize by increasing the fraction and reducing the exponent
            # until we reach a 1 in the integer place
            carry = components["fraction"].find("1") + 1
            exponent = int(components["exponent"], 2) - (2**14 - 1 + carry)
            mantissa = components["fraction"][carry:] + "0" * carry
        else:
            exponent = int(components["exponent"], 2) - (2**14 - 1)
            mantissa = components["fraction"]
    else:
        exp_bias = {
            16: 2**4 - 1,
            32: 2**7 - 1,
            64: 2**10 - 1
        }
        exponent = int(components["exponent"], 2) - exp_bias[len(bit_string)]
        mantissa = components["mantissa"]

    widths = {
        np.float16: (-2**4 + 2, 2**4 - 1, 10),
        np.float32: (-2**7 + 2, 2**7 - 1, 23),
        np.float64: (-2**10 + 2, 2**10 - 1, 52),
        np.longdouble: (-2**14 + 2, 2**10 - 1, 63),
    }
    for float_type, (min_exp, max_exp, man_width) in widths.items():
        mask = mantissa[man_width:]
        if min_exp <= exponent <= max_exp and (not mask or not int(mask, 2)):
            return float_type(floating_point)
    err_msg = (f"[{error_trace()}] could not downcast float: {floating_point}")
    raise ValueError(err_msg)


@lru_cache(maxsize=CACHE_SIZE, typed=True)
def downcast_complex(complex_number: complex) -> complex:
    if not np.issubdtype(type(complex_number), np.complexfloating):
        err_msg = (f"[{error_trace()}] `complex_number` must be a complex "
                   f"type, not {type(complex_number)}")
        raise TypeError(err_msg)

    # downcast real and imaginary components separately, then choose largest
    real = downcast_float(complex_number.real)
    imag = downcast_float(complex_number.imag)
    largest = max((real, imag), key=lambda x: np.dtype(x).itemsize)
    conversion_map = {
        np.float16: np.complex64,
        np.float32: np.complex64,
        np.float64: np.complex128,
        np.longdouble: np.clongdouble
    }
    return conversion_map[type(largest)](complex_number)









def downcast_int_dtype(min_val: int, max_val: int, dtype: type) -> type:
    """_summary_

    Args:
        min_val (int): _description_
        max_val (int): _description_
        dtype (type): _description_

    Returns:
        type: _description_
    """
    if dtype.itemsize == 1:
        return dtype

    # get type hierarchy
    if pd.api.types.is_extension_array_dtype(dtype):
        if pd.api.types.is_unsigned_integer_dtype(dtype):
            type_hierarchy = {
                8: pd.UInt64Dtype(),
                4: pd.UInt32Dtype(),
                2: pd.UInt16Dtype(),
                1: pd.UInt8Dtype()
            }
        else:
            type_hierarchy = {
                8: pd.Int64Dtype(),
                4: pd.Int32Dtype(),
                2: pd.Int16Dtype(),
                1: pd.Int8Dtype()
            }
    else:
        if pd.api.types.is_unsigned_integer_dtype(dtype):
            type_hierarchy = {
                8: np.dtype(np.uint64),
                4: np.dtype(np.uint32),
                2: np.dtype(np.uint16),
                1: np.dtype(np.uint8)
            }
        else:
            type_hierarchy = {
                8: np.dtype(np.int64),
                4: np.dtype(np.int32),
                2: np.dtype(np.int16),
                1: np.dtype(np.int8)
            }

    # check for smaller dtypes that fit given range
    size = dtype.itemsize
    selected = dtype
    while size > 1:
        test = type_hierarchy[size // 2]
        size = test.itemsize
        if pd.api.types.is_unsigned_integer_dtype(test):
            min_poss = 0
            max_poss = 2**(8 * test.itemsize) - 1
        else:
            min_poss = -2**(8 * test.itemsize - 1)
            max_poss = 2**(8 * test.itemsize - 1) - 1
        if min_val >= min_poss and max_val <= max_poss:
            selected = test
    return selected


# def int_to_bin(input_array: int | np.ndarray) -> np.ndarray:
#     """Turn a 1D array of integers into a 2D array containing their binary
#     representations.
#     """
#     input_array = np.array(input_array)
#     reshaped = input_array.reshape(-1, 1)
#     mask = 2**np.arange(8 * input_array.dtype.itemsize)
#     return ((reshaped & mask) != 0).astype(int)[:,::-1]


# def float_to_bin(input_array: float | np.ndarray) -> np.ndarray:
#     """Turn a 1D array of floats into a 2D array containing their binary
#     representations.
#     """
#     input_array = np.array(input_array)
#     byte_str = input_array.tobytes()
#     byte_array = np.array([list(reversed(byte_str[i:i + 8])) for i in range(0, len(byte_str), 8)],
#                           dtype=np.uint8)
#     bit_array = np.unpackbits(byte_array, axis=1)
#     return bit_array



def to_sparse(series: pd.Series) -> pd.Series:
    """Convert series into a sparse series, masking the most frequent value."""
    # TODO: doesn't work for extension dtypes with fill_value != pd.NA
    # value_counts(dropna=False) causes integers to be converted to float if dtype="O"
    # TODO: sparse array protocol type strings: "Sparse[int]"
    counts = series.value_counts()
    val = counts.index[0]
    if series.hasnans:
        # count nans separately
        nan_counts = series[series.isna()].value_counts(dropna=False)
        if nan_counts.iloc[0] >= counts.iloc[0]:
            val = nan_counts.index[0]
    return series.astype(pd.SparseDtype(series.dtype, val))
