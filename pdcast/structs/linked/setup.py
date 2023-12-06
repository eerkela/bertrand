from setuptools import setup, Extension


compile_args = ["-O3"]

setup(
    name='linked',
    version='0.1.0',
    description='A linked list implementation in C++',
    ext_modules=[
        Extension("list", sources=["list.cpp"], extra_compile_args=compile_args),
        Extension("set", sources=["set.cpp"], extra_compile_args=compile_args),
        Extension("dict", sources=["dict.cpp"], extra_compile_args=compile_args),
    ],
)
