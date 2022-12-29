import pstats, cProfile

import numpy as np

from pdtypes.types.base import *


test = np.arange(-10**6, 10**6, dtype="f8").astype("O")

cProfile.runctx("get_dtype(test)", globals(), locals(), "Profile.prof")

s = pstats.Stats("Profile.prof")
s.strip_dirs().sort_stats("time").print_stats()
