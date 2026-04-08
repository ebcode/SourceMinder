"""Python 3 lambda expressions."""


# Basic lambda
square = lambda xyz: xyz ** 2

# Lambda with multiple parameters
add = lambda aaa, bbb: aaa + bbb
multiply = lambda xy, yz, za: xy * yz * za

# Lambda in function call
numbers = [1, 2, 3, 4, 5]
squared = list(map(lambda xyz: xyz ** 2, numbers))
evens = list(filter(lambda xyz: xyz % 2 == 0, numbers))

# Lambda for sorting
pairs = [(1, 'one'), (3, 'three'), (2, 'two')]
sorted_pairs = sorted(pairs, key=lambda xyz: xyz[0])

# Lambda with conditional
absolute = lambda xyz: xyz if xyz >= 0 else -xyz

# Lambda in reduce
from functools import reduce
total = reduce(lambda acc, xyz: acc + xyz, numbers, 0)

# Lambda as default argument
def process(value, transformer=lambda xyz: xyz * 2):
    return transformer(value)


# Lambda in dictionary
operations = {
    'add': lambda xy, yz: xy + yz,
    'sub': lambda xy, yz: xy - yz,
    'mul': lambda xy, yz: xy * yz,
    'div': lambda xy, yz: xy / yz if yz != 0 else None
}


# Lambda in list comprehension
squares = [(lambda xyz: xyz ** 2)(xyz) for xyz in range(5)]


# Lambda returning lambda (closure)
def make_multiplier(nop):
    return lambda xyz: xyz * nop


double = make_multiplier(2)
triple = make_multiplier(3)


# Lambda with max/min
points = [(1, 2), (3, 1), (5, 4)]
max_by_x = max(points, key=lambda pq: pq[0])
max_by_y = max(points, key=lambda pq: pq[1])


# Lambda in sorted with multiple criteria
students = [
    {'name': 'Alice', 'grade': 85},
    {'name': 'Bob', 'grade': 92},
    {'name': 'Charlie', 'grade': 85}
]
sorted_students = sorted(students, key=lambda st: (st['grade'], st['name']))
