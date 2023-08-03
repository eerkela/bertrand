"""Cython headers for pdcast/util/structs/list/append.h"""
from cpython cimport PyObject

from .node cimport ListView, SetView, DictView

# NOTE: Cython does not seem to support nested templates in function definitions,
# so we have to list all the possible ViewTypes ourselves.

cdef extern from "append.h":
    void append[NodeType](ListView[NodeType]* view, PyObject* item) except +*
    void append[NodeType](SetView[NodeType]* view, PyObject* item) except +*
    void append[NodeType](DictView[NodeType]* view, PyObject* item) except +*
    void append[NodeType](DictView[NodeType]* view, PyObject* item, PyObject* mapped) except +*
    void appendleft[NodeType](ListView[NodeType]* view, PyObject* item) except +*
    void appendleft[NodeType](SetView[NodeType]* view, PyObject* item) except +*
    void appendleft[NodeType](DictView[NodeType]* view, PyObject* item) except +*
    void appendleft[NodeType](DictView[NodeType]* view, PyObject* item, PyObject* mapped) except +*
    void extend[NodeType](ListView[NodeType]* view, PyObject* items) except +*
    void extend[NodeType](SetView[NodeType]* view, PyObject* items) except +*
    void extend[NodeType](DictView[NodeType]* view, PyObject* items) except +*
    void extendleft[NodeType](ListView[NodeType]* view, PyObject* items) except +*
    void extendleft[NodeType](SetView[NodeType]* view, PyObject* items) except +*
    void extendleft[NodeType](DictView[NodeType]* view, PyObject* items) except +*
