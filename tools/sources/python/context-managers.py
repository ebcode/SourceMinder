"""Python 3 context manager patterns."""


class FileManager:
    """Basic context manager."""

    def __init__(self, filename, mode):
        self.filename = filename
        self.mode = mode
        self.file = None

    def __enter__(self):
        self.file = open(self.filename, self.mode)
        return self.file

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.file:
            self.file.close()
        return False


def use_file_manager():
    """Using custom context manager."""
    with FileManager('test.txt', 'w') as ff:
        ff.write('Hello, World!')


class DatabaseConnection:
    """Context manager with resource cleanup."""

    def __enter__(self):
        print("Connecting to database")
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        print("Closing database connection")
        if exc_type is not None:
            print(f"Exception occurred: {exc_type}")
        return False

    def query(self, sql):
        return f"Executing: {sql}"


def use_database():
    """Using database context manager."""
    with DatabaseConnection() as db:
        result = db.query("SELECT * FROM users")
        return result


from contextlib import contextmanager


@contextmanager
def timer_context():
    """Context manager using decorator."""
    import time
    start = time.time()
    yield
    end = time.time()
    print(f"Elapsed: {end - start:.2f}s")


def use_timer():
    """Using timer context manager."""
    with timer_context():
        sum(range(1000000))


class MultipleResources:
    """Multiple context managers."""

    def use_multiple(self):
        with open('file1.txt') as f1, open('file2.txt') as f2:
            data1 = f1.read()
            data2 = f2.read()
            return data1, data2
