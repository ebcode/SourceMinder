"""Python 3 property decorators."""


class Person:
    """Basic property usage."""

    def __init__(self, name):
        self._name = name
        self._age = 0

    @property
    def name(self):
        """Getter for name."""
        return self._name

    @name.setter
    def name(self, value):
        """Setter for name."""
        if not value:
            raise ValueError("Name cannot be empty")
        self._name = value

    @name.deleter
    def name(self):
        """Deleter for name."""
        del self._name

    @property
    def age(self):
        """Read-only property (no setter)."""
        return self._age


class Temperature:
    """Property with conversion logic."""

    def __init__(self, celsius=0):
        self._celsius = celsius

    @property
    def celsius(self):
        return self._celsius

    @celsius.setter
    def celsius(self, value):
        if value < -273.15:
            raise ValueError("Temperature below absolute zero")
        self._celsius = value

    @property
    def fahrenheit(self):
        """Computed property."""
        return (self.celsius * 9/5) + 32

    @fahrenheit.setter
    def fahrenheit(self, value):
        self.celsius = (value - 32) * 5/9


class Rectangle:
    """Property with validation."""

    def __init__(self, width, height):
        self._width = width
        self._height = height

    @property
    def width(self):
        return self._width

    @width.setter
    def width(self, value):
        if value <= 0:
            raise ValueError("Width must be positive")
        self._width = value

    @property
    def height(self):
        return self._height

    @height.setter
    def height(self, value):
        if value <= 0:
            raise ValueError("Height must be positive")
        self._height = value

    @property
    def area(self):
        """Computed read-only property."""
        return self._width * self._height


class CachedProperty:
    """Property with caching."""

    def __init__(self):
        self._cache = None

    @property
    def expensive_computation(self):
        """Cache the result of expensive computation."""
        if self._cache is None:
            print("Computing...")
            self._cache = sum(range(1000000))
        return self._cache


class User:
    """Multiple properties."""

    def __init__(self, first_name, last_name):
        self._first_name = first_name
        self._last_name = last_name

    @property
    def first_name(self):
        return self._first_name

    @first_name.setter
    def first_name(self, value):
        self._first_name = value

    @property
    def last_name(self):
        return self._last_name

    @last_name.setter
    def last_name(self, value):
        self._last_name = value

    @property
    def full_name(self):
        """Derived property from other properties."""
        return f"{self._first_name} {self._last_name}"

    @full_name.setter
    def full_name(self, value):
        parts = value.split()
        self._first_name = parts[0]
        self._last_name = parts[1] if len(parts) > 1 else ""
