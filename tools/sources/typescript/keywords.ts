/*
 * Every word in typescript/config/keywords.txt used in a naming position.
 *
 * TypeScript reserves 45 of the 73 in strict mode (modules are always strict),
 * but every one is legal as an object property, an interface member, a class
 * method name and a capitalized identifier. Several entries in the list are
 * contextual keywords that are not reserved at all -- get, set, is, from, of,
 * out, type, declare, namespace.
 *
 * Case variants matter because the symbol filter lowercases before comparing,
 * so `Break` collides with `break`.
 *
 * All identifiers are two characters or longer, so nothing here is dropped on
 * minimum length rather than on keyword-ness.
 */

// Keywords legal as bindings in strict mode, block-scoped so they do not
// collide with the function declarations below.
{
  const abstract = 1;
  const accessor = 2;
  const as = 3;
  const asserts = 4;
  const assert = 5;
  const constructor = 6;
  const declare = 7;
  const defer = 8;
  const from = 9;
  const get = 10;
  const infer = 11;
  const intrinsic = 12;
  const is = 13;
  const keyof = 14;
  const module = 15;
  const namespace = 16;
  const override = 17;
  const out = 18;
  const readonly = 19;
  const require = 20;
  const global = 21;
  const satisfies = 22;
  const set = 23;
  const type = 24;
  const unique = 25;
  const using = 26;
  const async = 27;
  const of = 28;
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
export function abstract(): number { return 1; }
export function accessor(): number { return 1; }
export function as(): number { return 1; }
export function asserts(): number { return 1; }
export function assert(): number { return 1; }
export function constructor(): number { return 1; }
export function declare(): number { return 1; }
export function defer(): number { return 1; }
export function from(): number { return 1; }
export function get(): number { return 1; }
export function infer(): number { return 1; }
export function intrinsic(): number { return 1; }
export function is(): number { return 1; }
export function keyof(): number { return 1; }
export function module(): number { return 1; }
export function namespace(): number { return 1; }
export function override(): number { return 1; }
export function out(): number { return 1; }
export function readonly(): number { return 1; }
export function require(): number { return 1; }
export function global(): number { return 1; }
export function satisfies(): number { return 1; }
export function set(): number { return 1; }
export function type(): number { return 1; }
export function unique(): number { return 1; }
export function using(): number { return 1; }
export function async(): number { return 1; }
export function of(): number { return 1; }

// Capitalized variants as class names.
export class AbstractHolder { }
export class AccessorHolder { }
export class AsHolder { }
export class AssertsHolder { }
export class AssertHolder { }
export class BreakHolder { }
export class CaseHolder { }
export class CatchHolder { }
export class ClassHolder { }
export class ContinueHolder { }
export class ConstHolder { }
export class ConstructorHolder { }
export class DebuggerHolder { }
export class DeclareHolder { }
export class DefaultHolder { }
export class DeferHolder { }
export class DeleteHolder { }
export class DoHolder { }
export class ElseHolder { }
export class EnumHolder { }
export class ExportHolder { }
export class ExtendsHolder { }
export class FalseHolder { }
export class FinallyHolder { }
export class ForHolder { }
export class FromHolder { }
export class FunctionHolder { }
export class GetHolder { }
export class IfHolder { }
export class ImplementsHolder { }
export class ImportHolder { }
export class InHolder { }
export class InferHolder { }
export class InstanceofHolder { }
export class InterfaceHolder { }
export class IntrinsicHolder { }
export class IsHolder { }
export class KeyofHolder { }
export class LetHolder { }
export class ModuleHolder { }
export class NamespaceHolder { }
export class NewHolder { }
export class NullHolder { }
export class PackageHolder { }
export class PrivateHolder { }
export class ProtectedHolder { }
export class PublicHolder { }
export class OverrideHolder { }
export class OutHolder { }
export class ReadonlyHolder { }
export class RequireHolder { }
export class GlobalHolder { }
export class ReturnHolder { }
export class SatisfiesHolder { }
export class SetHolder { }
export class StaticHolder { }
export class SuperHolder { }
export class SwitchHolder { }
export class ThisHolder { }
export class ThrowHolder { }
export class TrueHolder { }
export class TryHolder { }
export class TypeHolder { }
export class TypeofHolder { }
export class UniqueHolder { }
export class UsingHolder { }
export class VarHolder { }
export class WhileHolder { }
export class WithHolder { }
export class YieldHolder { }
export class AsyncHolder { }
export class AwaitHolder { }
export class OfHolder { }

// Every keyword as an object property name.
export const settings = {
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

// Every keyword as an interface member.
export interface KeywordShape {
  abstract: number;
  accessor: number;
  as: number;
  asserts: number;
  assert: number;
  break: number;
  case: number;
  catch: number;
  class: number;
  continue: number;
  const: number;
  constructor: number;
  debugger: number;
  declare: number;
  default: number;
  defer: number;
  delete: number;
  do: number;
  else: number;
  enum: number;
  export: number;
  extends: number;
  false: number;
  finally: number;
  for: number;
  from: number;
  function: number;
  get: number;
  if: number;
  implements: number;
  import: number;
  in: number;
  infer: number;
  instanceof: number;
  interface: number;
  intrinsic: number;
  is: number;
  keyof: number;
  let: number;
  module: number;
  namespace: number;
  new: number;
  null: number;
  package: number;
  private: number;
  protected: number;
  public: number;
  override: number;
  out: number;
  readonly: number;
  require: number;
  global: number;
  return: number;
  satisfies: number;
  set: number;
  static: number;
  super: number;
  switch: number;
  this: number;
  throw: number;
  true: number;
  try: number;
  type: number;
  typeof: number;
  unique: number;
  using: number;
  var: number;
  while: number;
  with: number;
  yield: number;
  async: number;
  await: number;
  of: number;
}

// Every keyword as a class method name (constructor excluded: it must return
// the instance type).
export class KeywordHolder {
  abstract(): number { return 1; }
  accessor(): number { return 1; }
  as(): number { return 1; }
  asserts(): number { return 1; }
  assert(): number { return 1; }
  break(): number { return 1; }
  case(): number { return 1; }
  catch(): number { return 1; }
  class(): number { return 1; }
  continue(): number { return 1; }
  const(): number { return 1; }
  debugger(): number { return 1; }
  declare(): number { return 1; }
  default(): number { return 1; }
  defer(): number { return 1; }
  delete(): number { return 1; }
  do(): number { return 1; }
  else(): number { return 1; }
  enum(): number { return 1; }
  export(): number { return 1; }
  extends(): number { return 1; }
  false(): number { return 1; }
  finally(): number { return 1; }
  for(): number { return 1; }
  from(): number { return 1; }
  function(): number { return 1; }
  get(): number { return 1; }
  if(): number { return 1; }
  implements(): number { return 1; }
  import(): number { return 1; }
  in(): number { return 1; }
  infer(): number { return 1; }
  instanceof(): number { return 1; }
  interface(): number { return 1; }
  intrinsic(): number { return 1; }
  is(): number { return 1; }
  keyof(): number { return 1; }
  let(): number { return 1; }
  module(): number { return 1; }
  namespace(): number { return 1; }
  new(): number { return 1; }
  null(): number { return 1; }
  package(): number { return 1; }
  private(): number { return 1; }
  protected(): number { return 1; }
  public(): number { return 1; }
  override(): number { return 1; }
  out(): number { return 1; }
  readonly(): number { return 1; }
  require(): number { return 1; }
  global(): number { return 1; }
  return(): number { return 1; }
  satisfies(): number { return 1; }
  set(): number { return 1; }
  static(): number { return 1; }
  super(): number { return 1; }
  switch(): number { return 1; }
  this(): number { return 1; }
  throw(): number { return 1; }
  true(): number { return 1; }
  try(): number { return 1; }
  type(): number { return 1; }
  typeof(): number { return 1; }
  unique(): number { return 1; }
  using(): number { return 1; }
  var(): number { return 1; }
  while(): number { return 1; }
  with(): number { return 1; }
  yield(): number { return 1; }
  async(): number { return 1; }
  await(): number { return 1; }
  of(): number { return 1; }
}

