import unittest

import pandas as pd

from context import pdtypes
import pdtypes.cast_old_2


class CastIntegerToDatetimeAccuracyTests(unittest.TestCase):

    ################################
    ####    Generic Integers    ####
    ################################

    def test_integer_to_datetime_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_datetime(input_series)

        # Assert
        expected = pd.to_datetime(input_series, unit="s", utc=True)
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_datetime_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers + [None])

        # Act
        result = pdtypes.cast.integer_to_datetime(input_series)

        # Assert
        expected = pd.to_datetime(input_series, unit="s", utc=True)
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_datetime_only_na(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.integer_to_datetime(input_series)

        # Assert
        expected = pd.to_datetime(input_series, unit="s", utc=True)
        pd.testing.assert_series_equal(result, expected)


class CastIntegertoDatetimeUnitConversionTests(unittest.TestCase):

    ###############################
    ####    Unit Conversion    ####
    ###############################

    def test_integer_to_datetime_ns_precision(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_datetime(input_series, unit="ns")

        # Assert
        expected = pd.to_datetime(input_series, unit="ns", utc=True)
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_datetime_us_precision(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_datetime(input_series, unit="us")

        # Assert
        expected = pd.to_datetime(input_series, unit="us", utc=True)
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_datetime_ms_precision(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_datetime(input_series, unit="ms")

        # Assert
        expected = pd.to_datetime(input_series, unit="ms", utc=True)
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_datetime_second_precision(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_datetime(input_series, unit="s")

        # Assert
        expected = pd.to_datetime(input_series, unit="s", utc=True)
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_datetime_minute_precision(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_datetime(input_series, unit="m")

        # Assert
        expected = pd.to_datetime(input_series, unit="m", utc=True)
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_datetime_hour_precision(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_datetime(input_series, unit="h")

        # Assert
        expected = pd.to_datetime(input_series, unit="h", utc=True)
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_datetime_day_precision(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_datetime(input_series, unit="d")

        # Assert
        expected = pd.to_datetime(input_series, unit="d", utc=True)
        pd.testing.assert_series_equal(result, expected)


if __name__ == "__main__":
    unittest.main()
