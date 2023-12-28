from sys import getrefcount as rc
from test import *

import gc


print(rc(NumpyInt8))
for _ in range(5):
    Union.from_types(LinkedSet([NumpyInt8]))
gc.collect()
print(rc(NumpyInt8))
