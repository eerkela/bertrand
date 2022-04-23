from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from test.apply.complex.apply_complex_to_integer_test import *
from test.apply.complex.apply_complex_to_float_test import *
from test.apply.complex.apply_complex_to_string_test import *
from test.apply.complex.apply_complex_to_boolean_test import *
from test.apply.complex.apply_complex_to_datetime_test import *
from test.apply.complex.apply_complex_to_timedelta_test import *


if __name__ == "__main__":
    unittest.main()
