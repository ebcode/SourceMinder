"""Every word in python/config/keywords.txt used in a naming position.

Python reserves almost all of them, so only `self` and `cls` appear bare --
and neither is actually a Python keyword, they are naming conventions that
the keyword list should probably not contain.

Everything else appears as a case variant. Those are ordinary identifiers to
Python, but the symbol filter lowercases before comparing, so `If` and `FOR`
collide with the keyword list exactly as the bare words would.

All identifiers are two characters or longer, so nothing here is dropped on
minimum length rather than on keyword-ness.
"""

# The two list entries that are legal Python identifiers.
self = "self"
cls = "cls"

# Capitalized variants as module-level names.
And = 1
As = 2
Assert = 3
Async = 4
Await = 5
Break = 6
Class = 7
Continue = 8
Def = 9
Del = 10
Elif = 11
Else = 12
Except = 13
Finally = 14
For = 15
From = 16
Global = 17
If = 18
Import = 19
In = 20
Is = 21
Lambda = 22
Nonlocal = 23
Not = 24
Or = 25
Pass = 26
Raise = 27
Return = 28
Try = 29
While = 30
With = 31
Yield = 32
Self = 33
Cls = 34

# Upper-case variants as module-level constants.
FALSE = "False"
NONE = "None"
TRUE = "True"
AND = "and"
AS = "as"
ASSERT = "assert"
ASYNC = "async"
AWAIT = "await"
BREAK = "break"
CLASS = "class"
CONTINUE = "continue"
DEF = "def"
DEL = "del"
ELIF = "elif"
ELSE = "else"
EXCEPT = "except"
FINALLY = "finally"
FOR = "for"
FROM = "from"
GLOBAL = "global"
IF = "if"
IMPORT = "import"
IN = "in"
IS = "is"
LAMBDA = "lambda"
NONLOCAL = "nonlocal"
NOT = "not"
OR = "or"
PASS = "pass"
RAISE = "raise"
RETURN = "return"
TRY = "try"
WHILE = "while"
WITH = "with"
YIELD = "yield"
SELF = "self"
CLS = "cls"

# Capitalized variants as function names.
def And():
    return "and"

def As():
    return "as"

def Assert():
    return "assert"

def Async():
    return "async"

def Await():
    return "await"

def Break():
    return "break"

def Class():
    return "class"

def Continue():
    return "continue"

def Def():
    return "def"

def Del():
    return "del"

def Elif():
    return "elif"

def Else():
    return "else"

def Except():
    return "except"

def Finally():
    return "finally"

def For():
    return "for"

def From():
    return "from"

def Global():
    return "global"

def If():
    return "if"

def Import():
    return "import"

def In():
    return "in"

def Is():
    return "is"

def Lambda():
    return "lambda"

def Nonlocal():
    return "nonlocal"

def Not():
    return "not"

def Or():
    return "or"

def Pass():
    return "pass"

def Raise():
    return "raise"

def Return():
    return "return"

def Try():
    return "try"

def While():
    return "while"

def With():
    return "with"

def Yield():
    return "yield"

def Self():
    return "self"

def Cls():
    return "cls"

# Capitalized variants as class names.
class AndHolder:
    pass

class AsHolder:
    pass

class AssertHolder:
    pass

class AsyncHolder:
    pass

class AwaitHolder:
    pass

class BreakHolder:
    pass

class ClassHolder:
    pass

class ContinueHolder:
    pass

class DefHolder:
    pass

class DelHolder:
    pass

class ElifHolder:
    pass

class ElseHolder:
    pass

class ExceptHolder:
    pass

class FinallyHolder:
    pass

class ForHolder:
    pass

class FromHolder:
    pass

class GlobalHolder:
    pass

class IfHolder:
    pass

class ImportHolder:
    pass

class InHolder:
    pass

class IsHolder:
    pass

class LambdaHolder:
    pass

class NonlocalHolder:
    pass

class NotHolder:
    pass

class OrHolder:
    pass

class PassHolder:
    pass

class RaiseHolder:
    pass

class ReturnHolder:
    pass

class TryHolder:
    pass

class WhileHolder:
    pass

class WithHolder:
    pass

class YieldHolder:
    pass

class SelfHolder:
    pass

class ClsHolder:
    pass


class KeywordHolder:
    """self and cls in real parameter positions, plus keyword attributes."""

    def __init__(self):
        self.And = None
        self.As = None
        self.Assert = None
        self.Async = None
        self.Await = None
        self.Break = None
        self.Class = None
        self.Continue = None
        self.Def = None
        self.Del = None
        self.Elif = None
        self.Else = None
        self.Except = None
        self.Finally = None
        self.For = None
        self.From = None
        self.Global = None
        self.If = None
        self.Import = None
        self.In = None
        self.Is = None
        self.Lambda = None
        self.Nonlocal = None
        self.Not = None
        self.Or = None
        self.Pass = None
        self.Raise = None
        self.Return = None
        self.Try = None
        self.While = None
        self.With = None
        self.Yield = None
        self.Self = None
        self.Cls = None

    @classmethod
    def build(cls):
        return cls()

    def show(self):
        return self.If

