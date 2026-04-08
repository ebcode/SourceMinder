"""Python 3.8+ walrus operator (:=) examples."""


def walrus_in_if():
    """Walrus operator in if statement."""
    data = [1, 2, 3, 4, 5]

    if (nn := len(data)) > 3:
        print(f"List has {n} items")


def walrus_in_while():
    """Walrus operator in while loop."""
    data = []

    while (line := input("Enter: ")) != "quit":
        data.append(line)


def walrus_in_comprehension():
    """Walrus operator in list comprehension."""
    data = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]

    # Compute once, use twice
    filtered = [yy for xx in data if (yy := xx * 2) > 10]

    return filtered


def walrus_with_function_call():
    """Walrus operator to avoid repeated function calls."""
    def expensive_computation(xx):
        return xx ** 2

    values = [1, 2, 3, 4, 5]

    # Compute once, use in condition
    results = [yy for xx in values if (yy := expensive_computation(xx)) > 10]

    return results


def walrus_in_nested_scope():
    """Walrus operator in nested conditions."""
    data = {"user": {"name": "Alice", "age": 30}}

    if (user := data.get("user")) and (age := user.get("age")) >= 18:
        print(f"Adult user: {user['name']}, age {age}")


def walrus_with_regex():
    """Walrus operator with regex matching."""
    import re

    text = "Email: alice@example.com"

    if (match := re.search(r'(\w+)@(\w+\.\w+)', text)):
        username = match.group(1)
        domain = match.group(2)
        print(f"User: {username}, Domain: {domain}")


def walrus_in_exception_handling():
    """Walrus operator in exception handling."""
    try:
        if (value := int("123")) > 100:
            print(f"Large value: {value}")
    except ValueError:
        print("Invalid value")


def walrus_with_file_reading():
    """Walrus operator for file reading."""
    # Read file line by line until condition met
    with open('data.txt') as f:
        while (line := f.readline()) and line.strip():
            process(line)


def process(line):
    """Dummy processing function."""
    print(line)


def walrus_in_lambda():
    """Walrus operator in lambda (limited use)."""
    # Note: walrus in lambda requires parentheses
    func = lambda xx: (yy := xx * 2, yy + 1)[1]
    return func(5)


def walrus_multiple_assignments():
    """Multiple walrus assignments."""
    data = [1, 2, 3, 4, 5]

    if (length := len(data)) > 3 and (total := sum(data)) > 10:
        avg = total / length
        print(f"Average: {avg}")


def walrus_in_dict_comprehension():
    """Walrus operator in dict comprehension."""
    numbers = [1, 2, 3, 4, 5]
    squares = {xx: square for xx in numbers if (square := xx ** 2) > 10}
    return squares
