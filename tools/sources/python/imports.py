"""Python 3 import patterns."""

# Standard library imports
import os
import sys
import json

# Import with alias
import numpy as np
import pandas as pd

# Import specific items
from datetime import datetime, timedelta
from pathlib import Path

# Import all (not recommended)
from math import *

# Import with rename
from collections import defaultdict as dd

# Multiple imports on one line
from typing import List, Dict, Optional, Union

# Relative imports (for package structure)
# from . import sibling_module
# from .. import parent_module
# from ..sibling import cousin_module

# Conditional imports
try:
    import optional_package
except ImportError:
    optional_package = None


def use_imports():
    """Using imported modules."""
    # Use os module
    current_dir = os.getcwd()

    # Use aliased import
    array = np.array([1, 2, 3])

    # Use specific import
    now = datetime.now()
    delta = timedelta(days=1)

    # Use renamed import
    counter = dd(int)

    return current_dir, array, now, delta, counter


# Import at different scopes
class MyClass:
    """Import inside class."""

    def method(self):
        import re
        pattern = re.compile(r'\d+')
        return pattern


def my_function():
    """Import inside function."""
    from itertools import chain
    result = list(chain([1, 2], [3, 4]))
    return result


# __all__ for controlling exports
__all__ = ['use_imports', 'MyClass', 'my_function']
