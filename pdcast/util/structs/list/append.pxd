"""Cython headers for pdcast/util/structs/list/append.h"""
from cpython cimport PyObject

from .node cimport (
    SingleNode, DoubleNode, DirectAllocator, FreeListAllocator, PreAllocator
)
from .view cimport ListView, SetView, DictView

cdef extern from "append.h":
    # list.append()
    void append(
        ListView[SingleNode, DirectAllocator]* view,
        PyObject* item,
        bint left
    ) except *
    void append(
        ListView[SingleNode, FreeListAllocator]* view,
        PyObject* item,
        bint left
    ) except *
    void append(
        ListView[SingleNode, PreAllocator]* view,
        PyObject* item,
        bint left
    ) except *
    void append(
        ListView[DoubleNode, DirectAllocator]* view,
        PyObject* item,
        bint left
    ) except *
    void append(
        ListView[DoubleNode, FreeListAllocator]* view,
        PyObject* item,
        bint left
    ) except *
    void append(
        ListView[DoubleNode, PreAllocator]* view,
        PyObject* item,
        bint left
    ) except *

    # set.add/append()
    void append(
        SetView[SingleNode, DirectAllocator]* view,
        PyObject* item,
        bint left
    ) except *
    void append(
        SetView[SingleNode, FreeListAllocator]* view,
        PyObject* item,
        bint left
    ) except *
    void append(
        SetView[SingleNode, PreAllocator]* view,
        PyObject* item,
        bint left
    ) except *
    void append(
        SetView[DoubleNode, DirectAllocator]* view,
        PyObject* item,
        bint left
    ) except *
    void append(
        SetView[DoubleNode, FreeListAllocator]* view,
        PyObject* item,
        bint left
    ) except *
    void append(
        SetView[DoubleNode, PreAllocator]* view,
        PyObject* item,
        bint left
    ) except *

    # dict.append()
    void append(
        DictView[SingleNode, DirectAllocator]* view,
        PyObject* item,
        bint left
    ) except *
    void append(
        DictView[SingleNode, DirectAllocator]* view,
        PyObject* item,
        PyObject* mapped,
        bint left
    ) except *
    void append(
        DictView[SingleNode, FreeListAllocator]* view,
        PyObject* item,
        bint left
    ) except *
    void append(
        DictView[SingleNode, FreeListAllocator]* view,
        PyObject* item,
        PyObject* mapped,
        bint left
    ) except *
    void append(
        DictView[SingleNode, PreAllocator]* view,
        PyObject* item,
        bint left
    ) except *
    void append(
        DictView[SingleNode, PreAllocator]* view,
        PyObject* item,
        PyObject* mapped,
        bint left
    ) except *
    void append(
        DictView[DoubleNode, DirectAllocator]* view,
        PyObject* item,
        bint left
    ) except *
    void append(
        DictView[DoubleNode, DirectAllocator]* view,
        PyObject* item,
        PyObject* mapped,
        bint left
    ) except *
    void append(
        DictView[DoubleNode, FreeListAllocator]* view,
        PyObject* item,
        bint left
    ) except *
    void append(
        DictView[DoubleNode, FreeListAllocator]* view,
        PyObject* item,
        PyObject* mapped,
        bint left
    ) except *
    void append(
        DictView[DoubleNode, PreAllocator]* view,
        PyObject* item,
        bint left
    ) except *
    void append(
        DictView[DoubleNode, PreAllocator]* view,
        PyObject* item,
        PyObject* mapped,
        bint left
    ) except *
