export module example.module2;

export import example;


export namespace example::module2 {

    int sub(int a = 2, int b = 1) {
        return a - b;
    }

    int mul(int a = 2, int b = 1) {
        return a * b;
    }

}
