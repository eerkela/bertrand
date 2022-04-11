from __future__ import annotations
import sys

import numpy as np

from pdtypes.error import error_trace


def bits(observation: float) -> str:
    dtype = np.dtype(type(observation))
    if dtype.byteorder == "=":
        if sys.byteorder == "big":
            byte_string = bytes(observation)
        else:
            byte_string = reversed(bytes(observation))
    elif dtype.byteorder == "<":  # x86, AMD64
        byte_string = reversed(bytes(observation))
    elif dtype.byteorder == ">":
        byte_string = bytes(observation)
    else:
        err_msg = (f"[{error_trace()}] could not interpret byte order: "
                   f"{dtype.byteorder}")
        raise ValueError(err_msg)
    return "".join([f"{b:08b}" for b in list(byte_string)])


def downcast_float(f: float) -> float:
    # IEEE 754 floating point bit layouts:
    #   fp16 -> sign (1), exponent (5), mantissa (10)
    #   fp32 -> sign (1), exponent (8), mantissa (23)
    #   fp64 -> sign (1), exponent (11), mantissa (52)
    #   fp128 -> sign (1), exponent (15), mantissa (112)
    bit_str = bits(f)
    if len(bit_str) == 64:
        exponent = int(bit_str[1:12], 2) - (2**10 - 1)  # bias
        mantissa = int(bit_str[12:], 2)
        if not exponent >> 5 and not mantissa & int("0" * 10 + "1" * 42, 2):
            return f.astype(np.float16)
        if not exponent >> 8 and not mantissa & int("0" * 23 + "1" * 29, 2):
            return f.astype(np.float32)
    elif len(bit_str) == 32:
        exponent = int(bit_str[1:9], 2) - (2**7 - 1)  # bias
        mantissa = int(bit_str[9:], 2)
        if not exponent >> 5 and not mantissa & int("0" * 10 + "1" * 13, 2):
            return f.astype(np.float16)
    return f
