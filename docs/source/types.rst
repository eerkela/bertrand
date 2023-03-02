Type Index
==========
Below is a complete list of all the types that come prepackaged with
``pdcast``, in hierarchical order.  Each type can be resolved as shown by
providing an equivalent string to ``pdcast.resolve_type()``.

* ``bool``

    * ``bool[numpy]``
    * ``bool[pandas]``
    * ``bool[python]``

* ``int``

    * ``signed``

        * ``signed[numpy]``

            * ``int8[numpy]``
            * ``int16[numpy]``
            * ``int32[numpy]``
            * ``int64[numpy]``

        * ``signed[pandas]``

            * ``int8[pandas]``
            * ``int16[pandas]``
            * ``int32[pandas]``
            * ``int64[pandas]``

        * ``signed[python]``
        * ``int8``

            * ``int8[numpy]``
            * ``int8[pandas]``

        * ``int16``

            * ``int16[numpy]``
            * ``int16[pandas]``

        * ``int32``

            * ``int32[numpy]``
            * ``int32[pandas]``

        * ``int64``

            * ``int64[numpy]``
            * ``int64[pandas]``

    * ``unsigned``

        * ``unsigned[numpy]``

            * ``uint8[numpy]``
            * ``uint16[numpy]``
            * ``uint32[numpy]``
            * ``uint64[numpy]``

        * ``unsigned[pandas]``

            * ``uint8[pandas]``
            * ``uint16[pandas]``
            * ``uint32[pandas]``
            * ``uint64[pandas]``

        * ``uint8``

            * ``uint8[numpy]``
            * ``uint8[pandas]``

        * ``uint16``

            * ``uint16[numpy]``
            * ``uint16[pandas]``

        * ``uint32``

            * ``uint32[numpy]``
            * ``uint32[pandas]``

        * ``uint64``

            * ``uint64[numpy]``
            * ``uint64[pandas]``

* ``float``

    * ``float[numpy]``

        * ``float16[numpy]``
        * ``float32[numpy]``
        * ``float64[numpy]``
        * ``float80[numpy]``\ :superscript:`1`\ 

    * ``float[python]``

        * ``float64[python]``

    * ``float16``

        * ``float16[numpy]``

    * ``float32``

        * ``float32[numpy]``

    * ``float64``

        * ``float64[numpy]``
        * ``float64[python]``

    * ``float80``\ :superscript:`1`\ 

        * ``float80[numpy]``\ :superscript:`1`\ 

* ``complex``

    * ``complex[numpy]``

        * ``complex64[numpy]``
        * ``complex128[numpy]``
        * ``complex160[numpy]``\ :superscript:`2`\ 

    * ``complex[python]``

        * ``complex128[python]``

    * ``complex64``

        * ``complex64[numpy]``

    * ``complex128``

        * ``complex128[numpy]``
        * ``complex128[python]``

    * ``complex160``\ :superscript:`2`\ 

        * ``complex160[numpy]``\ :superscript:`2`\ 

* ``decimal``

    * ``decimal[python]``

* ``datetime``

    * ``datetime[numpy]``
    * ``datetime[pandas]``
    * ``datetime[python]``

* ``timedelta``

    * ``timedelta[numpy]``
    * ``timedelta[pandas]``
    * ``timedelta[python]``

* ``string``

    * ``string[python]``
    * ``string[pyarrow]``\ :superscript:`3`\ 

* ``object``

Footnotes
---------
1.  This is an alias for `x86 extended precision float (long double) <https://en.wikipedia.org/wiki/Extended_precision#x86_extended_precision_format>`_ 
    and may not be defined on every system.  Numpy defines this as either a
    ``float96`` or ``float128`` object, but neither is technically accurate
    and only one of them is ever exposed at a time, depending on system
    configuration (``float96`` for 32-bit systems, ``float128`` for 64-bit).
    ``float80`` was chosen to reflect the actual number of significant bits in
    the specification, rather than the length it occupies in memory.  The
    type's ``itemsize`` differs from this, and is always accurate for the
    system in question.
2.  Complex equivalent of 1.
3.  Requires PyArrow>=1.0.0.
