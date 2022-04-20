from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from test.parse.parse_datetime_test import *
from test.parse.parse_dtype_test import *
from test.parse.parse_string_test import *


if __name__ == "__main__":
    unittest.main()
