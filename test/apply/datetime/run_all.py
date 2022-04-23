from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from test.apply.datetime.apply_datetime_to_integer_test import *
from test.apply.datetime.apply_datetime_to_float_test import *
from test.apply.datetime.apply_datetime_to_complex_test import *
from test.apply.datetime.apply_datetime_to_string_test import *
from test.apply.datetime.apply_datetime_to_boolean_test import *
from test.apply.datetime.apply_datetime_to_timedelta_test import *


if __name__ == "__main__":
    unittest.main()
