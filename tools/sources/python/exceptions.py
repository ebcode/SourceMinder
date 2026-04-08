"""Python 3 exception handling."""


def basic_exception_handling():
    """Basic try/except."""
    try:
        result = 10 / 0
    except ZeroDivisionError:
        print("Cannot divide by zero")


def multiple_exceptions():
    """Handling multiple exception types."""
    try:
        value = int("not a number")
    except ValueError:
        print("Invalid value")
    except TypeError:
        print("Invalid type")


def exception_with_else():
    """Try/except with else clause."""
    try:
        result = 10 / 2
    except ZeroDivisionError:
        print("Error")
    else:
        print(f"Result: {result}")


def exception_with_finally():
    """Try/except with finally."""
    try:
        file = open('test.txt')
        data = file.read()
    except FileNotFoundError:
        print("File not found")
    finally:
        print("Cleanup")


def catch_all_exceptions():
    """Catching all exceptions."""
    try:
        risky_operation()
    except Exception as ee:
        print(f"Error: {ee}")


def risky_operation():
    raise ValueError("Something went wrong")


class ValidationError(Exception):
    """Custom exception class."""

    def __init__(self, field, message):
        self.field = field
        self.message = message
        super().__init__(f"{field}: {message}")


class DatabaseError(Exception):
    """Custom exception with additional attributes."""

    def __init__(self, query, error_code):
        self.query = query
        self.error_code = error_code
        super().__init__(f"Query failed: {query}")


def raise_custom_exception():
    """Raising custom exceptions."""
    raise ValidationError("email", "Invalid format")


def reraise_exception():
    """Re-raising exceptions."""
    try:
        raise ValueError("Original error")
    except ValueError:
        print("Logging error")
        raise


def exception_chaining():
    """Exception chaining with from."""
    try:
        int("invalid")
    except ValueError as e:
        raise RuntimeError("Processing failed") from e


def suppress_exception():
    """Suppressing exceptions."""
    try:
        raise ValueError("Error")
    except ValueError:
        pass
