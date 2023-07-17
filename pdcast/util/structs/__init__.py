"""This package provides basic data structures with minimal dependencies, which
can be easily exported to other projects.

Modules
-------
list
    Optimized, pure-Cython implementations of doubly-linked lists that are
    drop-in compatible with base Python lists.
lru
    A Least-Recently Used (LRU) dictionary with a fixed size.
priority
    A read-only linked list in which items can be moved around, but not added
    or removed. 
"""
from .list import HashedList, LinkedList, ListNode
from .lru import LRUDict
from .priority import PriorityList
