from .base import *
from .boolean import *
from .integer import *
from .float import *
from .complex import *
from .decimal import *
from .datetime import *
from .timedelta import *
from .string import *


# TODO: move away from core.pyx factory function and put cache registries into
# modules instead.
# base -> shared_registry
# datetime -> datetime64_registry
# timedelta -> timedelta64_registry

# element_type functionality absorbed by get_dtype, resolve_dtype.
