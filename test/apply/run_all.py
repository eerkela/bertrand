from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from test.apply.integer.run_all import *
from test.apply.float.run_all import *
from test.apply.complex.run_all import *
from test.apply.string.run_all import *
from test.apply.boolean.run_all import *
from test.apply.datetime.run_all import *


if __name__ == "__main__":
    unittest.main()
