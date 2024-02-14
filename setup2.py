from setuptools import setup
from pybind11.setup_helpers import Pybind11Extension, build_ext

import bertrand

ext_modules = [
    Pybind11Extension(
        "example",
        ["example.cpp"],
    ),
]

setup(
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    include_dirs=[bertrand.get_include()]
)