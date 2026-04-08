"""Python 3 decorator patterns."""

import time
from functools import wraps


def timing_decorator(func):
    """Function decorator."""
    @wraps(func)
    def wrapper(*args, **kwargs):
        start = time.time()
        result = func(*args, **kwargs)
        end = time.time()
        print(f"{func.__name__} took {end - start:.2f}s")
        return result
    return wrapper


@timing_decorator
def slow_function():
    time.sleep(1)
    return "Done"


def repeat(times):
    """Decorator with arguments."""
    def decorator(func):
        @wraps(func)
        def wrapper(*args, **kwargs):
            for _ in range(times):
                result = func(*args, **kwargs)
            return result
        return wrapper
    return decorator


@repeat(3)
def greet(name):
    print(f"Hello, {name}")


class MyClass:
    """Class with decorated methods."""

    @staticmethod
    def static_method():
        return "Static method"

    @classmethod
    def class_method(cls):
        return f"Class method of {cls.__name__}"

    @property
    def value(self):
        return self._value

    @value.setter
    def value(self, val):
        self._value = val


def class_decorator(cls):
    """Class decorator."""
    cls.decorated = True
    return cls


@class_decorator
class DecoratedClass:
    pass
