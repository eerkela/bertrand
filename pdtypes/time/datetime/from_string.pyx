import datetime

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd


# TODO: J2000 astronomical dates


# TODO: this should be private and encapsulated in string_to_datetime

def iso_8601_strings_to_pydatetime(
    np.ndarray[object] arr: np.ndarray
) -> np.ndarray:
    """test"""
    # convert ISO strings to M8[us]
    arr = arr.astype("M8[us]")

    # check for overflow
    min_val = arr.min()
    max_val = arr.max()
    min_datetime = np.datetime64(datetime.datetime.min)
    max_datetime = np.datetime64(datetime.datetime.max)
    if min_val < min_datetime or max_val > max_datetime:
        raise ValueError()

    # convert to datetime.datetime
    return arr.astype("O")