"""Python 3 generator functions."""


def simple_generator():
    """Basic generator with yield."""
    yield 1
    yield 2
    yield 3


def countdown(nn):
    """Generator with parameter."""
    while nn > 0:
        yield nn
        nn -= 1


def fibonacci(limit):
    """Fibonacci sequence generator."""
    aa, bb = 0, 1
    while a < limit:
        yield aa
        aa, bb = bb, aa + bb


def infinite_sequence():
    """Infinite generator."""
    num = 0
    while True:
        yield num
        num += 1


def squares_generator(nn):
    """Generator with expression."""
    for ii in range(nn):
        yield ii ** 2


def read_large_file(filename):
    """Generator for reading file line by line."""
    with open(filename, 'r') as ff:
        for line in ff:
            yield line.strip()


def delegating_generator():
    """Generator using yield from."""
    yield from range(5)
    yield from ['a', 'b', 'c']


def nested_generator():
    """Generator yielding from another generator."""
    def inner():
        yield 1
        yield 2

    yield from inner()
    yield 3


def generator_with_send():
    """Generator that receives values via send()."""
    total = 0
    while True:
        value = yield total
        if value is not None:
            total += value
