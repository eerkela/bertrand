"""Cython headers for pdcast/util/structs/list/index.h"""
from cpython.ref cimport PyObject

from .node cimport ListView, SetView, DictView

cdef extern from "index.h":
    size_t MAX_SIZE_T
    size_t normalize_index(
        long long index,
        size_t size,
        bint truncate
    ) except? MAX_SIZE_T
