"""Mypy stubs for pdcast/util/numeric.pyx"""
from numbers import Real
from typing import Any

import numpy as np
import pandas as pd

from pdcast.types import CompositeType, ScalarType
from pdcast.util.round import Tolerance


def boundscheck(
    series: pd.Series[Any], target: ScalarType, tol: Tolerance, errors: str
) -> pd.Series[Any]:
    ...

def downcast_integer(
    series: pd.Series[int], tol: Tolerance, smallest: CompositeType
) -> pd.Series[int]:
    ...

def downcast_float(
    series: pd.Series[float], tol: Tolerance, smallest: CompositeType
) -> pd.Series[float]:
    ...

def downcast_complex(
    series: pd.Series[complex], tol: Tolerance, smallest: CompositeType
) -> pd.Series[complex]:
    ...

def isinf(series: pd.Series[Any]) -> np.ndarray[np.bool_, np.dtype[np.bool_]]: ...

def real(series: pd.Series[Any]) -> pd.Series[Any]: ...

def imag(series: pd.Series[Any]) -> pd.Series[Any]: ...

def within_tol(
    series1: pd.Series[Any], series2: pd.Series[Any], tol: Real
) -> np.ndarray[np.bool_, np.dtype[np.bool_]]:
    ...
