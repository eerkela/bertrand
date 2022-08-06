import unittest

import pandas as pd

from context import pdtypes
import pdtypes.cast_old_2


class CastIntegerToTimedeltaAccuracyTests(unittest.TestCase):

    ################################
    ####    Generic Integers    ####
    ################################

    def test_integer_to_timedelta_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_timedelta(input_series)

        # Assert
        expected = pd.to_timedelta(input_series, unit="s")
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_timedelta_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers + [None])

        # Act
        result = pdtypes.cast.integer_to_timedelta(input_series)

        # Assert
        expected = pd.to_timedelta(input_series, unit="s")
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_timedelta_only_na(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.integer_to_timedelta(input_series)

        # Assert
        expected = pd.to_timedelta(input_series, unit="s")
        pd.testing.assert_series_equal(result, expected)


class CastIntegertoTimedeltaUnitConversionTests(unittest.TestCase):

    ################################
    ####    Unit Conversions    ####
    ################################

    def test_integer_to_timedelta_ns_precision(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_timedelta(input_series, unit="ns")

        # Assert
        expected = pd.to_timedelta(input_series, unit="ns")
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_timedelta_us_precision(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_timedelta(input_series, unit="us")

        # Assert
        expected = pd.to_timedelta(input_series, unit="us")
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_timedelta_ms_precision(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_timedelta(input_series, unit="ms")

        # Assert
        expected = pd.to_timedelta(input_series, unit="ms")
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_timedelta_second_precision(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_timedelta(input_series, unit="s")

        # Assert
        expected = pd.to_timedelta(input_series, unit="s")
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_timedelta_minute_precision(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_timedelta(input_series, unit="m")

        # Assert
        expected = pd.to_timedelta(input_series, unit="m")
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_timedelta_hour_precision(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_timedelta(input_series, unit="h")

        # Assert
        expected = pd.to_timedelta(input_series, unit="h")
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_timedelta_day_precision(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_timedelta(input_series, unit="d")

        # Assert
        expected = pd.to_timedelta(input_series, unit="d")
        pd.testing.assert_series_equal(result, expected)


if __name__ == "__main__":
    unittest.main()
