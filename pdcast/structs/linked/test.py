
from random import randint
from set import *
from timeit import timeit

char = lambda: chr(randint(0, 25) + ord("a"))
t = [char() for _ in range(10**6)]

def test(s):
    for x in t:
        s.lru_add(x)


def test2(p):
    for x in t:
        p.add(x)



if __name__ == "__main__":
    s1 = LinkedSet()
    s2 = LinkedSet()
    p1 = set(s1)
    print(timeit(lambda: test(s1), number=10) / 10)
    print(timeit(lambda: test2(s2), number=10) / 10)
    print(timeit(lambda: test2(p1), number=10) / 10)
