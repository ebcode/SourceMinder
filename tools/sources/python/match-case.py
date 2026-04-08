"""Python 3.10+ match-case pattern matching."""


def match_literal(value):
    """Basic literal matching."""
    match value:
        case 0:
            return "zero"
        case 1:
            return "one"
        case 2:
            return "two"
        case _:
            return "other"


def match_multiple(value):
    """Matching multiple values."""
    match value:
        case 0 | 1 | 2:
            return "small"
        case 3 | 4 | 5:
            return "medium"
        case _:
            return "large"


def match_types(obj):
    """Type-based matching."""
    match obj:
        case int():
            return "integer"
        case str():
            return "string"
        case list():
            return "list"
        case _:
            return "unknown"


def match_sequence(point):
    """Sequence pattern matching."""
    match point:
        case [0, 0]:
            return "origin"
        case [0, yy]:
            return f"on y-axis at {yy}"
        case [xx, 0]:
            return f"on x-axis at {xx}"
        case [xx, yy]:
            return f"point at ({xx}, {yy})"
        case _:
            return "not a point"


def match_with_guard(point):
    """Pattern matching with guard conditions."""
    match point:
        case [xx, yy] if xx == yy:
            return "on diagonal"
        case [xx, yy] if xx > yy:
            return "above diagonal"
        case [xx, yy] if xx < yy:
            return "below diagonal"


def match_dict(obj):
    """Dictionary pattern matching."""
    match obj:
        case {"type": "user", "name": name}:
            return f"User: {name}"
        case {"type": "admin", "name": name, "level": level}:
            return f"Admin: {name} (level {level})"
        case _:
            return "unknown"


def match_class(obj):
    """Class pattern matching."""
    match obj:
        case Point(xx=0, yy=0):
            return "origin"
        case Point(xx=0, yy=yy):
            return f"y-axis: {yy}"
        case Point(xx=xx, yy=0):
            return f"x-axis: {xx}"
        case Point(x=x, y=y):
            return f"point: ({xx}, {yy})"


class Point:
    def __init__(self, xx, yy):
        self.xx = xx
        self.yy = yy


def match_nested(data):
    """Nested pattern matching."""
    match data:
        case {"user": {"name": name, "age": age}}:
            return f"{name} is {age}"
        case {"users": [first, *rest]}:
            return f"First: {first}, Rest: {rest}"
        case _:
            return "no match"


def match_as_pattern(value):
    """Using 'as' to capture patterns."""
    match value:
        case [xx, yy] as point:
            return f"Point {point} with x={xx}, y={yy}"
        case _:
            return "no match"


def match_wildcard(values):
    """Wildcard patterns."""
    match values:
        case [first, *middle, last]:
            return f"First: {first}, Last: {last}"
        case [single]:
            return f"Single: {single}"
        case _:
            return "empty"
