"""Python 3 class declarations and features."""


class User:
    """Basic class with instance variables."""

    def __init__(self, name: str, email: str):
        self.name = name
        self.email = email
        self.active = True

    def greet(self):
        return f"Hello, {self.name}"


class Counter:
    """Class with class variables."""

    total_count = 0

    def __init__(self):
        self.count = 0
        Counter.total_count += 1

    def increment(self):
        self.count += 1


class Rectangle:
    """Class with instance methods."""

    def __init__(self, width: float, height: float):
        self.width = width
        self.height = height

    def area(self) -> float:
        return self.width * self.height

    def perimeter(self) -> float:
        return 2 * (self.width + self.height)


class Animal:
    """Base class for inheritance."""

    def __init__(self, name: str):
        self.name = name

    def speak(self):
        pass


class Dog(Animal):
    """Derived class."""

    def speak(self):
        return f"{self.name} says Woof!"
