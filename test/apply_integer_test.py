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
        data = [-2.0, -1.0, 0.0, 1.0, 2.0]
        series = pd.Series(data)
        expected = pd.Series([int(f) for f in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_float_to_integer_with_na(self):
        data = [-2.0, -1.0, 0.0, 1.0, 2.0]
        series = pd.Series(data + [None])
        expected = pd.Series([int(f) for f in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_decimal_float_to_integer_error(self):
        data = [i + random.random() for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert float to "
                   f"int: {repr(data[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_decimal_float_to_integer_forced_no_na(self):
        data = [i + random.random() for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        expected = pd.Series([int(f) for f in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_decimal_float_to_integer_forced_with_na(self):
        data = [i + random.random() for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data + [None])
        expected = pd.Series([int(f) for f in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    ##################################
    ####    Complex to Integer    ####
    ##################################

    def test_apply_real_whole_complex_to_integer_no_na(self):
        data = [complex(i, 0) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        expected = pd.Series([int(c.real) for c in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_real_whole_complex_to_integer_with_na(self):
        data = [complex(i, 0) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data + [None])
        expected = pd.Series([int(c.real) for c in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_real_decimal_complex_to_integer_error(self):
        data = [complex(i + random.random(), 0) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert complex to "
                   f"int: {repr(data[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_imaginary_whole_complex_to_integer_error(self):
        data = [complex(i, 1) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert complex to "
                   f"int: {repr(data[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_imaginary_whole_complex_to_integer_forced_no_na(self):
        data = [complex(i, 1) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        expected = pd.Series([int(c.real) for c in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_imaginary_whole_complex_to_integer_forced_with_na(self):
        data = [complex(i, 1) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data + [None])
        expected = pd.Series([int(c.real) for c in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_imaginary_decimal_complex_to_integer_error(self):
        data = [complex(i + random.random(), 1) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert complex to "
                   f"int: {repr(data[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_imaginary_decimal_complex_to_integer_forced_no_na(self):
        data = [complex(i + random.random(), 1) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        expected = pd.Series([int(c.real) for c in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_imaginary_decimal_complex_to_integer_forced_with_na(self):
        data = [complex(i + random.random(), 1) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data + [None])
        expected = pd.Series([int(c.real) for c in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    #################################
    ####    String to Integer    ####
    #################################

    ## Integer strings

    def test_apply_integer_string_to_integer_no_na(self):
        data = ["-2", "-1", "0", "1", "2"]
        series = pd.Series(data)
        expected = pd.Series([int(s) for s in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_integer_string_to_integer_with_na(self):
        data = ["-2", "-1", "0", "1", "2"]
        series = pd.Series(data + [None])
        expected = pd.Series([int(s) for s in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    ## Float strings

    def test_apply_whole_float_string_to_integer_no_na(self):
        data = ["-2.0", "-1.0", "0.0", "1.0", "2.0"]
        series = pd.Series(data)
        expected = pd.Series([int(float(s)) for s in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_float_string_to_integer_with_na(self):
        data = ["-2.0", "-1.0", "0.0", "1.0", "2.0"]
        series = pd.Series(data + [None])
        expected = pd.Series([int(float(s)) for s in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_decimal_float_string_to_integer_error(self):
        data = [str(i + random.random()) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(data[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_decimal_float_string_to_integer_forced_no_na(self):
        data = [str(i + random.random()) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        expected = pd.Series([int(float(f)) for f in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_decimal_float_string_to_integer_forced_with_na(self):
        data = [str(i + random.random()) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data + [None])
        expected = pd.Series([int(float(f)) for f in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    ## Complex strings

    def test_apply_real_whole_complex_string_to_integer_no_na(self):
        data = ["(-2+0j)", "(-1+0j)", "0j", "(1+0j)", "(2+0j)"]
        series = pd.Series(data)
        expected = pd.Series([int(complex(s).real) for s in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_real_whole_complex_string_to_integer_with_na(self):
        data = ["(-2+0j)", "(-1+0j)", "0j", "(1+0j)", "(2+0j)"]
        series = pd.Series(data + [None])
        expected = pd.Series([int(complex(s).real) for s in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_real_decimal_complex_string_to_integer_error(self):
        data = [str(complex(i + random.random(), 0)) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(data[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_real_decimal_complex_String_to_integer_forced_no_na(self):
        data = [str(complex(i + random.random(), 0)) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        expected = pd.Series([int(complex(s).real) for s in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_real_decimal_complex_String_to_integer_forced_with_na(self):
        data = [str(complex(i + random.random(), 0)) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data + [None])
        expected = pd.Series([int(complex(s).real) for s in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_imaginary_complex_string_to_integer_error(self):
        # whole case (integer real component)
        data = ["(-2+1j)", "(-1+1j)", "1j", "(1+1j)", "(2+1j)"]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(data[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

        # decimal case (non-integer real component)
        data = [str(complex(i + random.random(), 1)) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(data[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_imaginary_complex_string_to_integer_forced_no_na(self):
        data = ["(-2+1j)", "(-1+1j)", "1j", "(1+1j)", "(2+1j)"]
        series = pd.Series(data)
        expected = pd.Series([int(complex(s).real) for s in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_imaginary_complex_string_to_integer_forced_with_na(self):
        data = ["(-2+1j)", "(-1+1j)", "1j", "(1+1j)", "(2+1j)"]
        series = pd.Series(data + [None])
        expected = pd.Series([int(complex(s).real) for s in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    ## Character strings

    def test_apply_character_string_to_integer_error(self):
        data = ["a", "b", "c", "d", "e"]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(data[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_character_string_to_integer_forced_error(self):
        data = ["a", "b", "c", "d", "e"]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(data[0])}")
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
        data = [datetime.fromtimestamp(i, timezone.utc)
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data])
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_datetime_string_to_integer_with_na(self):
        data = [datetime.fromtimestamp(i, timezone.utc)
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data] + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_naive_datetime_string_to_integer_no_na(self):
        data = [datetime.utcfromtimestamp(i) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data])
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_naive_datetime_string_to_integer_with_na(self):
        data = [datetime.fromtimestamp(i) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data] + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_naive_datetime_string_to_integer_no_na(self):
        data = [datetime.fromtimestamp(i, timezone.utc) if i % 2
                else datetime.fromtimestamp(i) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data])
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_naive_datetime_string_to_integer_with_na(self):
        data = [datetime.fromtimestamp(i, timezone.utc) if i % 2
                else datetime.fromtimestamp(i) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data] + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_mixed_tz_datetime_string_to_integer_no_na(self):
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i, pytz.timezone(get_tz(i)))
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data])
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_mixed_tz_datetime_string_to_integer_with_na(self):
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i, pytz.timezone(get_tz(i)))
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data] + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_ISO_8601_string_to_integer_no_na(self):
        data = [datetime.fromtimestamp(i, timezone.utc)
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data])
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_ISO_8601_string_to_integer_with_na(self):
        data = [datetime.fromtimestamp(i, timezone.utc)
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data] + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_naive_ISO_8601_string_to_integer_no_na(self):
        data = [datetime.utcfromtimestamp(i) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data])
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_naive_ISO_8601_string_to_integer_with_na(self):
        data = [datetime.utcfromtimestamp(i) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data] + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_naive_ISO_8601_string_to_integer_no_na(self):
        data = [datetime.fromtimestamp(i, timezone.utc) if i % 2
                else datetime.fromtimestamp(i) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data])
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_naive_ISO_8601_string_to_integer_with_na(self):
        data = [datetime.fromtimestamp(i, timezone.utc) if i % 2
                else datetime.fromtimestamp(i) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data] + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_mixed_tz_ISO_8601_string_to_integer_no_na(self):
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i, pytz.timezone(get_tz(i)))
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data])
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_mixed_tz_ISO_8601_string_to_integer_with_na(self):
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i, pytz.timezone(get_tz(i)))
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data] + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_datetime_string_to_integer_error(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data])
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(str(data[0]))}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_aware_datetime_string_to_integer_forced_no_na(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data])
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_datetime_string_to_integer_forced_with_na(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data] + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_naive_datetime_string_to_integer_error(self):
        data = [datetime.utcfromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data])
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(str(data[0]))}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_naive_datetime_string_to_integer_forced_no_na(self):
        data = [datetime.fromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data])
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_naive_datetime_string_to_integer_forced_with_na(self):
        data = [datetime.fromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data] + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_naive_datetime_string_to_integer_error(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                if i % 2 else datetime.fromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data])
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(str(data[0]))}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_aware_naive_datetime_string_to_integer_forced_no_na(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                if i % 2 else datetime.fromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data])
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_naive_datetime_string_to_integer_forced_with_na(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                if i % 2 else datetime.fromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data] + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_mixed_tz_datetime_string_to_integer_error(self):
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i + random.random(),
                                       pytz.timezone(get_tz(i)))
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data])
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(str(data[0]))}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_mixed_tz_datetime_string_to_integer_forced_no_na(self):
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i + random.random(),
                                       pytz.timezone(get_tz(i)))
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data])
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_mixed_tz_datetime_string_to_integer_forced_with_na(self):
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i + random.random(),
                                       pytz.timezone(get_tz(i)))
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(d) for d in data] + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_ISO_8601_string_to_integer_error(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data])
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(data[0].isoformat())}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_aware_ISO_8601_string_to_integer_forced_no_na(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data])
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_ISO_8601_string_to_integer_forced_with_na(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data] + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_naive_ISO_8601_string_to_integer_error(self):
        data = [datetime.utcfromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data])
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(data[0].isoformat())}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_naive_ISO_8601_string_to_integer_forced_no_na(self):
        data = [datetime.utcfromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data])
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_naive_ISO_8601_string_to_integer_forced_with_na(self):
        data = [datetime.utcfromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data] + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_naive_ISO_8601_string_to_integer_error(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                if i % 2 else datetime.fromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data])
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(str(data[0]))}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_aware_naive_ISO_8601_string_to_integer_forced_no_na(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                if i % 2 else datetime.fromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data])
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_naive_ISO_8601_string_to_integer_forced_with_na(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                if i % 2 else datetime.fromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data] + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_mixed_tz_ISO_8601_string_to_integer_error(self):
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i + random.random(),
                                       pytz.timezone(get_tz(i)))
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data])
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(str(data[0]))}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_mixed_tz_ISO_8601_string_to_integer_forced_no_na(self):
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i + random.random(),
                                       pytz.timezone(get_tz(i)))
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data])
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_mixed_tz_ISO_8601_string_to_integer_forced_with_na(self):
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i + random.random(),
                                       pytz.timezone(get_tz(i)))
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([d.isoformat() for d in data] + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    ## Timedelta strings

    def test_apply_whole_seconds_timedelta_string_to_integer_no_na(self):
        data = [timedelta(seconds=i) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(t) for t in data])
        expected = pd.Series([int(t.total_seconds()) for t in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_seconds_timedelta_string_to_integer_with_na(self):
        data = [timedelta(seconds=i) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(t) for t in data] + [None])
        expected = pd.Series([int(t.total_seconds()) for t in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_random_seconds_timedelta_string_to_integer_error(self):
        data = [timedelta(seconds=i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(t) for t in data])
        err_msg = (f"[pdtypes.apply.to_integer] could not convert str to "
                   f"int: {repr(str(data[0]))}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_seconds_timedelta_string_to_integer_forced_no_na(self):
        data = [timedelta(seconds=i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(t) for t in data])
        expected = pd.Series([int(t.total_seconds()) for t in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_seconds_timedelta_string_to_integer_forced_with_na(self):
        data = [timedelta(seconds=i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series([str(t) for t in data] + [None])
        expected = pd.Series([int(t.total_seconds()) for t in data] + [None])
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
        data = [datetime.fromtimestamp(i, timezone.utc)
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_datetime_to_integer_with_na(self):
        data = [datetime.fromtimestamp(i, timezone.utc)
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_naive_datetime_to_integer_no_na(self):
        data = [datetime.utcfromtimestamp(i) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_naive_datetime_to_integer_with_na(self):
        data = [datetime.fromtimestamp(i) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_naive_datetime_integer_no_na(self):
        data = [datetime.fromtimestamp(i, timezone.utc) if i % 2
                else datetime.fromtimestamp(i) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_aware_naive_datetime_to_integer_with_na(self):
        data = [datetime.fromtimestamp(i, timezone.utc) if i % 2
                else datetime.fromtimestamp(i) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_mixed_tz_datetime_to_integer_no_na(self):
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i, pytz.timezone(get_tz(i)))
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_timestamp_mixed_tz_datetime_to_integer_with_na(self):
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i, pytz.timezone(get_tz(i)))
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_datetime_to_integer_error(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert datetime to "
                   f"int: {repr(data[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_aware_datetime_to_integer_forced_no_na(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_datetime_to_integer_forced_with_na(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_naive_datetime_to_integer_error(self):
        data = [datetime.utcfromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert datetime to "
                   f"int: {repr(data[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_naive_datetime_to_integer_forced_no_na(self):
        data = [datetime.utcfromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_naive_datetime_to_integer_forced_with_na(self):
        data = [datetime.utcfromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_naive_datetime_to_integer_error(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                if i % 2 else datetime.fromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert datetime to "
                   f"int: {repr(data[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_aware_naive_datetime_to_integer_forced_no_na(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                if i % 2 else datetime.fromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_aware_naive_datetime_to_integer_forced_with_na(self):
        data = [datetime.fromtimestamp(i + random.random(), timezone.utc)
                if i % 2 else datetime.fromtimestamp(i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_mixed_tz_datetime_to_integer_error(self):
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i + random.random(),
                                       pytz.timezone(get_tz(i)))
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert datetime to "
                   f"int: {repr(data[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_timestamp_mixed_tz_datetime_to_integer_forced_no_na(self):
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i + random.random(),
                                       pytz.timezone(get_tz(i)))
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        expected = pd.Series([int(d.timestamp()) for d in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_timestamp_mixed_tz_datetime_to_integer_forced_with_na(self):
        get_tz = lambda x: pytz.all_timezones[x % len(pytz.all_timezones)]
        data = [datetime.fromtimestamp(i + random.random(),
                                       pytz.timezone(get_tz(i)))
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data + [None])
        expected = pd.Series([int(d.timestamp()) for d in data] + [None])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    ####################################
    ####    Timedelta to Integer    ####
    ####################################

    def test_apply_whole_seconds_timedelta_to_integer_no_na(self):
        data = [timedelta(seconds=i) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        expected = pd.Series([int(t.total_seconds()) for t in data])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_whole_seconds_timedelta_to_integer_with_na(self):
        data = [timedelta(seconds=i) for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data + [None])
        expected = pd.Series([int(t.total_seconds()) for t in data] + [None])
        result = series.apply(pdtypes.apply.to_integer)
        assert_series_equal(result, expected)

    def test_apply_random_seconds_timedelta_to_integer_error(self):
        data = [timedelta(seconds=i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        err_msg = (f"[pdtypes.apply.to_integer] could not convert timedelta "
                   f"to int: {repr(data[0])}")
        with self.assertRaises(ValueError) as err:
            series.apply(pdtypes.apply.to_integer)
        self.assertEqual(str(err.exception), err_msg)

    def test_apply_random_seconds_timedelta_to_integer_forced_no_na(self):
        data = [timedelta(seconds=i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data)
        expected = pd.Series([int(t.total_seconds()) for t in data])
        result = series.apply(pdtypes.apply.to_integer, force=True)
        assert_series_equal(result, expected)

    def test_apply_random_seconds_timedelta_to_integer_forced_with_na(self):
        data = [timedelta(seconds=i + random.random())
                for i in [-2, -1, 0, 1, 2]]
        series = pd.Series(data + [None])
        expected = pd.Series([int(t.total_seconds()) for t in data] + [None])
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
                   f"to int: {repr(data[0])}")
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
        