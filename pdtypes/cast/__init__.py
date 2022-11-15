# from pdtypes.types import (
#     resolve_dtype, BooleanType, IntegerType, FloatType, ComplexType,
#     DecimalType, DatetimeType, TimedeltaType, StringType
# )

# from .core import ConversionSeries


# #######################
# ####    GENERIC    ####
# #######################


# def cast(val, dtype, **kwargs):
#     resolved = resolve_dtype(dtype)

#     # boolean
#     if isinstance(resolved, BooleanType):
#         return to_boolean(val, dtype=resolved, **kwargs)

#     # integer
#     if isinstance(resolved, IntegerType):
#         return to_integer(val, dtype=resolved, **kwargs)

#     # float
#     if isinstance(resolved, FloatType):
#         return to_float(val, dtype=resolved, **kwargs)

#     # complex
#     if isinstance(resolved, ComplexType):
#         return to_complex(val, dtype=resolved, **kwargs)

#     # decimal
#     if isinstance(resolved, DecimalType):
#         return to_decimal(val, dtype=resolved, **kwargs)

#     # datetime
#     if isinstance(resolved, DatetimeType):
#         return to_datetime(val, dtype=resolved, **kwargs)

#     # timedelta
#     if isinstance(resolved, TimedeltaType):
#         return to_timedelta(val, dtype=resolved, **kwargs)

#     # string
#     if isinstance(resolved, StringType):
#         return to_string(val, dtype=resolved, **kwargs)

#     # object (explicit callable)
#     if isinstance(dtype, type) or callable(dtype):
#         # TODO: make sure if callable, that signature accepts one argument,
#         # the value at each index
#         return to_object(val, call=dtype)

#     # object (generic)
#     return ConversionSeries(val).to_object()


# ########################
# ####    EXPLICIT    ####
# ########################


# def to_boolean(val, dtype=bool, **kwargs):
#     dtype = resolve_dtype(dtype)
#     return ConversionSeries(val).to_boolean(dtype=dtype, **kwargs)


# def to_integer(val, dtype=int, **kwargs):
#     dtype = resolve_dtype(dtype)
#     return ConversionSeries(val).to_integer(dtype=dtype, **kwargs)


# def to_float(val, dtype=float, **kwargs):
#     dtype = resolve_dtype(dtype)
#     return ConversionSeries(val).to_float(dtype=dtype, **kwargs)


# def to_complex(val, dtype=complex, **kwargs):
#     dtype = resolve_dtype(dtype)
#     return ConversionSeries(val).to_complex(dtype=dtype, **kwargs)


# def to_decimal(val, dtype=decimal.Decimal, **kwargs):
#     dtype = resolve_dtype(dtype)
#     return ConversionSeries(val).to_decimal(dtype=dtype, **kwargs)


# def to_datetime(val, dtype="datetime", **kwargs):
#     dtype = resolve_dtype(dtype)
#     return ConversionSeries(val).to_datetime(dtype=dtype, **kwargs)


# def to_timedelta(val, dtype="timedelta", **kwargs):
#     dtype = resolve_dtype(dtype)
#     return ConversionSeries(val).to_timedelta(dtype=dtype, **kwargs)


# def to_string(val, dtype=str, **kwargs):
#     dtype = resolve_dtype(dtype)
#     return ConversionSeries(val).to_string(dtype=dtype, **kwargs)


# def to_object(val, dtype=object, **kwargs):
#     # TODO: in ConversionSeries.to_object()
#     # if isinstance(call, (str, type)):
#     #     dtype = resolve_dtype(dtype)
#     #     return series.astype("O")
#     # elif callable(dtype) and inspect.signature(dtype) has 1 argument:
#     #     hook into fast cython loop and call the function at each index.
#     # else:
#     #     raise ValueError(f"Could not interpret dtype: {repr(dtype)}")
#     return ConversionSeries(val).to_object(val, call=call, **kwargs)
