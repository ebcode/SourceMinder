"""Test fixture for pytest parametrize + bytes literals."""
import pytest


class _AnsibleUnicode(str):
    def __new__(cls, value):
        return str(value)


@pytest.mark.parametrize("cls, args, kwargs", [
    (_AnsibleUnicode, ('Hello',), {}),
    (_AnsibleUnicode, (b'Hello',), {}),
    (_AnsibleUnicode, ('Hello',), {'encoding': 'utf-8'}),
])
def test_objects(cls, args, kwargs):
    result = cls(*args, **kwargs)
    assert isinstance(result, str)
