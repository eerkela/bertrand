
# ScalarType
cdef class Foo:

    base = None
    init_subclass = True

    def __init__(self):
        if self.init_subclass and self.base is None:
            self.init_base()

    cdef void init_base(self):
        print("Hello from Foo.init_base")
        subclass = type(self)

        self._aliases = tuple(subclass.aliases)
        del subclass.aliases

        subclass.base = self

    @property
    def aliases(self) -> tuple:
        return self.base._aliases


cdef class Bar(Foo):

    init_subclass = True

    cdef void init_base(self):
        print("Hello from Bar.init_base")
        super().init_base()



class Baz(Foo):

    aliases = {"a", "b", "c"}


class Qux(Bar):

    aliases = {"x", "y", "z"}


# x = Baz()
# y = Qux()






cdef class A:

    def __init__(self):
        self.init_base()

    cdef void init_base(self):
        print("Hello from A.init_base")


cdef class B(A):

    cdef void init_base(self):
        print("Hello from B.init_base")
        A.init_base(self)


x = A()
y = B()

