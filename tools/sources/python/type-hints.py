"""Python 3 type hints and annotations."""

from typing import (
    List, Dict, Set, Tuple, Optional, Union, Any,
    Callable, Iterator, Iterable, TypeVar, Generic
)


def greet(name: str) -> str:
    """Function with basic type hints."""
    return f"Hello, {name}"


def add_numbers(aa: int, bb: int) -> int:
    """Function with multiple parameters."""
    return aa + bb


def process_items(items: List[str]) -> Dict[str, int]:
    """Function with collection type hints."""
    return {item: len(item) for item in items}


def find_user(user_id: int) -> Optional[Dict[str, Any]]:
    """Function with Optional return type."""
    if user_id > 0:
        return {"id": user_id, "name": "Alice"}
    return None


def parse_value(value: Union[int, str, float]) -> str:
    """Function with Union type."""
    return str(value)


def create_pair(aa: int, bb: str) -> Tuple[int, str]:
    """Function returning tuple."""
    return (aa, bb)


def callback_function(func: Callable[[int, int], int]) -> int:
    """Function accepting callable."""
    return func(5, 3)


class Container(Generic[T]):
    """Generic class with type parameter."""

    def __init__(self, value: T):
        self.value: T = value

    def get(self) -> T:
        return self.value

    def set(self, value: T) -> None:
        self.value = value


T = TypeVar('T')


def get_first(items: List[T]) -> Optional[T]:
    """Generic function with TypeVar."""
    return items[0] if items else None


class User:
    """Class with typed attributes."""

    name: str
    age: int
    emails: List[str]

    def __init__(self, name: str, age: int):
        self.name = name
        self.age = age
        self.emails = []


def iterate_values(values: Iterable[int]) -> Iterator[int]:
    """Function with iterator types."""
    for value in values:
        yield value * 2
