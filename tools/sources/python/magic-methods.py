"""Python 3 magic/dunder methods."""


class Vector:
    """Class with arithmetic magic methods."""

    def __init__(self, xx, yy):
        self.xx = xx
        self.yy = yy

    def __add__(self, other):
        return Vector(self.xx + other.xx, self.yy + other.yy)

    def __sub__(self, other):
        return Vector(self.xx - other.xx, self.yy - other.yy)

    def __mul__(self, scalar):
        return Vector(self.xx * scalar, self.yy * scalar)

    def __str__(self):
        return f"Vector({self.xx}, {self.yy})"

    def __repr__(self):
        return f"Vector(x={self.xx}, y={self.yy})"

    def __eq__(self, other):
        return self.xx == other.xx and self.yy == other.yy

    def __len__(self):
        return int((self.xx ** 2 + self.yy ** 2) ** 0.5)


class Container:
    """Class with container magic methods."""

    def __init__(self):
        self.items = []

    def __getitem__(self, index):
        return self.items[index]

    def __setitem__(self, index, value):
        self.items[index] = value

    def __delitem__(self, index):
        del self.items[index]

    def __len__(self):
        return len(self.items)

    def __contains__(self, item):
        return item in self.items

    def __iter__(self):
        return iter(self.items)


class CallableClass:
    """Class with __call__ method."""

    def __init__(self, multiplier):
        self.multiplier = multiplier

    def __call__(self, value):
        return value * self.multiplier


class ManagedAttribute:
    """Class with attribute access methods."""

    def __getattr__(self, name):
        return f"Getting {name}"

    def __setattr__(self, name, value):
        super().__setattr__(name, value)

    def __delattr__(self, name):
        super().__delattr__(name)


class ContextManager:
    """Class with context manager methods."""

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False
