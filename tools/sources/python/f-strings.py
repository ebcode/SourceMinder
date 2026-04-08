"""Python 3.6+ f-string formatting."""


name = "Alice"
age = 30

# Basic f-string
greeting = f"Hello, {name}!"

# F-string with expressions
message = f"{name} is {age} years old"
calculation = f"Next year {name} will be {age + 1}"

# F-string with method calls
text = "python"
uppercase = f"Language: {text.upper()}"

# F-string with formatting
pi = 3.14159
formatted_pi = f"Pi is approximately {pi:.2f}"

# F-string with alignment
left_aligned = f"{name:<10}"
right_aligned = f"{name:>10}"
centered = f"{name:^10}"

# F-string with number formatting
number = 1234567
comma_separated = f"{number:,}"
hex_format = f"{number:x}"
binary_format = f"{number:b}"

# F-string with dict access
user = {"name": "Bob", "age": 25}
info = f"User: {user['name']}, Age: {user['age']}"

# F-string with conditional
status = "active"
display = f"Status: {status.upper() if status else 'N/A'}"

# F-string with list comprehension
numbers = [1, 2, 3, 4, 5]
summary = f"Numbers: {', '.join(str(n) for n in numbers)}"

# Multi-line f-string
multiline = f"""
Name: {name}
Age: {age}
Status: Active
"""

# F-string with = for debugging (Python 3.8+)
debug = f"{name=} {age=}"

# F-string with nested expressions
width = 10
height = 20
rectangle_info = f"Area: {width * height}, Perimeter: {2 * (width + height)}"

# F-string with datetime
from datetime import datetime
now = datetime.now()
timestamp = f"Current time: {now:%Y-%m-%d %H:%M:%S}"

# F-string in function
def create_message(person, score):
    return f"{person} scored {score} points"


# F-string with class instance
class Person:
    def __init__(self, name, age):
        self.name = name
        self.age = age

    def __str__(self):
        return f"Person(name={self.name}, age={self.age})"


p = Person("Charlie", 35)
person_str = f"Details: {p}"
