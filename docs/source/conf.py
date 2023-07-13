# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = 'pdcast'
copyright = '2023, Eric Erkela'
author = 'Eric Erkela'
release = '0.0.1'


# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = [
    "sphinx.ext.duration",
    "sphinx.ext.doctest",
    "sphinx.ext.autodoc",
    "sphinx.ext.autosummary",
    "sphinx.ext.napoleon",
    "sphinx.ext.intersphinx",
    "sphinx_rtd_theme",
    "sphinx_tabs.tabs",
    "sphinx_togglebutton",
]

templates_path = ['_templates']
exclude_patterns = []

intersphinx_mapping = {
    "cython": ("https://cython.readthedocs.io/en/latest/", None),
    "dateutil": ("https://dateutil.readthedocs.io/en/stable/", None),
    "numba": ("https://numba.pydata.org/numba-doc/latest/", None),
    "numpy": ("https://numpy.org/doc/stable/", None),
    "pandas": ("https://pandas.pydata.org/pandas-docs/stable/", None),
    "python": ("https://docs.python.org/3", None),
}

# executed before every .. doctest:: block
doctest_global_setup = """
from pdcast import *
"""

# compatible builders for sphinx-tabs
sphinx_tabs_valid_builders = [
    "linkcheck",
]

# config for sphinx-togglebutton
togglebutton_hint = "Click to show"  # hint text for collapsible sections
togglebutton_hint_hide = "Click to hide"  # hint text for collapsed sections
togglebutton_open_on_print = True  # open all collapsible sections during print


# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = 'sphinx_rtd_theme'
html_theme_options = {
    "navigation_depth": 4,
}
html_static_path = ['_static']
html_css_files = [
    "autosummary_wrap.css",  # force autosummary tables to wrap descriptions
    "collapsible_arrows.css",  # change color of sphinx-togglebutton arrows
]


# -- Options for EPUB output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#epub-options

epub_show_urls = 'footnote'
