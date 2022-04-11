from datetime import datetime, timedelta, timezone
import random
import unittest

import pandas as pd
from pandas.testing import assert_series_equal
import pytz

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyToIntegerAccuracyTests(unittest.TestCase):

    ##################################
    ####    Integer to Integer    ####
    ##################################

    def test_apply_integer_to_integer_no_na(self):
        data = [-2, -1, 0, 1, 2]
        series = pd.Series(data)
        expected = series.copy()
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_integer_to_integer_with_na(self):
        data = [-2, -1, 0, 1, 2]
        series = pd.Series(data + [None])
        expected = series.copy()
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    ################################
    ####    Float to Integer    ####
    ################################

    def test_apply_whole_float_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [float(i) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_float_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [float(i) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_decimal_float_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [i + random.random() for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert float to "
                   f"int: {repr(series[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_decimal_float_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [i + random.random() for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_decimal_float_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [i + random.random() for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    ##################################
    ####    Complex to Integer    ####
    ##################################

    def test_apply_real_whole_complex_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [complex(i, 0) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_real_whole_complex_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [complex(i, 0) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_real_decimal_complex_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [complex(i + random.random(), 0) for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert complex to "
                   f"int: {repr(series[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_real_decimal_complex_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [complex(i + random.random(), 0) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_real_decimal_complex_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [complex(i + random.random(), 0) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_imaginary_whole_complex_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [complex(i, 1) for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert complex to "
                   f"int: {repr(series[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_imaginary_whole_complex_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [complex(i, 1) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_imaginary_whole_complex_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [complex(i, 1) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_imaginary_decimal_complex_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [complex(i + random.random(), 1) for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert complex to "
                   f"int: {repr(series[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_imaginary_decimal_complex_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [complex(i + random.random(), 1) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_imaginary_decimal_complex_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [complex(i + random.random(), 1) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    #################################
    ####    String to Integer    ####
    #################################

    ## Integer strings

    def test_apply_integer_string_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(i) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_integer_string_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(i) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    ## Float strings

    def test_apply_whole_float_string_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(float(i)) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_float_string_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(float(i)) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_decimal_float_string_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(i + random.random()) for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(series[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_decimal_float_string_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(i + random.random()) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_decimal_float_string_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(i + random.random()) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    ## Complex strings

    def test_apply_real_whole_complex_string_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(complex(i, 0)) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_real_whole_complex_string_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(complex(i, 0)) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_real_decimal_complex_string_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(complex(i + random.random(), 0)) for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(series[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_real_decimal_complex_string_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(complex(i + random.random(), 0)) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_real_decimal_complex_String_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(complex(i + random.random(), 0)) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_imaginary_whole_complex_string_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(complex(i, 1)) for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(series[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_imaginary_whole_complex_string_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(complex(i, 1)) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_imaginary_whole_complex_string_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(complex(i, 1)) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_imaginary_decimal_complex_string_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(complex(i + random.random(), 1)) for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(series[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_imaginary_decimal_complex_string_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(complex(i + random.random(), 1)) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_imaginary_decimal_complex_string_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(complex(i + random.random(), 1)) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    ## Character strings

    def test_apply_character_string_to_integer_error(self):
        data = ["a", "b", "c", "d", "e"]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(series[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_character_string_to_integer_forced_error(self):
        data = ["a", "b", "c", "d", "e"]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(series[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer, force=True)
        self.assertEqual(str(err.exception), err_msg)

    ## Boolean strings

    def test_apply_boolean_string_to_integer_no_na(self):
        data = [True, False, True, False, True]
        series = pd.Series([str(b) for b in data])
        expected = pd.Series([int(b) for b in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_boolean_string_to_integer_with_na(self):
        data = [True, False, True, False, True]
        series = pd.Series([str(b) for b in data] + [None])
        expected = pd.Series([int(b) for b in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    ## Datetime strings

    def test_apply_whole_timestamp_aware_datetime_string_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(datetime.fromtimestamp(i, timezone.utc)) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_datetime_string_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(datetime.fromtimestamp(i, timezone.utc)) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_naive_datetime_string_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(datetime.fromtimestamp(i)) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_naive_datetime_string_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(datetime.fromtimestamp(i)) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_naive_datetime_string_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(datetime.fromtimestamp(i, timezone.utc)) if i % 2
                else str(datetime.fromtimestamp(i)) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_naive_datetime_string_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(datetime.fromtimestamp(i, timezone.utc)) if i % 2
                else str(datetime.fromtimestamp(i)) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_mixed_tz_datetime_string_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [str(datetime.fromtimestamp(i, pytz.timezone(get_tz(i))))
                for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_mixed_tz_datetime_string_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [str(datetime.fromtimestamp(i, pytz.timezone(get_tz(i))))
                for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_ISO_8601_string_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i, timezone.utc).isoformat()
                for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_ISO_8601_string_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i, timezone.utc).isoformat()
                for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_naive_ISO_8601_string_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i).isoformat() for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_naive_ISO_8601_string_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i).isoformat() for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_naive_ISO_8601_string_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i, timezone.utc).isoformat() if i % 2
                else datetime.fromtimestamp(i).isoformat() for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_naive_ISO_8601_string_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i, timezone.utc).isoformat() if i % 2
                else datetime.fromtimestamp(i).isoformat() for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_mixed_tz_ISO_8601_string_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i, pytz.timezone(get_tz(i))).isoformat()
                for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_mixed_tz_ISO_8601_string_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i, pytz.timezone(get_tz(i))).isoformat()
                for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_datetime_string_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(datetime.fromtimestamp(i + random.random(), timezone.utc))
                for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(str(data[0]))}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_aware_datetime_string_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(datetime.fromtimestamp(i + random.random(), timezone.utc))
                for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_datetime_string_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(datetime.fromtimestamp(i + random.random(), timezone.utc))
                for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_naive_datetime_string_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(datetime.utcfromtimestamp(i + random.random()))
                for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(str(data[0]))}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_naive_datetime_string_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(datetime.utcfromtimestamp(i + random.random()))
                for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_naive_datetime_string_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(datetime.utcfromtimestamp(i + random.random()))
                for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_naive_datetime_string_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(datetime.fromtimestamp(i + random.random(), timezone.utc))
                if i % 2 else str(datetime.fromtimestamp(i + random.random()))
                for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(str(data[0]))}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_aware_naive_datetime_string_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(datetime.fromtimestamp(i + random.random(), timezone.utc))
                if i % 2 else str(datetime.fromtimestamp(i + random.random()))
                for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_naive_datetime_string_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(datetime.fromtimestamp(i + random.random(), timezone.utc))
                if i % 2 else str(datetime.fromtimestamp(i + random.random()))
                for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_mixed_tz_datetime_string_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [str(datetime.fromtimestamp(i + random.random(),
                                           pytz.timezone(get_tz(i))))
                for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(str(data[0]))}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_mixed_tz_datetime_string_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [str(datetime.fromtimestamp(i + random.random(),
                                           pytz.timezone(get_tz(i))))
                for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_mixed_tz_datetime_string_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [str(datetime.fromtimestamp(i + random.random(),
                                           pytz.timezone(get_tz(i))))
                for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_ISO_8601_string_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc) \
                        .isoformat()
                for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(data[0].isoformat())}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_aware_ISO_8601_string_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc) \
                        .isoformat()
                for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_ISO_8601_string_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc) \
                        .isoformat()
                for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_naive_ISO_8601_string_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i + random.random()).isoformat()
                for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(data[0].isoformat())}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_naive_ISO_8601_string_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i + random.random()).isoformat()
                for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_naive_ISO_8601_string_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i + random.random()).isoformat()
                for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_naive_ISO_8601_string_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc) \
                        .isoformat() if i % 2
                else datetime.fromtimestamp(i + random.random()).isoformat()
                for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(str(data[0]))}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_aware_naive_ISO_8601_string_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc) \
                        .isoformat() if i % 2
                else datetime.fromtimestamp(i + random.random()).isoformat()
                for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_naive_ISO_8601_string_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc) \
                        .isoformat() if i % 2
                else datetime.fromtimestamp(i + random.random()).isoformat()
                for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_mixed_tz_ISO_8601_string_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i + random.random(),
                                       pytz.timezone(get_tz(i))).isoformat()
                for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(str(data[0]))}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_mixed_tz_ISO_8601_string_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i + random.random(),
                                       pytz.timezone(get_tz(i))).isoformat()
                for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_mixed_tz_ISO_8601_string_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i + random.random(),
                                       pytz.timezone(get_tz(i))).isoformat()
                for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    ## Timedelta strings

    def test_apply_whole_seconds_timedelta_string_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(timedelta(seconds=i)) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_seconds_timedelta_string_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(timedelta(seconds=i)) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_random_seconds_timedelta_string_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(timedelta(seconds=i + random.random())) for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(str(data[0]))}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_seconds_timedelta_string_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(timedelta(seconds=i + random.random())) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_seconds_timedelta_string_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [str(timedelta(seconds=i + random.random())) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    ##################################
    ####    Boolean to Integer    ####
    ##################################

    def test_apply_boolean_to_integer_no_na(self):
        data = [True, False, True, False, True]
        series = pd.Series(data)
        expected = pd.Series([int(b) for b in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_boolean_to_integer_with_na(self):
        data = [True, False, True, False, True]
        series = pd.Series(data + [None])
        expected = pd.Series([int(b) for b in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    ###################################
    ####    Datetime to Integer    ####
    ###################################

    def test_apply_whole_timestamp_aware_datetime_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_datetime_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_naive_datetime_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_naive_datetime_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_naive_datetime_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i, timezone.utc) if i % 2
                else datetime.fromtimestamp(i) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_naive_datetime_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i, timezone.utc) if i % 2
                else datetime.fromtimestamp(i) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_mixed_tz_datetime_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i, pytz.timezone(get_tz(i)))
                for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_mixed_tz_datetime_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i, pytz.timezone(get_tz(i)))
                for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_datetime_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert datetime to "
                   f"int: {repr(series[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_aware_datetime_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_datetime_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_naive_datetime_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.utcfromtimestamp(i + random.random())
                for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert datetime to "
                   f"int: {repr(series[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_naive_datetime_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.utcfromtimestamp(i + random.random())
                for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_naive_datetime_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.utcfromtimestamp(i + random.random())
                for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_naive_datetime_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                if i % 2 else datetime.fromtimestamp(i + random.random())
                for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert datetime to "
                   f"int: {repr(series[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_aware_naive_datetime_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                if i % 2 else datetime.fromtimestamp(i + random.random())
                for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_naive_datetime_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                if i % 2 else datetime.fromtimestamp(i + random.random())
                for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_mixed_tz_datetime_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i + random.random(),
                                       pytz.timezone(get_tz(i)))
                for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert datetime to "
                   f"int: {repr(series[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_mixed_tz_datetime_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i + random.random(),
                                       pytz.timezone(get_tz(i)))
                for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_mixed_tz_datetime_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i + random.random(),
                                       pytz.timezone(get_tz(i)))
                for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    ####################################
    ####    Timedelta to Integer    ####
    ####################################

    def test_apply_whole_seconds_timedelta_to_integer_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [timedelta(seconds=i) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_seconds_timedelta_to_integer_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [timedelta(seconds=i) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_random_seconds_timedelta_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        data = [timedelta(seconds=i + random.random()) for i in integers]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert timedelta "
                   f"to int: {repr(series[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_seconds_timedelta_to_integer_forced_no_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [timedelta(seconds=i + random.random()) for i in integers]
        series = pd.Series(data)
        expected = pd.Series(integers)
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_seconds_timedelta_to_integer_forced_with_na(self):
        integers = [-2, -1, 0, 1, 2]
        data = [timedelta(seconds=i + random.random()) for i in integers]
        series = pd.Series(data + [None])
        expected = pd.Series(integers + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    #################################
    ####    Object to Integer    ####
    #################################

    def test_apply_all_nones_to_integer(self):
        data = [None, None, None, None, None]
        series = pd.DataFrame(data)
        expected = series.copy()
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_castable_object_to_integer_no_na(self):
        class CastableObject:
            def __init__(self, integer):
                self.integer = integer

            def __int__(self):
                return self.integer

        data = [CastableObject(i) for i in range(5)]
        series = pd.Series(data)
        expected = pd.Series([int(o) for o in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_castable_object_to_integer_with_na(self):
        class CastableObject:
            def __init__(self, integer):
                self.integer = integer

            def __int__(self):
                return self.integer

        data = [CastableObject(i) for i in range(5)]
        series = pd.Series(data + [None])
        expected = pd.Series([int(o) for o in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_non_castable_object_to_integer_error(self):
        class NonCastableObject:
            pass

        data = [NonCastableObject() for _ in range(5)]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert object "
                   f"to int: {repr(series[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)


class ApplyToIntegerReversibilityTests(unittest.TestCase):

    def test_apply_integer_to_float_to_integer_reversible_no_na(self):
        start = pd.Series([-2, -1, 0, 1, 2])
        mid = start.apply(pdtypes.apply_to_float)
        end = mid.apply(pdtypes.apply.to_integer)
        assert_series_equal(end, start)

    def test_apply_integer_to_float_to_integer_reversible_with_na(self):
        start = pd.Series([-2, -1, 0, 1, 2] + [None])
        mid = start.apply(pdtypes.apply_to_float)
        end = mid.apply(pdtypes.apply.to_integer)
        assert_series_equal(end, start)

    def test_apply_integer_to_complex_to_integer_reversible_no_na(self):
        start = pd.Series([-2, -1, 0, 1, 2])
        mid = start.apply(pdtypes.apply_to_complex)
        end = mid.apply(pdtypes.apply.to_integer)
        assert_series_equal(end, start)

    def test_apply_integer_to_complex_to_integer_reversible_with_na(self):
        start = pd.Series([-2, -1, 0, 1, 2] + [None])
        mid = start.apply(pdtypes.apply_to_complex)
        end = mid.apply(pdtypes.apply.to_integer)
        assert_series_equal(end, start)

    def test_apply_integer_to_string_to_integer_reversible_no_na(self):
        start = pd.Series([-2, -1, 0, 1, 2])
        mid = start.apply(pdtypes.apply_to_string)
        end = mid.apply(pdtypes.apply.to_integer)
        assert_series_equal(end, start)

    def test_apply_integer_to_string_to_integer_reversible_with_na(self):
        start = pd.Series([-2, -1, 0, 1, 2] + [None])
        mid = start.apply(pdtypes.apply_to_string)
        end = mid.apply(pdtypes.apply.to_integer)
        assert_series_equal(end, start)

    def test_apply_integer_to_bool_to_integer_reversible_no_na(self):
        start = pd.Series([1, 0, 1, 0, 1])
        mid = start.apply(pdtypes.apply_to_bool)
        end = mid.apply(pdtypes.apply.to_integer)
        assert_series_equal(end, start)

    def test_apply_integer_to_bool_to_integer_reversible_with_na(self):
        start = pd.Series([1, 0, 1, 0, 1] + [None])
        mid = start.apply(pdtypes.apply_to_bool)
        end = mid.apply(pdtypes.apply.to_integer)
        assert_series_equal(end, start)

    def test_apply_integer_to_datetime_to_integer_reversible_no_na(self):
        start = pd.Series([-2, -1, 0, 1, 2])
        mid = start.apply(pdtypes.apply_to_datetime)
        end = mid.apply(pdtypes.apply.to_integer)
        assert_series_equal(end, start)

    def test_apply_integer_to_datetime_to_integer_reversible_with_na(self):
        start = pd.Series([-2, -1, 0, 1, 2] + [None])
        mid = start.apply(pdtypes.apply_to_datetime)
        end = mid.apply(pdtypes.apply.to_integer)
        assert_series_equal(end, start)

    def test_apply_integer_to_timedelta_to_integer_reversible_no_na(self):
        start = pd.Series([-2, -1, 0, 1, 2])
        mid = start.apply(pdtypes.apply_to_timedelta)
        end = mid.apply(pdtypes.apply.to_integer)
        assert_series_equal(end, start)

    def test_apply_integer_to_timedelta_to_integer_reversible_with_na(self):
        start = pd.Series([-2, -1, 0, 1, 2] + [None])
        mid = start.apply(pdtypes.apply_to_timedelta)
        end = mid.apply(pdtypes.apply.to_integer)
        assert_series_equal(end, start)

    def test_apply_integer_to_object_to_integer_reversible_no_na(self):
        raise NotImplementedError()

    def test_apply_integer_to_object_to_integer_reversible_with_na(self):
        raise NotImplementedError()


if __name__ == "__main__":
    unittest.main()
        