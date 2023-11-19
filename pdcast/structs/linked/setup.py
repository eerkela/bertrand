from setuptools import setup, Extension


linked_list = Extension(
    "list",
    sources=["list.cpp"],
)


linked_set = Extension(
    "set",
    sources=["set.cpp"],
)


setup(
    name='linked',
    version='0.1.0',
    description='A linked list implementation in C++',
    ext_modules=[linked_set],
)
