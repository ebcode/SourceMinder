"""Python 3 comprehension expressions."""


def list_comprehensions():
    """List comprehension examples."""
    # Basic list comprehension
    squares = [xx ** 2 for xx in range(10)]

    # List comprehension with condition
    evens = [xx for xx in range(20) if xx % 2 == 0]

    # Nested list comprehension
    matrix = [[ii * jj for jj in range(5)] for ii in range(5)]

    # List comprehension with multiple for loops
    pairs = [(xx, yy) for xx in range(3) for yy in range(3)]

    return squares, evens, matrix, pairs


def dict_comprehensions():
    """Dictionary comprehension examples."""
    # Basic dict comprehension
    square_dict = {xx: xx ** 2 for xx in range(5)}

    # Dict comprehension with condition
    even_squares = {xx: xx ** 2 for xx in range(10) if xx % 2 == 0}

    # Dict from two lists
    keys = ['a', 'b', 'c']
    values = [1, 2, 3]
    combined = {kk: vv for kk, vv in zip(keys, values)}

    return square_dict, even_squares, combined


def set_comprehensions():
    """Set comprehension examples."""
    # Basic set comprehension
    unique_squares = {xx ** 2 for xx in range(-5, 6)}

    # Set comprehension with condition
    odd_numbers = {xx for xx in range(20) if xx % 2 == 1}

    return unique_squares, odd_numbers


def generator_expressions():
    """Generator expression examples."""
    # Generator expression (lazy evaluation)
    squares_gen = (xx ** 2 for xx in range(1000000))

    # Generator with condition
    evens_gen = (xx for xx in range(100) if xx % 2 == 0)

    # Sum using generator expression
    total = sum(xx ** 2 for xx in range(100))

    return squares_gen, evens_gen, total
