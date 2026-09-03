//! Every word in rust/config/keywords.txt used in a naming position.
//!
//! Rust reserves all but `union` (contextual), so bare use is almost
//! impossible. Raw identifiers (`r#type`) cover 48 of the 52, and capitalized
//! or upper-case variants cover the rest -- ordinary Rust identifiers that the
//! symbol filter folds onto a keyword, because it lowercases before comparing.
//!
//! `self` and `Self` collapse to the same variant, so the case-variant
//! sections carry one of each pair.
//!
//! Note: the list entry `Self` carries a capital and is compared with strcmp
//! against an already lowercased symbol, so it can never match anything.
//!
//! All identifiers are two characters or longer, so nothing here is dropped on
//! minimum length rather than on keyword-ness.

// Case variants are deliberately not snake_case: the point is to collide with
// the keyword list after lowercasing.
#![allow(non_snake_case)]

// Upper-case variants as consts.
pub const AS: i32 = 1;
pub const ASYNC: i32 = 2;
pub const AWAIT: i32 = 3;
pub const BREAK: i32 = 4;
pub const CONST: i32 = 5;
pub const CONTINUE: i32 = 6;
pub const CRATE: i32 = 7;
pub const DYN: i32 = 8;
pub const ELSE: i32 = 9;
pub const ENUM: i32 = 10;
pub const EXTERN: i32 = 11;
pub const FALSE: i32 = 12;
pub const FN: i32 = 13;
pub const FOR: i32 = 14;
pub const IF: i32 = 15;
pub const IMPL: i32 = 16;
pub const IN: i32 = 17;
pub const LET: i32 = 18;
pub const LOOP: i32 = 19;
pub const MATCH: i32 = 20;
pub const MOD: i32 = 21;
pub const MOVE: i32 = 22;
pub const MUT: i32 = 23;
pub const PUB: i32 = 24;
pub const REF: i32 = 25;
pub const RETURN: i32 = 26;
pub const SELF: i32 = 27;
pub const STATIC: i32 = 28;
pub const STRUCT: i32 = 29;
pub const SUPER: i32 = 30;
pub const TRAIT: i32 = 31;
pub const TRUE: i32 = 32;
pub const TYPE: i32 = 33;
pub const UNSAFE: i32 = 34;
pub const USE: i32 = 35;
pub const WHERE: i32 = 36;
pub const WHILE: i32 = 37;
pub const ABSTRACT: i32 = 38;
pub const BECOME: i32 = 39;
pub const BOX: i32 = 40;
pub const DO: i32 = 41;
pub const FINAL: i32 = 42;
pub const MACRO: i32 = 43;
pub const OVERRIDE: i32 = 44;
pub const PRIV: i32 = 45;
pub const TYPEOF: i32 = 46;
pub const UNSIZED: i32 = 47;
pub const VIRTUAL: i32 = 48;
pub const YIELD: i32 = 49;
pub const TRY: i32 = 50;
pub const UNION: i32 = 51;

// Capitalized variants as struct names.
pub struct AsHolder { pub mem: i32 }
pub struct AsyncHolder { pub mem: i32 }
pub struct AwaitHolder { pub mem: i32 }
pub struct BreakHolder { pub mem: i32 }
pub struct ConstHolder { pub mem: i32 }
pub struct ContinueHolder { pub mem: i32 }
pub struct CrateHolder { pub mem: i32 }
pub struct DynHolder { pub mem: i32 }
pub struct ElseHolder { pub mem: i32 }
pub struct EnumHolder { pub mem: i32 }
pub struct ExternHolder { pub mem: i32 }
pub struct FalseHolder { pub mem: i32 }
pub struct FnHolder { pub mem: i32 }
pub struct ForHolder { pub mem: i32 }
pub struct IfHolder { pub mem: i32 }
pub struct ImplHolder { pub mem: i32 }
pub struct InHolder { pub mem: i32 }
pub struct LetHolder { pub mem: i32 }
pub struct LoopHolder { pub mem: i32 }
pub struct MatchHolder { pub mem: i32 }
pub struct ModHolder { pub mem: i32 }
pub struct MoveHolder { pub mem: i32 }
pub struct MutHolder { pub mem: i32 }
pub struct PubHolder { pub mem: i32 }
pub struct RefHolder { pub mem: i32 }
pub struct ReturnHolder { pub mem: i32 }
pub struct SelfHolder { pub mem: i32 }
pub struct StaticHolder { pub mem: i32 }
pub struct StructHolder { pub mem: i32 }
pub struct SuperHolder { pub mem: i32 }
pub struct TraitHolder { pub mem: i32 }
pub struct TrueHolder { pub mem: i32 }
pub struct TypeHolder { pub mem: i32 }
pub struct UnsafeHolder { pub mem: i32 }
pub struct UseHolder { pub mem: i32 }
pub struct WhereHolder { pub mem: i32 }
pub struct WhileHolder { pub mem: i32 }
pub struct AbstractHolder { pub mem: i32 }
pub struct BecomeHolder { pub mem: i32 }
pub struct BoxHolder { pub mem: i32 }
pub struct DoHolder { pub mem: i32 }
pub struct FinalHolder { pub mem: i32 }
pub struct MacroHolder { pub mem: i32 }
pub struct OverrideHolder { pub mem: i32 }
pub struct PrivHolder { pub mem: i32 }
pub struct TypeofHolder { pub mem: i32 }
pub struct UnsizedHolder { pub mem: i32 }
pub struct VirtualHolder { pub mem: i32 }
pub struct YieldHolder { pub mem: i32 }
pub struct TryHolder { pub mem: i32 }
pub struct UnionHolder { pub mem: i32 }

// Capitalized variants as struct fields.
pub struct KeywordHolder {
    pub As: i32,
    pub Async: i32,
    pub Await: i32,
    pub Break: i32,
    pub Const: i32,
    pub Continue: i32,
    pub Crate: i32,
    pub Dyn: i32,
    pub Else: i32,
    pub Enum: i32,
    pub Extern: i32,
    pub False: i32,
    pub Fn: i32,
    pub For: i32,
    pub If: i32,
    pub Impl: i32,
    pub In: i32,
    pub Let: i32,
    pub Loop: i32,
    pub Match: i32,
    pub Mod: i32,
    pub Move: i32,
    pub Mut: i32,
    pub Pub: i32,
    pub Ref: i32,
    pub Return: i32,
    pub Static: i32,
    pub Struct: i32,
    pub Super: i32,
    pub Trait: i32,
    pub True: i32,
    pub Type: i32,
    pub Unsafe: i32,
    pub Use: i32,
    pub Where: i32,
    pub While: i32,
    pub Abstract: i32,
    pub Become: i32,
    pub Box: i32,
    pub Do: i32,
    pub Final: i32,
    pub Macro: i32,
    pub Override: i32,
    pub Priv: i32,
    pub Typeof: i32,
    pub Unsized: i32,
    pub Virtual: i32,
    pub Yield: i32,
    pub Try: i32,
    pub Union: i32,
}

// Capitalized variants as function names.
pub fn AsFn() -> i32 { 1 }
pub fn AsyncFn() -> i32 { 1 }
pub fn AwaitFn() -> i32 { 1 }
pub fn BreakFn() -> i32 { 1 }
pub fn ConstFn() -> i32 { 1 }
pub fn ContinueFn() -> i32 { 1 }
pub fn CrateFn() -> i32 { 1 }
pub fn DynFn() -> i32 { 1 }
pub fn ElseFn() -> i32 { 1 }
pub fn EnumFn() -> i32 { 1 }
pub fn ExternFn() -> i32 { 1 }
pub fn FalseFn() -> i32 { 1 }
pub fn FnFn() -> i32 { 1 }
pub fn ForFn() -> i32 { 1 }
pub fn IfFn() -> i32 { 1 }
pub fn ImplFn() -> i32 { 1 }
pub fn InFn() -> i32 { 1 }
pub fn LetFn() -> i32 { 1 }
pub fn LoopFn() -> i32 { 1 }
pub fn MatchFn() -> i32 { 1 }
pub fn ModFn() -> i32 { 1 }
pub fn MoveFn() -> i32 { 1 }
pub fn MutFn() -> i32 { 1 }
pub fn PubFn() -> i32 { 1 }
pub fn RefFn() -> i32 { 1 }
pub fn ReturnFn() -> i32 { 1 }
pub fn StaticFn() -> i32 { 1 }
pub fn StructFn() -> i32 { 1 }
pub fn SuperFn() -> i32 { 1 }
pub fn TraitFn() -> i32 { 1 }
pub fn TrueFn() -> i32 { 1 }
pub fn TypeFn() -> i32 { 1 }
pub fn UnsafeFn() -> i32 { 1 }
pub fn UseFn() -> i32 { 1 }
pub fn WhereFn() -> i32 { 1 }
pub fn WhileFn() -> i32 { 1 }
pub fn AbstractFn() -> i32 { 1 }
pub fn BecomeFn() -> i32 { 1 }
pub fn BoxFn() -> i32 { 1 }
pub fn DoFn() -> i32 { 1 }
pub fn FinalFn() -> i32 { 1 }
pub fn MacroFn() -> i32 { 1 }
pub fn OverrideFn() -> i32 { 1 }
pub fn PrivFn() -> i32 { 1 }
pub fn TypeofFn() -> i32 { 1 }
pub fn UnsizedFn() -> i32 { 1 }
pub fn VirtualFn() -> i32 { 1 }
pub fn YieldFn() -> i32 { 1 }
pub fn TryFn() -> i32 { 1 }
pub fn UnionFn() -> i32 { 1 }

// Raw identifiers as let bindings: the keyword itself, escaped.
pub fn raw_bindings() -> i32 {
    let r#as = 1;
    let r#async = 1;
    let r#await = 1;
    let r#break = 1;
    let r#const = 1;
    let r#continue = 1;
    let r#dyn = 1;
    let r#else = 1;
    let r#enum = 1;
    let r#extern = 1;
    let r#false = 1;
    let r#fn = 1;
    let r#for = 1;
    let r#if = 1;
    let r#impl = 1;
    let r#in = 1;
    let r#let = 1;
    let r#loop = 1;
    let r#match = 1;
    let r#mod = 1;
    let r#move = 1;
    let r#mut = 1;
    let r#pub = 1;
    let r#ref = 1;
    let r#return = 1;
    let r#static = 1;
    let r#struct = 1;
    let r#trait = 1;
    let r#true = 1;
    let r#type = 1;
    let r#unsafe = 1;
    let r#use = 1;
    let r#where = 1;
    let r#while = 1;
    let r#abstract = 1;
    let r#become = 1;
    let r#box = 1;
    let r#do = 1;
    let r#final = 1;
    let r#macro = 1;
    let r#override = 1;
    let r#priv = 1;
    let r#typeof = 1;
    let r#unsized = 1;
    let r#virtual = 1;
    let r#yield = 1;
    let r#try = 1;
    let r#union = 1;
    r#as + r#async + r#await + r#break + r#const + r#continue + r#dyn + r#else + r#enum + r#extern + r#false + r#fn + r#for + r#if + r#impl + r#in + r#let + r#loop + r#match + r#mod + r#move + r#mut + r#pub + r#ref + r#return + r#static + r#struct + r#trait + r#true + r#type + r#unsafe + r#use + r#where + r#while + r#abstract + r#become + r#box + r#do + r#final + r#macro + r#override + r#priv + r#typeof + r#unsized + r#virtual + r#yield + r#try + r#union
}

// `union` is contextual, so it is legal bare.
pub fn union() -> i32 { 1 }

