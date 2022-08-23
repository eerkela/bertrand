import pandas as pd

# if pyarrow >= 1.0.0 is installed, use as default string storage backend
try:
    DEFAULT_STRING_DTYPE = pd.StringDtype("pyarrow")
    PYARROW_INSTALLED = True
except ImportError:
    DEFAULT_STRING_DTYPE = pd.StringDtype("python")
    PYARROW_INSTALLED = False


from .check import check_dtype, get_dtype, is_dtype
# from .cast import coerce_dtype
# from .attach import *
