.. currentmodule:: pdcast

.. testsetup::

    import pdcast

.. _CompositeType:

pdcast.CompositeType
====================

.. autoclass:: CompositeType

.. raw:: html
    :file: ../../images/types/Types_UML.html

.. _CompositeType.constructors:

Constructors
------------
:class:`CompositeTypes <CompositeType>` use specialized construction semantics
in :func:`detect_type` and :func:`resolve_type`.  They can be created in one of
3 ways:

    #.  By providing an iterable of type specifiers to :func:`resolve_type`.
    #.  By providing a comma-separated string in the
        :ref:`type specificiation mini-language <resolve_type.mini_language>`.
    #.  By providing an iterable of mixed type to :func:`detect_type`.

.. doctest::

    # (1)
    >>> pdcast.resolve_type(["int[python]", float, pdcast.PythonComplexType])   # doctest: +SKIP
    CompositeType({int[python], float[python], complex[python]})

    # (2)
    >>> pdcast.resolve_type("int[python], float[python], complex[python]")   # doctest: +SKIP
    CompositeType({int[python], float[python], complex[python]})

    # (3)
    >>> pdcast.detect_type([1, 2.3, 4+5j])   # doctest: +SKIP
    CompositeType({int[python], float[python], complex[python]})

In the latter case, the result also includes a special
:attr:`.index <CompositeType.index>` attribute, which keeps track of the
inferred type at each index.

.. doctest::

    >>> _.index
    array([PythonIntegerType(), PythonFloatType(), PythonComplexType()],
           dtype=object)

They also support the addition of dynamic :attr:`aliases <Type.aliases>` at
runtime, in which case the :meth:`from_string <Type.from_string>` method may be
used.

.. autosummary::
    :toctree: ../../generated

    CompositeType.index
    CompositeType.from_string

.. _CompositeType.membership:

Membership
----------
These are called by :func:`typecheck` and :func:`@dispatch <dispatch>` to
perform membership tests.

.. autosummary::
    :toctree: ../../generated

    CompositeType.contains
    CompositeType.__contains__

.. _CompositeType.expand:

Hierarchies
-----------
:class:`CompositeTypes <CompositeType>` implicitly include all the 
:ref:`hierarchical <AbstractType.hierarchy>` children of each of their
elements.  These can be expanded or collapsed using the following methods.

.. autosummary::
    :toctree: ../../generated

    CompositeType.expand
    CompositeType.collapse

.. _CompositeType.set:

Set Interface
-------------
All other attributes and methods are used to implement the standard
:class:`set <python:set>` interface.  These work just like their built-in
counterparts, but are implicitly extended to include the
:ref:`hierarchical <AbstractType.hierarchy>` children of each element.

.. autosummary::
    :toctree: ../../generated

    CompositeType.add
    CompositeType.remove
    CompositeType.clear
    CompositeType.pop
    CompositeType.discard
    CompositeType.__len__
    CompositeType.__iter__
    CompositeType.__str__

.. _CompositeType.comparisons:

Comparisons
-----------
:class:`CompositeTypes <CompositeType>` can be compared just like ordinary
:class:`sets <python:set>`.

.. autosummary::
    :toctree: ../../generated

    CompositeType.issubset
    CompositeType.issuperset
    CompositeType.isdisjoint
    CompositeType.__eq__
    CompositeType.__ge__
    CompositeType.__gt__
    CompositeType.__le__
    CompositeType.__lt__

.. _CompositeType.operations:

Operations
----------
:class:`CompositeTypes <CompositeType>` support all the standard set
operations.

.. autosummary::
    :toctree: ../../generated

    CompositeType.union
    CompositeType.intersection
    CompositeType.difference
    CompositeType.symmetric_difference
    CompositeType.__or__
    CompositeType.__and__
    CompositeType.__sub__
    CompositeType.__xor__

.. _CompositeType.updates:

Updates
-------
They can also be updated in place, just like built-in
:class:`sets <python:set>`.

.. autosummary::
    :toctree: ../../generated

    CompositeType.update
    CompositeType.intersection_update
    CompositeType.difference_update
    CompositeType.symmetric_difference_update
    CompositeType.__ior__
    CompositeType.__iand__
    CompositeType.__isub__
    CompositeType.__ixor__
