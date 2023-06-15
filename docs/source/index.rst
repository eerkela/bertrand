.. pdcast documentation master file, created by
   sphinx-quickstart on Fri Feb 24 16:22:55 2023.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

.. testsetup::

   import pandas as pd
   from pdcast import *

.. include:: ../../README.rst

.. testcleanup::

   registry.remove(CustomType)

.. toctree::
   :hidden:
   :caption: Contents

   Overview <self>

.. toctree::
   :hidden:

   Motivation <content/motivation>
   Tutorial: bfloat16 <content/tutorial>
   Type Index <content/types/types>
   API <content/api/api>
   Changelog <content/changelog>
