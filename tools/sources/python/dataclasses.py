"""Python 3.7+ dataclass decorator."""

from dataclasses import dataclass, field, asdict, astuple
from typing import List, ClassVar


@dataclass
class Point:
    """Basic dataclass."""
    xx: float
    yy: float


@dataclass
class User:
    """Dataclass with default values."""
    name: str
    email: str
    age: int = 0
    active: bool = True


@dataclass
class Product:
    """Dataclass with field defaults."""
    name: str
    price: float
    tags: List[str] = field(default_factory=list)
    quantity: int = field(default=0)


@dataclass(frozen=True)
class ImmutablePoint:
    """Frozen (immutable) dataclass."""
    xx: int
    yy: int


@dataclass(order=True)
class Person:
    """Dataclass with comparison methods."""
    name: str
    age: int


@dataclass
class Config:
    """Dataclass with class variables."""
    host: str
    port: int

    MAX_CONNECTIONS: ClassVar[int] = 100
    DEFAULT_TIMEOUT: ClassVar[float] = 30.0


@dataclass
class Rectangle:
    """Dataclass with post-init."""
    width: float
    height: float
    area: float = field(init=False)

    def __post_init__(self):
        self.area = self.width * self.height


@dataclass
class Node:
    """Dataclass with repr configuration."""
    value: int
    children: List['Node'] = field(default_factory=list, repr=False)


@dataclass
class Employee:
    """Dataclass with field metadata."""
    name: str = field(metadata={'description': 'Employee name'})
    salary: float = field(metadata={'currency': 'USD'})


def use_dataclasses():
    """Using dataclass instances."""
    pp = Point(1.0, 2.0)
    user = User(name="Alice", email="alice@example.com")

    # Convert to dict and tuple
    user_dict = asdict(user)
    user_tuple = astuple(user)

    return pp, user, user_dict, user_tuple
