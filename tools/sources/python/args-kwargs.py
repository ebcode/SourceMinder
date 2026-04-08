"""Python 3 *args and **kwargs patterns."""


def var_args(*args):
    """Function with variable positional arguments."""
    for arg in args:
        print(arg)


def var_kwargs(**kwargs):
    """Function with variable keyword arguments."""
    for key, value in kwargs.items():
        print(f"{key}: {value}")


def mixed_args(a, b, *args, **kwargs):
    """Function with mixed argument types."""
    print(f"a={a}, b={b}")
    print(f"args={args}")
    print(f"kwargs={kwargs}")


def call_with_args():
    """Calling functions with unpacking."""
    numbers = [1, 2, 3, 4]
    var_args(*numbers)

    config = {'host': 'localhost', 'port': 8080}
    var_kwargs(**config)


def keyword_only_args(a, b, *, c, d):
    """Function with keyword-only arguments."""
    return a + b + c + d


def positional_only_args(a, b, /, c, d):
    """Function with positional-only arguments (Python 3.8+)."""
    return a + b + c + d


def combined_args(pos_only, /, standard, *, kw_only):
    """Combining positional-only, standard, and keyword-only."""
    return pos_only + standard + kw_only


def default_with_kwargs(a, b=10, **kwargs):
    """Default arguments with **kwargs."""
    return a + b + kwargs.get('c', 0)


def forwarding_args(*args, **kwargs):
    """Forwarding arguments to another function."""
    return another_function(*args, **kwargs)


def another_function(x, y, z=0):
    return x + y + z


def args_and_defaults(a, *args, b=5, **kwargs):
    """Args with defaults and kwargs."""
    total = a + b
    total += sum(args)
    total += sum(kwargs.values())
    return total


class MyClass:
    """Class with various argument patterns."""

    def __init__(self, *args, **kwargs):
        self.args = args
        self.kwargs = kwargs

    def method(self, *args, **kwargs):
        """Instance method with args and kwargs."""
        return args, kwargs


def unpack_in_call():
    """Unpacking in function calls."""
    def add(a, b, c):
        return a + b + c

    numbers = [1, 2, 3]
    result = add(*numbers)

    params = {'a': 1, 'b': 2, 'c': 3}
    result2 = add(**params)

    return result, result2


def args_in_lambda():
    """Args in lambda expressions."""
    sum_all = lambda *args: sum(args)
    result = sum_all(1, 2, 3, 4, 5)
    return result
