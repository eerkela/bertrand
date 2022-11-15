cimport numpy as np
import numpy as np
import pandas as pd

from pdtypes.types cimport (
    BooleanType, CompositeType, ElementType, IntegerType, NumpyDatetime64Type,
    NumpyTimedelta64Type, ObjectType
)

from .type cimport type_lookup, non_nullable_types


######################
####    PUBLIC    ####
######################


cdef ElementType parse_example_scalar(
    object example,
    bint sparse = False,
    bint categorical = False,
    bint force_nullable = False
):
    """TODO"""
    cdef str unit
    cdef unsigned long long step_size
    cdef type base = type_lookup.get(type(example), ObjectType)

    # numpy datetime64/timedelta64 special cases
    if issubclass(base, (NumpyDatetime64Type, NumpyTimedelta64Type)):
        unit, step_size = np.datetime_data(example)
        return base.instance(
            sparse=sparse,
            categorical=categorical,
            unit=unit,
            step_size=step_size
        )

    # comply with force_nullable
    if force_nullable and issubclass(base, (BooleanType, IntegerType)):
        return base.instance(
            sparse=sparse,
            categorical=categorical,
            nullable=True
        )

    # general case
    return base.instance(
        sparse=sparse,
        categorical=categorical
    )



# TODO: make the CompositeType returned by this function contain a lot more
# information.  See personal notes on how to do this.


cdef CompositeType parse_example_vector(
    np.ndarray[object] arr,
    bint sparse = False,
    bint categorical = False,
    bint force_nullable = False
):
    """TODO"""
    cdef unsigned int arr_length = arr.shape[0]
    cdef unsigned int i
    cdef object element
    cdef str unit
    cdef unsigned long long step_size
    cdef set type_set = set()

    # add type of each element in array to shared set
    for i in range(arr_length):
        element = arr[i]
        if isinstance(element, (np.datetime64, np.timedelta64)):
            unit, step_size = np.datetime_data(element)
            type_set.add((type(element), unit, step_size))
        else:
            type_set.add(type(element))

    cdef CompositeType result = CompositeType()

    # convert type set into ElementType objects, obeying flags
    for element in type_set:
        if isinstance(element, tuple):
            element, unit, step_size = element
            result.add(
                type_lookup[element].instance(
                    sparse=sparse,
                    categorical=categorical,
                    unit=unit,
                    step_size=step_size
                )
            )
        elif force_nullable and issubclass(element, non_nullable_types):
            result.add(
                type_lookup[element].instance(
                    sparse=sparse,
                    categorical=categorical,
                    nullable=True
                )
            )
        else:
            result.add(
                type_lookup.get(element, ObjectType).instance(
                    sparse=sparse,
                    categorical=categorical
                )
            )

    return result


