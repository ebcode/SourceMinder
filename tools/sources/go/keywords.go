// Package keywords uses every word in go/config/keywords.txt in a naming
// position.
//
// Go reserves all 25, so none can appear bare. Each is exercised as a
// capitalized or upper-case variant instead: legal Go identifiers that the
// symbol filter folds onto a keyword, because it lowercases before comparing.
//
// Package-scope names must be unique and a type cannot carry a field and a
// method of the same name, so the variants are spread across namespaces:
// capitalized package vars, upper-case consts, fields on KeywordFields and
// methods on KeywordMethods. Named types carry a Type suffix to avoid clashing
// with the vars, which makes them controls rather than collisions.
//
// All identifiers are two characters or longer, so nothing here is dropped on
// minimum length rather than on keyword-ness.
package keywords

// Capitalized variants as package variables.
var (
	Break       = 1
	Case        = 2
	Chan        = 3
	Const       = 4
	Continue    = 5
	Default     = 6
	Defer       = 7
	Else        = 8
	Fallthrough = 9
	For         = 10
	Func        = 11
	Go          = 12
	Goto        = 13
	If          = 14
	Import      = 15
	Interface   = 16
	Map         = 17
	Package     = 18
	Range       = 19
	Return      = 20
	Select      = 21
	Struct      = 22
	Switch      = 23
	Type        = 24
	Var         = 25
)

// Upper-case variants as constants.
const (
	BREAK       = "break"
	CASE        = "case"
	CHAN        = "chan"
	CONST       = "const"
	CONTINUE    = "continue"
	DEFAULT     = "default"
	DEFER       = "defer"
	ELSE        = "else"
	FALLTHROUGH = "fallthrough"
	FOR         = "for"
	FUNC        = "func"
	GO          = "go"
	GOTO        = "goto"
	IF          = "if"
	IMPORT      = "import"
	INTERFACE   = "interface"
	MAP         = "map"
	PACKAGE     = "package"
	RANGE       = "range"
	RETURN      = "return"
	SELECT      = "select"
	STRUCT      = "struct"
	SWITCH      = "switch"
	TYPE        = "type"
	VAR         = "var"
)

// Capitalized variants with a suffix as named types.
type BreakType struct{ Mem int }
type CaseType struct{ Mem int }
type ChanType struct{ Mem int }
type ConstType struct{ Mem int }
type ContinueType struct{ Mem int }
type DefaultType struct{ Mem int }
type DeferType struct{ Mem int }
type ElseType struct{ Mem int }
type FallthroughType struct{ Mem int }
type ForType struct{ Mem int }
type FuncType struct{ Mem int }
type GoType struct{ Mem int }
type GotoType struct{ Mem int }
type IfType struct{ Mem int }
type ImportType struct{ Mem int }
type InterfaceType struct{ Mem int }
type MapType struct{ Mem int }
type PackageType struct{ Mem int }
type RangeType struct{ Mem int }
type ReturnType struct{ Mem int }
type SelectType struct{ Mem int }
type StructType struct{ Mem int }
type SwitchType struct{ Mem int }
type TypeType struct{ Mem int }
type VarType struct{ Mem int }

// Capitalized variants as struct fields.
type KeywordFields struct {
	Break       int
	Case        int
	Chan        int
	Const       int
	Continue    int
	Default     int
	Defer       int
	Else        int
	Fallthrough int
	For         int
	Func        int
	Go          int
	Goto        int
	If          int
	Import      int
	Interface   int
	Map         int
	Package     int
	Range       int
	Return      int
	Select      int
	Struct      int
	Switch      int
	Type        int
	Var         int
}

// Capitalized variants as method names.
type KeywordMethods struct{ Mem int }

func (km KeywordMethods) Break() int       { return km.Mem }
func (km KeywordMethods) Case() int        { return km.Mem }
func (km KeywordMethods) Chan() int        { return km.Mem }
func (km KeywordMethods) Const() int       { return km.Mem }
func (km KeywordMethods) Continue() int    { return km.Mem }
func (km KeywordMethods) Default() int     { return km.Mem }
func (km KeywordMethods) Defer() int       { return km.Mem }
func (km KeywordMethods) Else() int        { return km.Mem }
func (km KeywordMethods) Fallthrough() int { return km.Mem }
func (km KeywordMethods) For() int         { return km.Mem }
func (km KeywordMethods) Func() int        { return km.Mem }
func (km KeywordMethods) Go() int          { return km.Mem }
func (km KeywordMethods) Goto() int        { return km.Mem }
func (km KeywordMethods) If() int          { return km.Mem }
func (km KeywordMethods) Import() int      { return km.Mem }
func (km KeywordMethods) Interface() int   { return km.Mem }
func (km KeywordMethods) Map() int         { return km.Mem }
func (km KeywordMethods) Package() int     { return km.Mem }
func (km KeywordMethods) Range() int       { return km.Mem }
func (km KeywordMethods) Return() int      { return km.Mem }
func (km KeywordMethods) Select() int      { return km.Mem }
func (km KeywordMethods) Struct() int      { return km.Mem }
func (km KeywordMethods) Switch() int      { return km.Mem }
func (km KeywordMethods) Type() int        { return km.Mem }
func (km KeywordMethods) Var() int         { return km.Mem }
