from random import randint
from set import *
from timeit import timeit

char = lambda: chr(randint(0, 25) + ord("a"))
t = [char() for _ in range(10**6)]

def test1(s):
    for x in t:
        s.lru_add(x)


def test2(s):
    for x in t:
        s.add(x)


def test3(s):
    try:
        i = 0
        for x in t:
            s.add(x)
            s.remove(x)
            i += 1
    except:
        print(i)


if __name__ == "__main__":
    f = LinkedSet(max_size=8)
    s = LinkedSet()
    p = set(s)
    print(timeit(lambda: test1(f), number=100) / 100)
    print(timeit(lambda: test3(s), number=100) / 100)
    print(timeit(lambda: test3(p), number=100) / 100)

    # print(timeit(lambda: test3(s), number=10) / 10)
    # print(timeit(lambda: test3(p), number=10) / 10)

    # print(timeit(lambda: s.update(t), number=1))
    # print(timeit(lambda: p.update(t), number=1))

    # s = LinkedSet("abcdef")
    # p = set(s)
    # print(timeit(lambda: "a" in s))
    # print(timeit(lambda: "a" in p))
