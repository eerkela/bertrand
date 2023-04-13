"""This package provides basic data structures with minimal dependencies, which
can be easily exported to other projects.

Modules
-------
func
    Extendable function objects with dynamic arguments.

lru
    A Least-Recently Used (LRU) dictionary with a fixed size.
"""
from .func import extension_func, no_default, ExtensionFunc, ExtensionMethod
from .lru import LRUDict
