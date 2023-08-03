"""Cython headers for pdcast/util/lists/structs/index.h (`SingleIndex`
namespace)
"""
from cpython.ref cimport PyObject

from .node cimport ListView, SetView, DictView

cdef extern from "index.h" namespace "SingleIndex":
    # node_at_index(view, index)
    NodeType* node_at_index[NodeType](ListView[NodeType]* view, size_t index)
    NodeType* node_at_index[NodeType](SetView[NodeType]* view, size_t index)
    NodeType* node_at_index[NodeType](DictView[NodeType]* view, size_t index)

    # index(view, item, start, stop)
    size_t index[NodeType](
        ListView[NodeType]* view,
        PyObject* item,
        long long start,
        long long stop
    ) except? MAX_SIZE_T
    size_t index[NodeType](
        SetView[NodeType]* view,
        PyObject* item,
        long long start,
        long long stop
    ) except? MAX_SIZE_T
    size_t index[NodeType](
        DictView[NodeType]* view,
        PyObject* item,
        long long start,
        long long stop
    ) except? MAX_SIZE_T

    # count(view, item, start, stop)
    size_t count[NodeType](
        ListView[NodeType]* view,
        PyObject* item,
        long long start,
        long long stop
    ) except? MAX_SIZE_T
    size_t count[NodeType](
        SetView[NodeType]* view,
        PyObject* item,
        long long start,
        long long stop
    ) except? MAX_SIZE_T
    size_t count[NodeType](
        DictView[NodeType]* view,
        PyObject* item,
        long long start,
        long long stop
    ) except? MAX_SIZE_T

    # insert(view, index, item)
    void insert[NodeType](
        ListView[NodeType]* view,
        long long index,
        PyObject* item
    ) except *
    void insert[NodeType](
        SetView[NodeType]* view,
        long long index,
        PyObject* item
    ) except *
    void insert[NodeType](
        DictView[NodeType]* view,
        long long index,
        PyObject* item
    ) except *

    # get_slice(view, start, stop, step)
    ListView[NodeType]* get_slice[NodeType](
        ListView[NodeType]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except NULL
    SetView[NodeType]* get_slice[NodeType](
        SetView[NodeType]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except NULL
    DictView[NodeType]* get_slice[NodeType](
        DictView[NodeType]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except NULL

    # set_slice(view, start, stop, step, iterator)
    void set_slice[NodeType](
        ListView[NodeType]* view,
        size_t start,
        size_t stop,
        ssize_t step,
        PyObject* iterator
    ) except *
    void set_slice[NodeType](
        SetView[NodeType]* view,
        size_t start,
        size_t stop,
        ssize_t step,
        PyObject* iterator
    ) except *
    void set_slice[NodeType](
        DictView[NodeType]* view,
        size_t start,
        size_t stop,
        ssize_t step,
        PyObject* iterator
    ) except *

    # delete_slice(view, start, stop, step)
    void delete_slice[NodeType](
        ListView[NodeType]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except *
    void delete_slice[NodeType](
        SetView[NodeType]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except *
    void delete_slice[NodeType](
        DictView[NodeType]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except *
