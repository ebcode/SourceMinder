/*
 * Every Java reserved word and contextual keyword used in a naming position.
 *
 * There is no java/config/keywords.txt yet -- the Java indexer is planned but
 * not built -- so this list comes from the language spec. When that config is
 * added, this file should be regenerated from it.
 *
 * Java reserves all of the first group, so those appear only as capitalized or
 * upper-case variants: legal Java identifiers that the symbol filter folds onto
 * a keyword, because it lowercases before comparing. The contextual keywords
 * are ordinary identifiers and appear bare.
 *
 * All identifiers are two characters or longer, so nothing here is dropped on
 * minimum length rather than on keyword-ness.
 */

class KeywordHolder {

    // Capitalized variants as fields.
    int Abstract;
    int Assert;
    int Boolean;
    int Break;
    int Byte;
    int Case;
    int Catch;
    int Char;
    int Class;
    int Const;
    int Continue;
    int Default;
    int Do;
    int Double;
    int Else;
    int Enum;
    int Extends;
    int Final;
    int Finally;
    int Float;
    int For;
    int Goto;
    int If;
    int Implements;
    int Import;
    int Instanceof;
    int Int;
    int Interface;
    int Long;
    int Native;
    int New;
    int Package;
    int Private;
    int Protected;
    int Public;
    int Return;
    int Short;
    int Static;
    int Strictfp;
    int Super;
    int Switch;
    int Synchronized;
    int This;
    int Throw;
    int Throws;
    int Transient;
    int Try;
    int Void;
    int Volatile;
    int While;
    int True;
    int False;
    int Null;
    int Exports;
    int Module;
    int Open;
    int Opens;
    int Permits;
    int Provides;
    int Record;
    int Requires;
    int Sealed;
    int To;
    int Transitive;
    int Uses;
    int Var;
    int With;
    int Yield;

    // Upper-case variants as constants.
    static final int ABSTRACT = 1;
    static final int ASSERT = 1;
    static final int BOOLEAN = 1;
    static final int BREAK = 1;
    static final int BYTE = 1;
    static final int CASE = 1;
    static final int CATCH = 1;
    static final int CHAR = 1;
    static final int CLASS = 1;
    static final int CONST = 1;
    static final int CONTINUE = 1;
    static final int DEFAULT = 1;
    static final int DO = 1;
    static final int DOUBLE = 1;
    static final int ELSE = 1;
    static final int ENUM = 1;
    static final int EXTENDS = 1;
    static final int FINAL = 1;
    static final int FINALLY = 1;
    static final int FLOAT = 1;
    static final int FOR = 1;
    static final int GOTO = 1;
    static final int IF = 1;
    static final int IMPLEMENTS = 1;
    static final int IMPORT = 1;
    static final int INSTANCEOF = 1;
    static final int INT = 1;
    static final int INTERFACE = 1;
    static final int LONG = 1;
    static final int NATIVE = 1;
    static final int NEW = 1;
    static final int PACKAGE = 1;
    static final int PRIVATE = 1;
    static final int PROTECTED = 1;
    static final int PUBLIC = 1;
    static final int RETURN = 1;
    static final int SHORT = 1;
    static final int STATIC = 1;
    static final int STRICTFP = 1;
    static final int SUPER = 1;
    static final int SWITCH = 1;
    static final int SYNCHRONIZED = 1;
    static final int THIS = 1;
    static final int THROW = 1;
    static final int THROWS = 1;
    static final int TRANSIENT = 1;
    static final int TRY = 1;
    static final int VOID = 1;
    static final int VOLATILE = 1;
    static final int WHILE = 1;
    static final int TRUE = 1;
    static final int FALSE = 1;
    static final int NULL = 1;
    static final int EXPORTS = 1;
    static final int MODULE = 1;
    static final int OPEN = 1;
    static final int OPENS = 1;
    static final int PERMITS = 1;
    static final int PROVIDES = 1;
    static final int RECORD = 1;
    static final int REQUIRES = 1;
    static final int SEALED = 1;
    static final int TO = 1;
    static final int TRANSITIVE = 1;
    static final int USES = 1;
    static final int VAR = 1;
    static final int WITH = 1;
    static final int YIELD = 1;

    // Capitalized variants as method names.
    int AbstractMethod() { return 1; }
    int AssertMethod() { return 1; }
    int BooleanMethod() { return 1; }
    int BreakMethod() { return 1; }
    int ByteMethod() { return 1; }
    int CaseMethod() { return 1; }
    int CatchMethod() { return 1; }
    int CharMethod() { return 1; }
    int ClassMethod() { return 1; }
    int ConstMethod() { return 1; }
    int ContinueMethod() { return 1; }
    int DefaultMethod() { return 1; }
    int DoMethod() { return 1; }
    int DoubleMethod() { return 1; }
    int ElseMethod() { return 1; }
    int EnumMethod() { return 1; }
    int ExtendsMethod() { return 1; }
    int FinalMethod() { return 1; }
    int FinallyMethod() { return 1; }
    int FloatMethod() { return 1; }
    int ForMethod() { return 1; }
    int GotoMethod() { return 1; }
    int IfMethod() { return 1; }
    int ImplementsMethod() { return 1; }
    int ImportMethod() { return 1; }
    int InstanceofMethod() { return 1; }
    int IntMethod() { return 1; }
    int InterfaceMethod() { return 1; }
    int LongMethod() { return 1; }
    int NativeMethod() { return 1; }
    int NewMethod() { return 1; }
    int PackageMethod() { return 1; }
    int PrivateMethod() { return 1; }
    int ProtectedMethod() { return 1; }
    int PublicMethod() { return 1; }
    int ReturnMethod() { return 1; }
    int ShortMethod() { return 1; }
    int StaticMethod() { return 1; }
    int StrictfpMethod() { return 1; }
    int SuperMethod() { return 1; }
    int SwitchMethod() { return 1; }
    int SynchronizedMethod() { return 1; }
    int ThisMethod() { return 1; }
    int ThrowMethod() { return 1; }
    int ThrowsMethod() { return 1; }
    int TransientMethod() { return 1; }
    int TryMethod() { return 1; }
    int VoidMethod() { return 1; }
    int VolatileMethod() { return 1; }
    int WhileMethod() { return 1; }
    int TrueMethod() { return 1; }
    int FalseMethod() { return 1; }
    int NullMethod() { return 1; }
    int ExportsMethod() { return 1; }
    int ModuleMethod() { return 1; }
    int OpenMethod() { return 1; }
    int OpensMethod() { return 1; }
    int PermitsMethod() { return 1; }
    int ProvidesMethod() { return 1; }
    int RecordMethod() { return 1; }
    int RequiresMethod() { return 1; }
    int SealedMethod() { return 1; }
    int ToMethod() { return 1; }
    int TransitiveMethod() { return 1; }
    int UsesMethod() { return 1; }
    int VarMethod() { return 1; }
    int WithMethod() { return 1; }
    int YieldMethod() { return 1; }

    // Contextual keywords are legal bare identifiers.
    int contextual() {
        int exports = 1;
        int module = 1;
        int open = 1;
        int opens = 1;
        int permits = 1;
        int provides = 1;
        int record = 1;
        int requires = 1;
        int sealed = 1;
        int to = 1;
        int transitive = 1;
        int uses = 1;
        int var = 1;
        int with = 1;
        int yield = 1;
        return exports + module + open + opens + permits + provides + record + requires + sealed + to + transitive + uses + var + with + yield;
    }
}

