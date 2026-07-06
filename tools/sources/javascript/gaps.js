// Indexer gap cases: every construct that is currently misclassified or missing.
// Each block carries an "expected:" comment showing what the indexer SHOULD produce.
// "actual:" shows what it produces today (pre-fix).
// Genuine callbacks (anonymous, no name binding) are left as LAM — those are correct.

'use strict';

// ---------------------------------------------------------------------------
// A. const/let/var bindings to function values
//    All four forms should yield a named FUNC, not VAR + anonymous LAM.
// ---------------------------------------------------------------------------

// expected: symbol=arrowConst context=FUNC
// actual:   symbol=arrowConst context=VAR  +  symbol=<lambda> context=LAM
const arrowConst = (ax) => { return ax; };

// expected: symbol=asyncArrow context=FUNC
// actual:   symbol=asyncArrow context=VAR  +  symbol=<lambda> context=LAM
const asyncArrow = async (bx) => { return bx; };

// expected: symbol=funcConst context=FUNC
// actual:   symbol=funcConst context=VAR  +  symbol=<lambda> context=LAM
const funcConst = function (cc) { return cc; };

// expected: symbol=namedFuncConst context=FUNC  (inner name "named" is also fine to keep as FUNC)
// actual:   symbol=namedFuncConst context=VAR  +  symbol=<lambda> context=LAM  (inner "named" lost)
const namedFuncConst = function named(dd) { return dd; };

// For contrast — function declarations already work:
// symbol=declFunc context=FUNC  ✅
function declFunc(ex) { return ex; }

// ---------------------------------------------------------------------------
// B. Generator functions
//    Both plain and async generators are not indexed at all today.
// ---------------------------------------------------------------------------

// expected: symbol=genFunc context=FUNC
// actual:   (not indexed at all)
function* genFunc() { yield 1; }

// expected: symbol=asyncGen context=FUNC
// actual:   (not indexed at all)
async function* asyncGen() { yield 1; }

// Generator expression bound to a const — the binding gap and the generator gap combine:
// expected: symbol=boundGen context=FUNC
// actual:   symbol=boundGen context=VAR  +  (generator body not indexed)
const boundGen = function* () { yield 2; };

// ---------------------------------------------------------------------------
// C. Object-literal function properties
//    Shorthand methods already work; value-form properties do not.
// ---------------------------------------------------------------------------

const obj = {
    // expected: symbol=method context=FUNC parent=obj  ✅  (shorthand already works)
    method() { return 1; },

    // expected: symbol=propArrow context=FUNC parent=obj
    // actual:   symbol=propArrow context=PROP
    propArrow: () => 2,

    // expected: symbol=propFunc context=FUNC parent=obj
    // actual:   symbol=propFunc context=PROP
    propFunc: function () { return 3; },

    // expected: symbol=asyncProp context=FUNC parent=obj
    // actual:   symbol=asyncProp context=PROP
    asyncProp: async (id) => { return id; },
};

// ---------------------------------------------------------------------------
// D. Member-assigned functions  (NodeBB's dominant idiom)
//    `Namespace.method = function() {}` — the name is lost, body is anonymous LAM.
// ---------------------------------------------------------------------------

const UserEmail = {};

// expected: symbol=exists context=FUNC parent=UserEmail
// actual:   symbol=exists context=PROP  +  symbol=<lambda> context=LAM
UserEmail.exists = async function (email) {
    return email != null;
};

// expected: symbol=sendVerificationEmail context=FUNC parent=UserEmail
// actual:   symbol=sendVerificationEmail context=PROP  +  symbol=<lambda> context=LAM
UserEmail.sendVerificationEmail = async function (uid, email) {
    return { uid, email };
};

// Arrow variant:
// expected: symbol=available context=FUNC parent=UserEmail
// actual:   symbol=available context=PROP  +  symbol=<lambda> context=LAM
UserEmail.available = async (email) => !await UserEmail.exists(email);

module.exports = UserEmail;

// ---------------------------------------------------------------------------
// E. Class getters and setters
//    Regular methods and static methods already work; accessors are not indexed at all.
// ---------------------------------------------------------------------------

class Config {
    constructor() { this._value = 0; }

    // expected: symbol=doThing context=FUNC parent=Config  ✅  (regular method works)
    doThing() { return this._value; }

    // expected: symbol=value context=FUNC parent=Config  ✅  (already works)
    // note: single-char names like `get x()` are filtered by MIN_SYMBOL_LENGTH;
    //       use a 2+ char property name to verify getter/setter indexing.
    get value() { return this._value; }

    // expected: symbol=value context=FUNC parent=Config  ✅  (already works)
    set value(vv) { this._value = vv; }

    // expected: symbol=create context=FUNC parent=Config  ✅  (static method works)
    static create() { return new Config(); }
}

// ---------------------------------------------------------------------------
// F. Imports — require()
// ---------------------------------------------------------------------------

// Simple require: binding is the module handle.
// expected: symbol=db context=IMP clue=./database
// actual:   symbol=db context=VAR
const db = require('./database');

// Destructured require: each extracted name should be an IMP.
// expected: symbol=meta context=IMP clue=./meta
// expected: symbol=foo  context=IMP clue=./meta
// actual:   meta and foo are NOT INDEXED AT ALL (not even VAR)
const { meta, foo } = require('./meta');

// Nested destructure with rename:
// expected: symbol=bar context=IMP clue=./utils  (local alias, not original name)
// actual:   (not indexed at all)
const { baz: bar } = require('./utils');

// Dynamic require (best-effort; skip if path is non-literal):
// expected: symbol=plugin context=IMP  (or VAR if path is dynamic — acceptable)
// actual:   symbol=plugin context=VAR
const plugin = require(pluginPath);

// ---------------------------------------------------------------------------
// G. Imports — ES module syntax
//    The indexer handles `import { name }` (named import) correctly.
//    Default imports and aliased imports are currently broken.
// ---------------------------------------------------------------------------

// expected: symbol=defaultExport context=IMP clue=./mod
// actual:   (not indexed at all)
import defaultExport from './mod';

// expected: symbol=alias1 context=IMP clue=./mod2  (local alias is the useful name)
// actual:   symbol=named1 context=IMP  (original name indexed; alias1 missing)
import { named1 as alias1 } from './mod2';

// expected: symbol=named2 context=IMP clue=./mod3  ✅  (plain named import works)
import { named2 } from './mod3';

// ---------------------------------------------------------------------------
// H. module.exports — export surface
//    Currently `exports` appears as PROP parent=module but the exported value
//    is not tracked. A qi user cannot ask "what does this module export?"
// ---------------------------------------------------------------------------

// expected: some way to discover that UserEmail is this module's export surface
//           (e.g. symbol=UserEmail context=EXP, or tracking the RHS of the assignment)
// actual:   symbol=exports context=PROP parent=module  (value not captured)
// (the `module.exports = UserEmail` line is at the top of section D above)

// ---------------------------------------------------------------------------
// I. Genuine callbacks — should remain LAM (no change needed)
// ---------------------------------------------------------------------------

// expected: symbol=<lambda> context=LAM  ✅  (anonymous, no name binding)
arr.map((item) => item.id);
arr.filter(function (xx) { return xx > 0; });
setTimeout(() => { console.log('ok'); }, 100);
