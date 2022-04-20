from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from apply.apply_integer_to_x_test import  *
from apply.apply_float_to_x_test import *
from apply.apply_complex_to_x_test import *
from apply.apply_string_to_x_test import *
from apply.apply_boolean_to_x_test import *
from apply.apply_datetime_to_x_test import *


if __name__ == "__main__":
    unittest.main()