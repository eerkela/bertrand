.. pdcast documentation master file, created by
   sphinx-quickstart on Fri Feb 24 16:22:55 2023.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

.. include:: ../../README.rst
   :end-line: 173

.. testsetup:: dispatch

   # detach from pandas to give correct errors during doctest
   import pdcast.attach
   pdcast.attach.detach()

.. include:: ../../README.rst
   :start-line: 174

.. toctree::
   :hidden:
   :caption: Contents
   :maxdepth: 1

   Overview <self>

.. toctree::
   :hidden:
   :maxdepth: 2

   Motivation <motivation>
   Type Index <type_index>
   Implementation <implementation>
   Tutorial: bfloat16 <tutorial>
   API <api>
   Changelog <changelog>
