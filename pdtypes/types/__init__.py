from .base import *
from .boolean import *
from .integer import *
from .float import *
from .complex import *
from .decimal import *
from .datetime import *
from .timedelta import *
from .string import *


# TODO: resolve_dtype is about 4x slower than np.dtype(), but it works
# identically.
# -> this might have something to do with dict packing/unpacking.  Maybe opt
# for a pure tuple approach?
