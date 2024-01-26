from linked import LinkedSet


s = LinkedSet()
for i in range(100):
    s.add(i)
    list(s)
    print(i)
