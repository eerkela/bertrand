module;
#include <iostream>
#include <string>
export module bertrand.abc;


void foo() {
    std::cout << "Hello from foo()!\n";
}


export void bar() {
    std::cout << "Hello from bar()!\n";
}


export void baz(const std::string& name) {
    std::cout << "Hello, " << name << "!\n";
}
