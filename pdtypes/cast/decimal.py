from __future__ import annotations
import decimal

import numpy as np
import pandas as pd


def round_decimal(
    dec: decimal.Decimal | np.ndarray | pd.Series,
    rounding: str
) -> decimal.Decimal | np.ndarray | pd.Series:
    """test"""
    if rounding == "down":
        return dec // 1
    switch = {  # C-style switch statement
        "floor": lambda x: x.quantize(1, decimal.ROUND_FLOOR),
        "ceiling": lambda x: x.quantize(1, decimal.ROUND_CEILING),
        "up": lambda x: x.quantize(1, decimal.ROUND_UP),
        "half_floor": (lambda x: x.quantize(1, decimal.ROUND_HALF_UP)
                                 if x < 0 else
                                 x.quantize(1, decimal.ROUND_HALF_DOWN)),
        "half_ceiling": (lambda x: x.quantize(1, decimal.ROUND_HALF_DOWN)
                                   if x < 0 else
                                   x.quantize(1, decimal.ROUND_HALF_UP)),
        "half_down": lambda x: x.quantize(1, decimal.ROUND_HALF_DOWN),
        "half_up": lambda x: x.quantize(1, decimal.ROUND_HALF_UP),
        "half_even": lambda x: x.quantize(1, decimal.ROUND_HALF_EVEN)
    }
    return np.frompyfunc(switch[rounding], 1, 1)(dec)
