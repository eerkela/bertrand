"""Cython headers for pdcast/util/structs/list/sort.h"""
from cpython.ref cimport PyObject

from .view cimport ListView, SetView, DictView

cdef extern from "sort.h":
    void sort[T](ListView[T]* view, PyObject* key, bint reverse) except *
    void sort[T](SetView[T]* view, PyObject* key, bint reverse) except *
    void sort[T](DictView[T]* view, PyObject* key, bint reverse) except *
