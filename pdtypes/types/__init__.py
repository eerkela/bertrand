from .boolean import *
from .integer import *
from .float import *
from .complex import *
from .decimal import *
from .datetime import *
from .timedelta import *
from .string import *


# TODO: ElementType.extension_type -> ElementType.nullable.  This is always
# defined, and points to either a numpy.dtype object or a pandas extension
# type that allows this type to take nullable values.
# bool              - pd.BooleanDtype()
# int               - np.dtype("O")
# signed int        - None
# unsigned int      - None
# int8              - pd.Int8Dtype()
# int16             - pd.Int16Dtype()
# int32             - pd.Int32Dtype()
# int64             - pd.Int64Dtype()
# uint8             - pd.UInt8Dtype()
# uint16            - pd.UInt16Dtype()
# uint32            - pd.UInt32Dtype()
# uint64            - pd.UInt64Dtype()
# float             - np.dtype("O")
# float16           - np.dtype("f2")
# float32           - np.dtype("f4")
# float64           - np.dtype("f8")
# longdouble        - np.dtype(np.longdouble)
# complex           - np.dtype("O")
# complex64         - np.dtype("c8")
# complex128        - np.dtype("c16")
# clongdouble       - np.dtype(np.clongdouble)
# pandas.Timestamp  - np.dtype("M8[ns]") ?
# datetime.datetime - np.dtype("O")  ?
# numpy.datetime64  - np.dtype("M8[{step_size}{unit}]")  ?



# TODO: find a more sustainable way to compute hashes and append them to
# ElementType instances.
