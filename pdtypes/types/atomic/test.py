

class TestBase:

    def __init__(self, x: int):
        self.x = x

    def new_base(self):
        print("TestBase.new_base called")
        return TestBase(self.x + 1)

    def add(self, other: int):
        print("TestBase.add called")
        return self.x + other

    def __repr__(self) -> str:
        return f"{type(self).__name__}({self.x})"


class TestWrapper:

    def __init__(self, base: TestBase):
        self.base = base

    def __getattr__(self, name):
        base = getattr(self.base, name)
        if callable(base):

            def wrapper(*args, **kwargs):
                result = base(*args, **kwargs)
                if isinstance(result, TestBase):
                    print("wrapping result")
                    return TestWrapper(result)
                return result
            return wrapper

        return base
