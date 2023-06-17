.. currentmodule:: pdcast

pdcast/decorators/attachable.py
===============================
The :func:`@attachable <attachable>` decorator is split across two files to
allow for code reuse.  The first describes a base class that grants transparent
attribute access to the wrapped function, and the second describes the
:func:`@attachable <attachable>` decorator itself.

pdcast/decorators/base.py
-------------------------

.. literalinclude:: ../../../../../pdcast/decorators/base.py

pdcast/decorators/attachable.py
-------------------------------

.. literalinclude:: ../../../../../pdcast/decorators/attachable.py
