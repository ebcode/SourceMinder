/*
 * Every word in c/config/keywords.txt used in a naming position.
 *
 * C reserves all 43, so none can appear bare. Every one is exercised as a
 * capitalized or upper-case variant instead: legal C identifiers that the
 * symbol filter folds onto a keyword, because it lowercases before comparing.
 *
 * Note: the ten _Alignas/_Bool/_Static_assert style entries carry capitals in
 * the list itself, and the list is compared with strcmp against an already
 * lowercased symbol, so they can never match anything.
 *
 * All identifiers are two characters or longer, so nothing here is dropped on
 * minimum length rather than on keyword-ness.
 */

/* Capitalized variants as file-scope variables. */
int Auto = 1;
int Break = 2;
int Case = 3;
int Char = 4;
int Const = 5;
int Continue = 6;
int Default = 7;
int Do = 8;
int Double = 9;
int Else = 10;
int Enum = 11;
int Extern = 12;
int Float = 13;
int For = 14;
int Goto = 15;
int If = 16;
int Inline = 17;
int Int = 18;
int Long = 19;
int Register = 20;
int Restrict = 21;
int Return = 22;
int Short = 23;
int Signed = 24;
int Static = 25;
int Struct = 26;
int Switch = 27;
int Typedef = 28;
int Union = 29;
int Unsigned = 30;
int Void = 31;
int Volatile = 32;
int While = 33;
int _alignas = 34;
int _alignof = 35;
int _atomic = 36;
int _bool = 37;
int _complex = 38;
int _generic = 39;
int _imaginary = 40;
int _noreturn = 41;
int _static_assert = 42;
int _thread_local = 43;

/* Upper-case variants as enum constants. */
enum KeywordCodes {
    AUTO,
    BREAK,
    CASE,
    CHAR,
    CONST,
    CONTINUE,
    DEFAULT,
    DO,
    DOUBLE,
    ELSE,
    ENUM,
    EXTERN,
    FLOAT,
    FOR,
    GOTO,
    IF,
    INLINE,
    INT,
    LONG,
    REGISTER,
    RESTRICT,
    RETURN,
    SHORT,
    SIGNED,
    STATIC,
    STRUCT,
    SWITCH,
    TYPEDEF,
    UNION,
    UNSIGNED,
    VOID,
    VOLATILE,
    WHILE,
    _ALIGNAS,
    _ALIGNOF,
    _ATOMIC,
    _BOOL,
    _COMPLEX,
    _GENERIC,
    _IMAGINARY,
    _NORETURN,
    _STATIC_ASSERT,
    _THREAD_LOCAL,
};

/* Capitalized variants as struct tags. */
struct Auto { int mem; };
struct Break { int mem; };
struct Case { int mem; };
struct Char { int mem; };
struct Const { int mem; };
struct Continue { int mem; };
struct Default { int mem; };
struct Do { int mem; };
struct Double { int mem; };
struct Else { int mem; };
struct Enum { int mem; };
struct Extern { int mem; };
struct Float { int mem; };
struct For { int mem; };
struct Goto { int mem; };
struct If { int mem; };
struct Inline { int mem; };
struct Int { int mem; };
struct Long { int mem; };
struct Register { int mem; };
struct Restrict { int mem; };
struct Return { int mem; };
struct Short { int mem; };
struct Signed { int mem; };
struct Static { int mem; };
struct Struct { int mem; };
struct Switch { int mem; };
struct Typedef { int mem; };
struct Union { int mem; };
struct Unsigned { int mem; };
struct Void { int mem; };
struct Volatile { int mem; };
struct While { int mem; };
struct _alignas { int mem; };
struct _alignof { int mem; };
struct _atomic { int mem; };
struct _bool { int mem; };
struct _complex { int mem; };
struct _generic { int mem; };
struct _imaginary { int mem; };
struct _noreturn { int mem; };
struct _static_assert { int mem; };
struct _thread_local { int mem; };

/* Capitalized variants as struct members. */
struct KeywordHolder {
    int Auto;
    int Break;
    int Case;
    int Char;
    int Const;
    int Continue;
    int Default;
    int Do;
    int Double;
    int Else;
    int Enum;
    int Extern;
    int Float;
    int For;
    int Goto;
    int If;
    int Inline;
    int Int;
    int Long;
    int Register;
    int Restrict;
    int Return;
    int Short;
    int Signed;
    int Static;
    int Struct;
    int Switch;
    int Typedef;
    int Union;
    int Unsigned;
    int Void;
    int Volatile;
    int While;
    int _alignas;
    int _alignof;
    int _atomic;
    int _bool;
    int _complex;
    int _generic;
    int _imaginary;
    int _noreturn;
    int _static_assert;
    int _thread_local;
};

/* Capitalized variants as typedef names. */
typedef int AutoAlias;
typedef int BreakAlias;
typedef int CaseAlias;
typedef int CharAlias;
typedef int ConstAlias;
typedef int ContinueAlias;
typedef int DefaultAlias;
typedef int DoAlias;
typedef int DoubleAlias;
typedef int ElseAlias;
typedef int EnumAlias;
typedef int ExternAlias;
typedef int FloatAlias;
typedef int ForAlias;
typedef int GotoAlias;
typedef int IfAlias;
typedef int InlineAlias;
typedef int IntAlias;
typedef int LongAlias;
typedef int RegisterAlias;
typedef int RestrictAlias;
typedef int ReturnAlias;
typedef int ShortAlias;
typedef int SignedAlias;
typedef int StaticAlias;
typedef int StructAlias;
typedef int SwitchAlias;
typedef int TypedefAlias;
typedef int UnionAlias;
typedef int UnsignedAlias;
typedef int VoidAlias;
typedef int VolatileAlias;
typedef int WhileAlias;
typedef int _alignasAlias;
typedef int _alignofAlias;
typedef int _atomicAlias;
typedef int _boolAlias;
typedef int _complexAlias;
typedef int _genericAlias;
typedef int _imaginaryAlias;
typedef int _noreturnAlias;
typedef int _static_assertAlias;
typedef int _thread_localAlias;

/* Capitalized variants as function names. */
int AutoFn(void) { return 1; }
int BreakFn(void) { return 1; }
int CaseFn(void) { return 1; }
int CharFn(void) { return 1; }
int ConstFn(void) { return 1; }
int ContinueFn(void) { return 1; }
int DefaultFn(void) { return 1; }
int DoFn(void) { return 1; }
int DoubleFn(void) { return 1; }
int ElseFn(void) { return 1; }
int EnumFn(void) { return 1; }
int ExternFn(void) { return 1; }
int FloatFn(void) { return 1; }
int ForFn(void) { return 1; }
int GotoFn(void) { return 1; }
int IfFn(void) { return 1; }
int InlineFn(void) { return 1; }
int IntFn(void) { return 1; }
int LongFn(void) { return 1; }
int RegisterFn(void) { return 1; }
int RestrictFn(void) { return 1; }
int ReturnFn(void) { return 1; }
int ShortFn(void) { return 1; }
int SignedFn(void) { return 1; }
int StaticFn(void) { return 1; }
int StructFn(void) { return 1; }
int SwitchFn(void) { return 1; }
int TypedefFn(void) { return 1; }
int UnionFn(void) { return 1; }
int UnsignedFn(void) { return 1; }
int VoidFn(void) { return 1; }
int VolatileFn(void) { return 1; }
int WhileFn(void) { return 1; }
int _alignasFn(void) { return 1; }
int _alignofFn(void) { return 1; }
int _atomicFn(void) { return 1; }
int _boolFn(void) { return 1; }
int _complexFn(void) { return 1; }
int _genericFn(void) { return 1; }
int _imaginaryFn(void) { return 1; }
int _noreturnFn(void) { return 1; }
int _static_assertFn(void) { return 1; }
int _thread_localFn(void) { return 1; }

