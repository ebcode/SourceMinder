<?php

/*
 * Every word in php/config/keywords.txt used in a naming position.
 *
 * PHP allows reserved words as variable, property, method and class-constant
 * names; only class/function/trait/interface declaration names are barred.
 * Case variants are included because the symbol filter lowercases before it
 * compares, so `$For` collides with `for` even though PHP keeps them apart.
 *
 * All identifiers are two characters or longer, so nothing here is dropped
 * on minimum length rather than on keyword-ness.
 *
 * Position coverage was verified with php -l; the only word PHP rejects is
 * `class` as a class constant (reserved for ::class fetching).
 */

namespace App\Keywords;

// Keywords as local variables.
$abstract = 1;
$and = 2;
$array = 3;
$as = 4;
$break = 5;
$callable = 6;
$case = 7;
$catch = 8;
$class = 9;
$clone = 10;
$const = 11;
$continue = 12;
$declare = 13;
$default = 14;
$die = 15;
$do = 16;
$echo = 17;
$else = 18;
$elseif = 19;
$empty = 20;
$enddeclare = 21;
$endfor = 22;
$endforeach = 23;
$endif = 24;
$endswitch = 25;
$endwhile = 26;
$eval = 27;
$exit = 28;
$extends = 29;
$final = 30;
$finally = 31;
$fn = 32;
$for = 33;
$foreach = 34;
$function = 35;
$global = 36;
$goto = 37;
$if = 38;
$implements = 39;
$include = 40;
$include_once = 41;
$instanceof = 42;
$insteadof = 43;
$interface = 44;
$isset = 45;
$list = 46;
$match = 47;
$namespace = 48;
$new = 49;
$or = 50;
$print = 51;
$private = 52;
$protected = 53;
$public = 54;
$readonly = 55;
$require = 56;
$require_once = 57;
$return = 58;
$static = 59;
$switch = 60;
$throw = 61;
$trait = 62;
$try = 63;
$unset = 64;
$use = 65;
$var = 66;
$while = 67;
$xor = 68;
$yield = 69;

// Case variants: legal PHP identifiers the filter folds onto keywords.
$Abstract = 'abstract';
$And = 'and';
$Array = 'array';
$As = 'as';
$Break = 'break';
$Callable = 'callable';
$Case = 'case';
$Catch = 'catch';
$Class = 'class';
$Clone = 'clone';
$Const = 'const';
$Continue = 'continue';
$Declare = 'declare';
$Default = 'default';
$Die = 'die';
$Do = 'do';
$Echo = 'echo';
$Else = 'else';
$Elseif = 'elseif';
$Empty = 'empty';
$Enddeclare = 'enddeclare';
$Endfor = 'endfor';
$Endforeach = 'endforeach';
$Endif = 'endif';
$Endswitch = 'endswitch';
$Endwhile = 'endwhile';
$Eval = 'eval';
$Exit = 'exit';
$Extends = 'extends';
$Final = 'final';
$Finally = 'finally';
$Fn = 'fn';
$For = 'for';
$Foreach = 'foreach';
$Function = 'function';
$Global = 'global';
$Goto = 'goto';
$If = 'if';
$Implements = 'implements';
$Include = 'include';
$Include_once = 'include_once';
$Instanceof = 'instanceof';
$Insteadof = 'insteadof';
$Interface = 'interface';
$Isset = 'isset';
$List = 'list';
$Match = 'match';
$Namespace = 'namespace';
$New = 'new';
$Or = 'or';
$Print = 'print';
$Private = 'private';
$Protected = 'protected';
$Public = 'public';
$Readonly = 'readonly';
$Require = 'require';
$Require_once = 'require_once';
$Return = 'return';
$Static = 'static';
$Switch = 'switch';
$Throw = 'throw';
$Trait = 'trait';
$Try = 'try';
$Unset = 'unset';
$Use = 'use';
$Var = 'var';
$While = 'while';
$Xor = 'xor';
$Yield = 'yield';

class KeywordHolder
{
    // Keywords as class constants (UPPER case variants).
    public const ABSTRACT = 'abstract';
    public const AND = 'and';
    public const ARRAY = 'array';
    public const AS = 'as';
    public const BREAK = 'break';
    public const CALLABLE = 'callable';
    public const CASE = 'case';
    public const CATCH = 'catch';
    public const CLONE = 'clone';
    public const CONST = 'const';
    public const CONTINUE = 'continue';
    public const DECLARE = 'declare';
    public const DEFAULT = 'default';
    public const DIE = 'die';
    public const DO = 'do';
    public const ECHO = 'echo';
    public const ELSE = 'else';
    public const ELSEIF = 'elseif';
    public const EMPTY = 'empty';
    public const ENDDECLARE = 'enddeclare';
    public const ENDFOR = 'endfor';
    public const ENDFOREACH = 'endforeach';
    public const ENDIF = 'endif';
    public const ENDSWITCH = 'endswitch';
    public const ENDWHILE = 'endwhile';
    public const EVAL = 'eval';
    public const EXIT = 'exit';
    public const EXTENDS = 'extends';
    public const FINAL = 'final';
    public const FINALLY = 'finally';
    public const FN = 'fn';
    public const FOR = 'for';
    public const FOREACH = 'foreach';
    public const FUNCTION = 'function';
    public const GLOBAL = 'global';
    public const GOTO = 'goto';
    public const IF = 'if';
    public const IMPLEMENTS = 'implements';
    public const INCLUDE = 'include';
    public const INCLUDE_ONCE = 'include_once';
    public const INSTANCEOF = 'instanceof';
    public const INSTEADOF = 'insteadof';
    public const INTERFACE = 'interface';
    public const ISSET = 'isset';
    public const LIST = 'list';
    public const MATCH = 'match';
    public const NAMESPACE = 'namespace';
    public const NEW = 'new';
    public const OR = 'or';
    public const PRINT = 'print';
    public const PRIVATE = 'private';
    public const PROTECTED = 'protected';
    public const PUBLIC = 'public';
    public const READONLY = 'readonly';
    public const REQUIRE = 'require';
    public const REQUIRE_ONCE = 'require_once';
    public const RETURN = 'return';
    public const STATIC = 'static';
    public const SWITCH = 'switch';
    public const THROW = 'throw';
    public const TRAIT = 'trait';
    public const TRY = 'try';
    public const UNSET = 'unset';
    public const USE = 'use';
    public const VAR = 'var';
    public const WHILE = 'while';
    public const XOR = 'xor';
    public const YIELD = 'yield';

    // Keywords as properties.
    public $abstract = null;
    public $and = null;
    public $array = null;
    public $as = null;
    public $break = null;
    public $callable = null;
    public $case = null;
    public $catch = null;
    public $class = null;
    public $clone = null;
    public $const = null;
    public $continue = null;
    public $declare = null;
    public $default = null;
    public $die = null;
    public $do = null;
    public $echo = null;
    public $else = null;
    public $elseif = null;
    public $empty = null;
    public $enddeclare = null;
    public $endfor = null;
    public $endforeach = null;
    public $endif = null;
    public $endswitch = null;
    public $endwhile = null;
    public $eval = null;
    public $exit = null;
    public $extends = null;
    public $final = null;
    public $finally = null;
    public $fn = null;
    public $for = null;
    public $foreach = null;
    public $function = null;
    public $global = null;
    public $goto = null;
    public $if = null;
    public $implements = null;
    public $include = null;
    public $include_once = null;
    public $instanceof = null;
    public $insteadof = null;
    public $interface = null;
    public $isset = null;
    public $list = null;
    public $match = null;
    public $namespace = null;
    public $new = null;
    public $or = null;
    public $print = null;
    public $private = null;
    public $protected = null;
    public $public = null;
    public $readonly = null;
    public $require = null;
    public $require_once = null;
    public $return = null;
    public $static = null;
    public $switch = null;
    public $throw = null;
    public $trait = null;
    public $try = null;
    public $unset = null;
    public $use = null;
    public $var = null;
    public $while = null;
    public $xor = null;
    public $yield = null;

    // Keywords as method names.
    public function abstract() { return $this->abstract; }
    public function and() { return $this->and; }
    public function array() { return $this->array; }
    public function as() { return $this->as; }
    public function break() { return $this->break; }
    public function callable() { return $this->callable; }
    public function case() { return $this->case; }
    public function catch() { return $this->catch; }
    public function class() { return $this->class; }
    public function clone() { return $this->clone; }
    public function const() { return $this->const; }
    public function continue() { return $this->continue; }
    public function declare() { return $this->declare; }
    public function default() { return $this->default; }
    public function die() { return $this->die; }
    public function do() { return $this->do; }
    public function echo() { return $this->echo; }
    public function else() { return $this->else; }
    public function elseif() { return $this->elseif; }
    public function empty() { return $this->empty; }
    public function enddeclare() { return $this->enddeclare; }
    public function endfor() { return $this->endfor; }
    public function endforeach() { return $this->endforeach; }
    public function endif() { return $this->endif; }
    public function endswitch() { return $this->endswitch; }
    public function endwhile() { return $this->endwhile; }
    public function eval() { return $this->eval; }
    public function exit() { return $this->exit; }
    public function extends() { return $this->extends; }
    public function final() { return $this->final; }
    public function finally() { return $this->finally; }
    public function fn() { return $this->fn; }
    public function for() { return $this->for; }
    public function foreach() { return $this->foreach; }
    public function function() { return $this->function; }
    public function global() { return $this->global; }
    public function goto() { return $this->goto; }
    public function if() { return $this->if; }
    public function implements() { return $this->implements; }
    public function include() { return $this->include; }
    public function include_once() { return $this->include_once; }
    public function instanceof() { return $this->instanceof; }
    public function insteadof() { return $this->insteadof; }
    public function interface() { return $this->interface; }
    public function isset() { return $this->isset; }
    public function list() { return $this->list; }
    public function match() { return $this->match; }
    public function namespace() { return $this->namespace; }
    public function new() { return $this->new; }
    public function or() { return $this->or; }
    public function print() { return $this->print; }
    public function private() { return $this->private; }
    public function protected() { return $this->protected; }
    public function public() { return $this->public; }
    public function readonly() { return $this->readonly; }
    public function require() { return $this->require; }
    public function require_once() { return $this->require_once; }
    public function return() { return $this->return; }
    public function static() { return $this->static; }
    public function switch() { return $this->switch; }
    public function throw() { return $this->throw; }
    public function trait() { return $this->trait; }
    public function try() { return $this->try; }
    public function unset() { return $this->unset; }
    public function use() { return $this->use; }
    public function var() { return $this->var; }
    public function while() { return $this->while; }
    public function xor() { return $this->xor; }
    public function yield() { return $this->yield; }
}

// Keywords as function arguments and named-argument labels.
function describe(string $class_name, int $for = 0, bool $match = false)
{
    return $class_name . $for . $match;
}

