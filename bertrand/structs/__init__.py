"""A set of high-performance data structures based on linked lists.

Each data structure is implemented in pure C++ using the CPython API, and can
be used just like their python `list`, `set`, and `dict` counterparts.  They
have all the same methods and semantics, plus a few extras related to their
ordered nature and enhanced type/thread safety.  The same data structures are
also available in C++ with identical interfaces, so you can use them in other
extension modules and C++ code.  They are located under the `<bertrand.h>`
header and associated bertrand:: namespace, both of which are installed along
with the python module using CMake integrations.

See the bertrand API documentation for more details.
"""
from .linked import LinkedList, LinkedSet, LinkedDict
