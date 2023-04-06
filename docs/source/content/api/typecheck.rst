.. currentmodule:: pdcast

.. testsetup::

    import numpy as np
    import pandas as pd
    import pdcast

pdcast.typecheck
================

.. autofunction:: typecheck



Pandas integration
------------------
If :func:`pdcast.attach <attach>` is invoked, this function is attached
directly to ``pandas.Series`` objects, allowing users to omit the ``data``
argument.  It is accessible under :meth:`pandas.Series.typecheck`.

.. doctest::

    >>> pdcast.attach()
    >>> pd.Series([1, 2, 3]).typecheck("int")
    True

A similar attribute is attached to ``pandas.DataFrame`` objects under
:meth:`pandas.DataFrame.typecheck`.  This version, however, can accept
dictionaries mapping column names to comparison types in its ``dtype`` field.

.. doctest::

    >>> df = pd.DataFrame({"a": [1, 2], "b": [1., 2.], "c": ["1", "2"]})
    >>> df.typecheck({"a": "int", "b": "float", "c": "string")
    True
