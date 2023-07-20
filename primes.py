



def get_primes(power=32, threshold=0.7):
    primes = [2]
    table_size = 16
    target = threshold * table_size
    for number in range(3, 2**power, 2):
        for prime in primes:
            if prime * prime > number:
                primes.append(number)
                if number > target:
                    print(number)
                    table_size *= 2
                    target = 0.7 * table_size
                break
            if number % prime == 0:
                break
    

get_primes(32, 0.7)

