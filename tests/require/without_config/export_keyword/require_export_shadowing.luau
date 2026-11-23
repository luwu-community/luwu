-- verify exported bindings follow local shadowing rules
local m = require("./export_shadowing")

assert(m.foo() == "exported", "exported function should shadow the non-exported local")
assert(m.fruit == nil, "export fruit = nil should create a new binding")
assert(m.animal == "dog", "later non-exported shadowing should not change the export")

return true
