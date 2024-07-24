export module bertrand.example_module;

import std;
import bertrand.python;


void foo() {
    std::cout << "Hello from foo()!\n";
}


export [[py::noexport]] void bar() {
    std::cout << "Hello from bar()!\n";
}


/* This is a docstring! */
export void baz(const std::string& name = "World") {
    std::cout << "Hello, " << name << "!\n";
}
