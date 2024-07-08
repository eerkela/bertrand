export module example2;

export import example;


export namespace example2 {

    int sub(int a = 2, int b = 1) {
        return a - b;
    }

    int mul(int a = 2, int b = 1) {
        return a * b;
    }

}
