/*
 * Every word in typescript/config/keywords.txt used in a naming position in
 * plain JavaScript. JavaScript files are indexed by index-ts, so they are
 * filtered against the TypeScript keyword list.
 *
 * Many of those words are TypeScript-only (type, interface, declare, keyof,
 * satisfies, readonly) and are ordinary identifiers in JavaScript. This file
 * is sloppy mode -- CommonJS, no 'use strict' -- so the strict-mode reserved
 * words (let, static, yield, private) are legal here too.
 *
 * Case variants matter because the symbol filter lowercases before comparing,
 * so `Break` collides with `break`.
 *
 * All identifiers are two characters or longer, so nothing here is dropped on
 * minimum length rather than on keyword-ness.
 */

// Keywords legal as bindings, block-scoped so they do not collide with the
// function declarations below.
{
  const abstract = 1;
  const accessor = 2;
  const as = 3;
  const asserts = 4;
  const assert = 5;
  const constructor = 6;
  const declare = 7;
  const defer = 8;
  const export = 9;
  const from = 10;
  const get = 11;
  const implements = 12;
  const infer = 13;
  const interface = 14;
  const intrinsic = 15;
  const is = 16;
  const keyof = 17;
  const module = 18;
  const namespace = 19;
  const package = 20;
  const private = 21;
  const protected = 22;
  const public = 23;
  const override = 24;
  const out = 25;
  const readonly = 26;
  const require = 27;
  const global = 28;
  const satisfies = 29;
  const set = 30;
  const static = 31;
  const type = 32;
  const unique = 33;
  const using = 34;
  const yield = 35;
  const async = 36;
  const await = 37;
  const of = 38;
}

// Capitalized variants as bindings.
{
  const Abstract = 'abstract';
  const Accessor = 'accessor';
  const As = 'as';
  const Asserts = 'asserts';
  const Assert = 'assert';
  const Break = 'break';
  const Case = 'case';
  const Catch = 'catch';
  const Class = 'class';
  const Continue = 'continue';
  const Const = 'const';
  const Constructor = 'constructor';
  const Debugger = 'debugger';
  const Declare = 'declare';
  const Default = 'default';
  const Defer = 'defer';
  const Delete = 'delete';
  const Do = 'do';
  const Else = 'else';
  const Enum = 'enum';
  const Export = 'export';
  const Extends = 'extends';
  const False = 'false';
  const Finally = 'finally';
  const For = 'for';
  const From = 'from';
  const Function = 'function';
  const Get = 'get';
  const If = 'if';
  const Implements = 'implements';
  const Import = 'import';
  const In = 'in';
  const Infer = 'infer';
  const Instanceof = 'instanceof';
  const Interface = 'interface';
  const Intrinsic = 'intrinsic';
  const Is = 'is';
  const Keyof = 'keyof';
  const Let = 'let';
  const Module = 'module';
  const Namespace = 'namespace';
  const New = 'new';
  const Null = 'null';
  const Package = 'package';
  const Private = 'private';
  const Protected = 'protected';
  const Public = 'public';
  const Override = 'override';
  const Out = 'out';
  const Readonly = 'readonly';
  const Require = 'require';
  const Global = 'global';
  const Return = 'return';
  const Satisfies = 'satisfies';
  const Set = 'set';
  const Static = 'static';
  const Super = 'super';
  const Switch = 'switch';
  const This = 'this';
  const Throw = 'throw';
  const True = 'true';
  const Try = 'try';
  const Type = 'type';
  const Typeof = 'typeof';
  const Unique = 'unique';
  const Using = 'using';
  const Var = 'var';
  const While = 'while';
  const With = 'with';
  const Yield = 'yield';
  const Async = 'async';
  const Await = 'await';
  const Of = 'of';
}

// Keywords as function names.
function abstract() { return 1; }
function accessor() { return 1; }
function as() { return 1; }
function asserts() { return 1; }
function assert() { return 1; }
function constructor() { return 1; }
function declare() { return 1; }
function defer() { return 1; }
function export() { return 1; }
function from() { return 1; }
function get() { return 1; }
function implements() { return 1; }
function infer() { return 1; }
function interface() { return 1; }
function intrinsic() { return 1; }
function is() { return 1; }
function keyof() { return 1; }
function let() { return 1; }
function module() { return 1; }
function namespace() { return 1; }
function package() { return 1; }
function private() { return 1; }
function protected() { return 1; }
function public() { return 1; }
function override() { return 1; }
function out() { return 1; }
function readonly() { return 1; }
function require() { return 1; }
function global() { return 1; }
function satisfies() { return 1; }
function set() { return 1; }
function static() { return 1; }
function type() { return 1; }
function unique() { return 1; }
function using() { return 1; }
function yield() { return 1; }
function async() { return 1; }
function await() { return 1; }
function of() { return 1; }

// Capitalized variants as class names.
class AbstractHolder { }
class AccessorHolder { }
class AsHolder { }
class AssertsHolder { }
class AssertHolder { }
class BreakHolder { }
class CaseHolder { }
class CatchHolder { }
class ClassHolder { }
class ContinueHolder { }
class ConstHolder { }
class ConstructorHolder { }
class DebuggerHolder { }
class DeclareHolder { }
class DefaultHolder { }
class DeferHolder { }
class DeleteHolder { }
class DoHolder { }
class ElseHolder { }
class EnumHolder { }
class ExportHolder { }
class ExtendsHolder { }
class FalseHolder { }
class FinallyHolder { }
class ForHolder { }
class FromHolder { }
class FunctionHolder { }
class GetHolder { }
class IfHolder { }
class ImplementsHolder { }
class ImportHolder { }
class InHolder { }
class InferHolder { }
class InstanceofHolder { }
class InterfaceHolder { }
class IntrinsicHolder { }
class IsHolder { }
class KeyofHolder { }
class LetHolder { }
class ModuleHolder { }
class NamespaceHolder { }
class NewHolder { }
class NullHolder { }
class PackageHolder { }
class PrivateHolder { }
class ProtectedHolder { }
class PublicHolder { }
class OverrideHolder { }
class OutHolder { }
class ReadonlyHolder { }
class RequireHolder { }
class GlobalHolder { }
class ReturnHolder { }
class SatisfiesHolder { }
class SetHolder { }
class StaticHolder { }
class SuperHolder { }
class SwitchHolder { }
class ThisHolder { }
class ThrowHolder { }
class TrueHolder { }
class TryHolder { }
class TypeHolder { }
class TypeofHolder { }
class UniqueHolder { }
class UsingHolder { }
class VarHolder { }
class WhileHolder { }
class WithHolder { }
class YieldHolder { }
class AsyncHolder { }
class AwaitHolder { }
class OfHolder { }

// Every keyword as an object property name.
const settings = {
  abstract: 1,
  accessor: 1,
  as: 1,
  asserts: 1,
  assert: 1,
  break: 1,
  case: 1,
  catch: 1,
  class: 1,
  continue: 1,
  const: 1,
  constructor: 1,
  debugger: 1,
  declare: 1,
  default: 1,
  defer: 1,
  delete: 1,
  do: 1,
  else: 1,
  enum: 1,
  export: 1,
  extends: 1,
  false: 1,
  finally: 1,
  for: 1,
  from: 1,
  function: 1,
  get: 1,
  if: 1,
  implements: 1,
  import: 1,
  in: 1,
  infer: 1,
  instanceof: 1,
  interface: 1,
  intrinsic: 1,
  is: 1,
  keyof: 1,
  let: 1,
  module: 1,
  namespace: 1,
  new: 1,
  null: 1,
  package: 1,
  private: 1,
  protected: 1,
  public: 1,
  override: 1,
  out: 1,
  readonly: 1,
  require: 1,
  global: 1,
  return: 1,
  satisfies: 1,
  set: 1,
  static: 1,
  super: 1,
  switch: 1,
  this: 1,
  throw: 1,
  true: 1,
  try: 1,
  type: 1,
  typeof: 1,
  unique: 1,
  using: 1,
  var: 1,
  while: 1,
  with: 1,
  yield: 1,
  async: 1,
  await: 1,
  of: 1,
};

// Every keyword as a class method name.
class KeywordHolder {
  abstract() { return 1; }
  accessor() { return 1; }
  as() { return 1; }
  asserts() { return 1; }
  assert() { return 1; }
  break() { return 1; }
  case() { return 1; }
  catch() { return 1; }
  class() { return 1; }
  continue() { return 1; }
  const() { return 1; }
  constructor() { return 1; }
  debugger() { return 1; }
  declare() { return 1; }
  default() { return 1; }
  defer() { return 1; }
  delete() { return 1; }
  do() { return 1; }
  else() { return 1; }
  enum() { return 1; }
  export() { return 1; }
  extends() { return 1; }
  false() { return 1; }
  finally() { return 1; }
  for() { return 1; }
  from() { return 1; }
  function() { return 1; }
  get() { return 1; }
  if() { return 1; }
  implements() { return 1; }
  import() { return 1; }
  in() { return 1; }
  infer() { return 1; }
  instanceof() { return 1; }
  interface() { return 1; }
  intrinsic() { return 1; }
  is() { return 1; }
  keyof() { return 1; }
  let() { return 1; }
  module() { return 1; }
  namespace() { return 1; }
  new() { return 1; }
  null() { return 1; }
  package() { return 1; }
  private() { return 1; }
  protected() { return 1; }
  public() { return 1; }
  override() { return 1; }
  out() { return 1; }
  readonly() { return 1; }
  require() { return 1; }
  global() { return 1; }
  return() { return 1; }
  satisfies() { return 1; }
  set() { return 1; }
  static() { return 1; }
  super() { return 1; }
  switch() { return 1; }
  this() { return 1; }
  throw() { return 1; }
  true() { return 1; }
  try() { return 1; }
  type() { return 1; }
  typeof() { return 1; }
  unique() { return 1; }
  using() { return 1; }
  var() { return 1; }
  while() { return 1; }
  with() { return 1; }
  yield() { return 1; }
  async() { return 1; }
  await() { return 1; }
  of() { return 1; }
}

module.exports = { settings, KeywordHolder };

