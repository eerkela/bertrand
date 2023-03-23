.. pdcast documentation master file, created by
   sphinx-quickstart on Fri Feb 24 16:22:55 2023.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

.. include:: ../../README.rst
   :end-line: 187

.. testsetup:: dispatch

   # detach from pandas to give correct errors during dispatch doctest
   import pdcast
   pdcast.detach()

.. include:: ../../README.rst
   :start-line: 188

.. toctree::
   :hidden:
   :caption: Contents
   :maxdepth: 1

   Overview <self>

.. toctree::
   :hidden:
   :maxdepth: 3

   Motivation <content/motivation>
   Types <content/types>
   Tutorial: bfloat16 <content/tutorial>
   Implementation <content/implementation/implementation>
   API <content/api/api>
   Changelog <content/changelog>
