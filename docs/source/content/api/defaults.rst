.. currentmodule:: pdcast

.. testsetup::

    import numpy as np
    import pandas as pd
    import pdcast

pdcast.defaults
===============

.. autoclass:: CastDefaults

.. _defaults.interface:

Interface
---------
:class:`CastDefaults` implements a minimal interface, reserving only 2 methods
for internal use.

.. autosummary::
    :toctree: ../../generated

    CastDefaults.default_values
    CastDefaults.register_arg

.. _defaults.reset:

Resetting defaults
------------------
:class:`CastDefaults` remembers the default values that were supplied to
:meth:`register_arg <CastDefaults.register_arg>`.  Each of its properties is
given a deleter that resets the argument back to this value when the ``del``
keyword is invoked on it.  For instance:

.. doctest::

    >>> pdcast.defaults.unit
    'ns'
    >>> pdcast.defaults.unit = "s"
    >>> pdcast.defaults.unit
    's'
    >>> del pdcast.defaults.unit
    >>> pdcast.defaults.unit
    'ns'
