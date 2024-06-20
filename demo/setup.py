from bertrand import Extension, setup

setup(
    ext_modules=[
        Extension("demo_cpp", ["demo_cpp.cpp"]),
    ]
)
