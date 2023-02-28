.. pdcast documentation master file, created by
   sphinx-quickstart on Fri Feb 24 16:22:55 2023.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

.. include:: ../../README.rst
   :end-line: 181

.. have to manually detach pdcast before running dispatch tests to give
.. correct errors.

.. testsetup:: dispatch

   # detach from pandas to give correct errors
   import pdcast.attach
   pdcast.attach.detach()

.. include:: ../../README.rst
   :start-line: 182


Contents
========

.. toctree::
   :maxdepth: 1

   Overview <self>

.. toctree::
   :maxdepth: 2

   Motivation <motivation>
