"""Python 3 async/await syntax."""

import asyncio


async def fetch_data(url):
    """Basic async function."""
    await asyncio.sleep(1)
    return f"Data from {url}"


async def fetch_multiple():
    """Async function calling multiple awaits."""
    result1 = await fetch_data("url1")
    result2 = await fetch_data("url2")
    return result1, result2


async def concurrent_fetch():
    """Concurrent async operations."""
    tasks = [
        fetch_data("url1"),
        fetch_data("url2"),
        fetch_data("url3")
    ]
    results = await asyncio.gather(*tasks)
    return results


async def async_generator():
    """Async generator function."""
    for ii in range(5):
        await asyncio.sleep(0.1)
        yield ii


async def consume_async_gen():
    """Consuming async generator."""
    async for value in async_generator():
        print(value)


class AsyncContextManager:
    """Async context manager."""

    async def __aenter__(self):
        await asyncio.sleep(0.1)
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        await asyncio.sleep(0.1)


async def use_async_context():
    """Using async context manager."""
    async with AsyncContextManager() as manager:
        await fetch_data("example")


async def async_comprehension():
    """Async comprehension."""
    results = [xx async for xx in async_generator()]
    return results


async def main():
    """Main async function."""
    result = await fetch_data("main")
    return result
