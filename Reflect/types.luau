--!strict
-- This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details

export type Id = any

-- Source Position (x = 1-indexed line, y = 1-indexed column, z = 0-indexed computedOffset)
export type AstPosition = vector

-- Luau::Location (1-indexed begin and end positions)
export type AstLocation = {
    read begin: AstPosition,
    read ["end"]: AstPosition,
}

-- comment block
export type AstComment = {
    read kind: "AstComment",
    -- Comment, BlockComment, or BrokenComment
    read type: (self: AstComment) -> ("single" | "block" | "broken" | "unknown"),
    read location: (self: AstComment) -> AstLocation,
    read setLocation: (self: AstComment, location: AstLocation) -> AstComment,
    read text: (self: AstComment) -> string,
    read properties: (self: AstComment) -> { [string]: any },
}

-- local variable definition/binding
export type AstLocal = {
    read id: Id,
    read kind: "AstLocal",
    read name: (self: AstLocal) -> string,
    read setName: (self: AstLocal, name: string) -> AstLocal,
    read location: (self: AstLocal) -> AstLocation,
    read setLocation: (self: AstLocal, location: AstLocation) -> AstLocal,
    -- Shadowed local variable if this declaration shadows a variable from an outer scope
    read shadow: (self: AstLocal) -> AstLocal?,
    read setShadow: (self: AstLocal, shadow: AstLocal?) -> AstLocal,
    read isConst: (self: AstLocal) -> boolean,
    read setIsConst: (self: AstLocal, isConst: boolean) -> AstLocal,
    read exported: (self: AstLocal) -> boolean,
    read setExported: (self: AstLocal, exported: boolean) -> AstLocal,
    read depth: (self: AstLocal) -> number,
    read setDepth: (self: AstLocal, depth: number) -> AstLocal,
    read annotation: (self: AstLocal) -> AstNode?,
    read setAnnotation: (self: AstLocal, annotation: AstNode?) -> AstLocal,
    read properties: (self: AstLocal) -> { [string]: any },
}

-- parser errored
export type ParseError = {
    read message: string,
    read location: AstLocation,
}

--[=[ table item in table literals 

for example:

```luau
local doc = reflect.parse([[
    local tbl = {
        "hello",             
        foo = 123,           
        ["key" .. 2] = true 
    }
]])

local stat = doc:root():body()[1]        -- AstStatLocal
local tableExpr = stat:values()[1]     -- AstExprTable
local items = tableExpr:items()        -- { AstTableItem }
```
]=]
export type AstTableItem = {
    read kind: "list" | "record" | "general" | ((self: AstTableItem) -> ("list" | "record" | "general")),
    read setKind: (self: AstTableItem, kind: "list" | "record" | "general") -> AstTableItem,
    -- can be nullptr! (foo: nullptr for List, foo=bar: AstExprConstantString for Record, [foo]=bar: AstExpr for General)
    read key: (self: AstTableItem) -> AstNode?,
    read setKey: (self: AstTableItem, key: AstNode?) -> AstTableItem,
    read value: (self: AstTableItem) -> AstNode,
    read setValue: (self: AstTableItem, value: AstNode) -> AstTableItem,
    read properties: (self: AstTableItem) -> { [string]: any },
}

-- Auxiliary AST structures
export type AstTableProp = {
    read id: Id,
    read kind: "AstTableProp",
    read name: (self: AstTableProp) -> string,
    read setName: (self: AstTableProp, name: string) -> AstTableProp,
    read location: (self: AstTableProp) -> AstLocation,
    read setLocation: (self: AstTableProp, location: AstLocation) -> AstTableProp,
    read type: (self: AstTableProp) -> AstType,
    read setType: (self: AstTableProp, type: AstType) -> AstTableProp,
    read access: (self: AstTableProp) -> ("read" | "write" | "readwrite"),
    read setAccess: (self: AstTableProp, access: "read" | "write" | "readwrite") -> AstTableProp,
    read properties: (self: AstTableProp) -> { [string]: any },
}

export type AstTableIndexer = {
    read id: Id,
    read kind: "AstTableIndexer",
    read indexType: (self: AstTableIndexer) -> AstType,
    read setIndexType: (self: AstTableIndexer, indexType: AstType) -> AstTableIndexer,
    read resultType: (self: AstTableIndexer) -> AstType,
    read setResultType: (self: AstTableIndexer, resultType: AstType) -> AstTableIndexer,
    read location: (self: AstTableIndexer) -> AstLocation,
    read setLocation: (self: AstTableIndexer, location: AstLocation) -> AstTableIndexer,
    read access: (self: AstTableIndexer) -> ("read" | "write" | "readwrite"),
    read setAccess: (self: AstTableIndexer, access: "read" | "write" | "readwrite") -> AstTableIndexer,
    read properties: (self: AstTableIndexer) -> { [string]: any },
}

export type AstDeclaredExternTypeProperty = {
    read id: Id,
    read kind: "AstDeclaredExternTypeProperty",
    read name: (self: AstDeclaredExternTypeProperty) -> string,
    read setName: (self: AstDeclaredExternTypeProperty, name: string) -> AstDeclaredExternTypeProperty,
    read location: (self: AstDeclaredExternTypeProperty) -> AstLocation,
    read setLocation: (self: AstDeclaredExternTypeProperty, location: AstLocation) -> AstDeclaredExternTypeProperty,
    read type: (self: AstDeclaredExternTypeProperty) -> AstType,
    read setType: (self: AstDeclaredExternTypeProperty, type: AstType) -> AstDeclaredExternTypeProperty,
    read isMethod: (self: AstDeclaredExternTypeProperty) -> boolean,
    read setIsMethod: (self: AstDeclaredExternTypeProperty, isMethod: boolean) -> AstDeclaredExternTypeProperty,
    read access: (self: AstDeclaredExternTypeProperty) -> ("read" | "write" | "readwrite"),
    read setAccess: (self: AstDeclaredExternTypeProperty, access: "read" | "write" | "readwrite") -> AstDeclaredExternTypeProperty,
    read properties: (self: AstDeclaredExternTypeProperty) -> { [string]: any },
}

export type AstClassProperty = {
    read id: Id,
    read kind: "AstClassProperty",
    read name: (self: AstClassProperty) -> string,
    read setName: (self: AstClassProperty, name: string) -> AstClassProperty,
    read type: (self: AstClassProperty) -> AstType?,
    read setType: (self: AstClassProperty, type: AstType?) -> AstClassProperty,
    read properties: (self: AstClassProperty) -> { [string]: any },
}

export type AstClassMethod = {
    read id: Id,
    read kind: "AstClassMethod",
    read name: (self: AstClassMethod) -> string,
    read setName: (self: AstClassMethod, name: string) -> AstClassMethod,
    read func: (self: AstClassMethod) -> AstExprFunction,
    read setFunc: (self: AstClassMethod, func: AstExprFunction) -> AstClassMethod,
    read properties: (self: AstClassMethod) -> { [string]: any },
}

export type AstTypeList = {
    read id: Id,
    read kind: "AstTypeList",
    read types: (self: AstTypeList) -> {AstType},
    read setTypes: (self: AstTypeList, types: {AstType}) -> AstTypeList,
    read tailType: (self: AstTypeList) -> AstTypePack?,
    read setTailType: (self: AstTypeList, tailType: AstTypePack?) -> AstTypeList,
    read properties: (self: AstTypeList) -> { [string]: any },
}

export type AstAux =
    | AstTableProp | AstTableIndexer | AstDeclaredExternTypeProperty | AstClassProperty
    | AstClassMethod | AstLocal | AstComment | AstTableItem
    | CstTableItem | AstTypeList | { read id: Id, read kind: string }

-- CST for tableitem
export type CstTableItem = {
    read kind: "CstTableItem",
    -- '[', only if Kind == General
    read indexerOpenPosition: (self: CstTableItem) -> AstPosition?,
    -- ']', only if Kind == General
    read indexerClosePosition: (self: CstTableItem) -> AstPosition?,
    -- only if Kind != List
    read equalsPosition: (self: CstTableItem) -> AstPosition?,
    read separatorPosition: (self: CstTableItem) -> AstPosition?,
    -- may be missing for last Item
    read separator: (self: CstTableItem) -> ("comma" | "semicolon" | "missing"),
    read properties: (self: CstTableItem) -> { [string]: any },
}

export type CstNodeCommon = {
    read id: Id,
    read category: "generic",
    read properties: (self: CstNode) -> { [string]: any },
}

export type CstAttr = CstNodeCommon & {
    read kind: "CstAttr",
    -- false when inside an attribute list, ie @[native checked]
    read hasAt: (self: CstAttr) -> boolean,
}

export type CstParametrizedAttr = CstNodeCommon & {
    read kind: "CstParametrizedAttr",
    -- for `@x(args)` form
    read openParenPosition: (self: CstParametrizedAttr) -> AstPosition?,
    read closeParenPosition: (self: CstParametrizedAttr) -> AstPosition?,
    -- Commas inside the `(a, b, c)` arg list
    read argsCommaPositions: (self: CstParametrizedAttr) -> {AstPosition},
}

export type CstExprGroup = CstNodeCommon & { read kind: "CstExprGroup", read closePosition: (self: CstExprGroup) -> AstPosition? }
export type CstExprConstantNumber = CstNodeCommon & { read kind: "CstExprConstantNumber", read value: (self: CstExprConstantNumber) -> string }
export type CstExprConstantInteger = CstNodeCommon & { read kind: "CstExprConstantInteger", read value: (self: CstExprConstantInteger) -> string }

export type CstExprConstantString = CstNodeCommon & {
    read kind: "CstExprConstantString",
    read quoteStyle: (self: CstExprConstantString) -> ("single" | "double" | "raw" | "interp"),
    read sourceString: (self: CstExprConstantString) -> string,
    read blockDepth: (self: CstExprConstantString) -> number,
}

export type CstExprCall = CstNodeCommon & {
    read kind: "CstExprCall",
    read openParens: (self: CstExprCall) -> AstPosition?,
    read closeParens: (self: CstExprCall) -> AstPosition?,
    read commaPositions: (self: CstExprCall) -> {AstPosition},
}

export type CstExprIndexExpr = CstNodeCommon & {
    read kind: "CstExprIndexExpr",
    read openBracketPosition: (self: CstExprIndexExpr) -> AstPosition?,
    read closeBracketPosition: (self: CstExprIndexExpr) -> AstPosition?,
}

export type CstExprFunction = CstNodeCommon & {
    read kind: "CstExprFunction",
    read functionKeywordPosition: (self: CstExprFunction) -> AstPosition?,
    read openGenericsPosition: (self: CstExprFunction) -> AstPosition?,
    read genericsCommaPositions: (self: CstExprFunction) -> {AstPosition},
    read closeGenericsPosition: (self: CstExprFunction) -> AstPosition?,
    read argsAnnotationColonPositions: (self: CstExprFunction) -> {AstPosition},
    read argsCommaPositions: (self: CstExprFunction) -> {AstPosition},
    read varargAnnotationColonPosition: (self: CstExprFunction) -> AstPosition?,
    read returnSpecifierPosition: (self: CstExprFunction) -> AstPosition?,
}

export type CstExprTable = CstNodeCommon & { read kind: "CstExprTable", read items: (self: CstExprTable) -> {CstTableItem} }
export type CstExprOp = CstNodeCommon & { read kind: "CstExprOp", read opPosition: (self: CstExprOp) -> AstPosition? }
export type CstExprTypeAssertion = CstNodeCommon & { read kind: "CstExprTypeAssertion", read opPosition: (self: CstExprTypeAssertion) -> AstPosition? }

export type CstExprIfElse = CstNodeCommon & {
    read kind: "CstExprIfElse",
    read thenPosition: (self: CstExprIfElse) -> AstPosition?,
    read elsePosition: (self: CstExprIfElse) -> AstPosition?,
    read isElseIf: (self: CstExprIfElse) -> boolean,
}

export type CstExprInterpString = CstNodeCommon & { read kind: "CstExprInterpString", read commaPositions: (self: CstExprInterpString) -> {AstPosition} }
export type CstExprExplicitTypeInstantiation = CstNodeCommon & { read kind: "CstExprExplicitTypeInstantiation" }

export type CstStatDo = CstNodeCommon & {
    read kind: "CstStatDo",
    read statsStartPosition: (self: CstStatDo) -> AstPosition?,
    read endPosition: (self: CstStatDo) -> AstPosition?,
}

export type CstStatRepeat = CstNodeCommon & { read kind: "CstStatRepeat", read untilPosition: (self: CstStatRepeat) -> AstPosition? }
export type CstStatReturn = CstNodeCommon & { read kind: "CstStatReturn", read commaPositions: (self: CstStatReturn) -> {AstPosition} }

export type CstStatLocal = CstNodeCommon & {
    read kind: "CstStatLocal",
    read varsAnnotationColonPositions: (self: CstStatLocal) -> {AstPosition},
    read varsCommaPositions: (self: CstStatLocal) -> {AstPosition},
    read valuesCommaPositions: (self: CstStatLocal) -> {AstPosition},
}

export type CstStatFor = CstNodeCommon & {
    read kind: "CstStatFor",
    read annotationColonPosition: (self: CstStatFor) -> AstPosition?,
    read equalsPosition: (self: CstStatFor) -> AstPosition?,
    read endCommaPosition: (self: CstStatFor) -> AstPosition?,
    read stepCommaPosition: (self: CstStatFor) -> AstPosition?,
}

export type CstStatForIn = CstNodeCommon & {
    read kind: "CstStatForIn",
    read varsAnnotationColonPositions: (self: CstStatForIn) -> {AstPosition},
    read varsCommaPositions: (self: CstStatForIn) -> {AstPosition},
    read valuesCommaPositions: (self: CstStatForIn) -> {AstPosition},
}

export type CstStatAssign = CstNodeCommon & {
    read kind: "CstStatAssign",
    read varsCommaPositions: (self: CstStatAssign) -> {AstPosition},
    read equalsPosition: (self: CstStatAssign) -> AstPosition?,
    read valuesCommaPositions: (self: CstStatAssign) -> {AstPosition},
}

export type CstStatCompoundAssign = CstNodeCommon & { read kind: "CstStatCompoundAssign", read opPosition: (self: CstStatCompoundAssign) -> AstPosition? }
export type CstStatFunction = CstNodeCommon & { read kind: "CstStatFunction", read functionKeywordPosition: (self: CstStatFunction) -> AstPosition? }

export type CstStatLocalFunction = CstNodeCommon & {
    read kind: "CstStatLocalFunction",
    read localKeywordPosition: (self: CstStatLocalFunction) -> AstPosition?,
    read functionKeywordPosition: (self: CstStatLocalFunction) -> AstPosition?,
}

export type CstGenericType = CstNodeCommon & { read kind: "CstGenericType", read defaultEqualsPosition: (self: CstGenericType) -> AstPosition? }

export type CstGenericTypePack = CstNodeCommon & {
    read kind: "CstGenericTypePack",
    read ellipsisPosition: (self: CstGenericTypePack) -> AstPosition?,
    read defaultEqualsPosition: (self: CstGenericTypePack) -> AstPosition?,
}

export type CstStatTypeAlias = CstNodeCommon & {
    read kind: "CstStatTypeAlias",
    read typeKeywordPosition: (self: CstStatTypeAlias) -> AstPosition?,
    read genericsOpenPosition: (self: CstStatTypeAlias) -> AstPosition?,
    read genericsCommaPositions: (self: CstStatTypeAlias) -> {AstPosition},
    read genericsClosePosition: (self: CstStatTypeAlias) -> AstPosition?,
    read equalsPosition: (self: CstStatTypeAlias) -> AstPosition?,
}

export type CstStatTypeFunction = CstNodeCommon & {
    read kind: "CstStatTypeFunction",
    read typeKeywordPosition: (self: CstStatTypeFunction) -> AstPosition?,
    read functionKeywordPosition: (self: CstStatTypeFunction) -> AstPosition?,
}

export type CstTypeReference = CstNodeCommon & {
    read kind: "CstTypeReference",
    read prefixPointPosition: (self: CstTypeReference) -> AstPosition?,
    read openParametersPosition: (self: CstTypeReference) -> AstPosition?,
    read parametersCommaPositions: (self: CstTypeReference) -> {AstPosition},
    read closeParametersPosition: (self: CstTypeReference) -> AstPosition?,
}

export type CstTypeTable = CstNodeCommon & { read kind: "CstTypeTable", read isArray: (self: CstTypeTable) -> boolean }

export type CstTypeFunction = CstNodeCommon & {
    read kind: "CstTypeFunction",
    read openGenericsPosition: (self: CstTypeFunction) -> AstPosition?,
    read genericsCommaPositions: (self: CstTypeFunction) -> {AstPosition},
    read closeGenericsPosition: (self: CstTypeFunction) -> AstPosition?,
    read openArgsPosition: (self: CstTypeFunction) -> AstPosition?,
    read argumentNameColonPositions: (self: CstTypeFunction) -> {AstPosition},
    read argumentsCommaPositions: (self: CstTypeFunction) -> {AstPosition},
    read closeArgsPosition: (self: CstTypeFunction) -> AstPosition?,
    read returnArrowPosition: (self: CstTypeFunction) -> AstPosition?,
}

export type CstTypeTypeof = CstNodeCommon & {
    read kind: "CstTypeTypeof",
    read openPosition: (self: CstTypeTypeof) -> AstPosition?,
    read closePosition: (self: CstTypeTypeof) -> AstPosition?,
}

export type CstTypeUnion = CstNodeCommon & {
    read kind: "CstTypeUnion",
    read leadingPosition: (self: CstTypeUnion) -> AstPosition?,
    read separatorPositions: (self: CstTypeUnion) -> {AstPosition},
}

export type CstTypeIntersection = CstNodeCommon & {
    read kind: "CstTypeIntersection",
    read leadingPosition: (self: CstTypeIntersection) -> AstPosition?,
    read separatorPositions: (self: CstTypeIntersection) -> {AstPosition},
}

export type CstTypeSingletonString = CstNodeCommon & {
    read kind: "CstTypeSingletonString",
    read sourceString: (self: CstTypeSingletonString) -> string,
    read blockDepth: (self: CstTypeSingletonString) -> number,
}

export type CstTypeGroup = CstNodeCommon & { read kind: "CstTypeGroup", read closePosition: (self: CstTypeGroup) -> AstPosition? }

export type CstTypePackExplicit = CstNodeCommon & {
    read kind: "CstTypePackExplicit",
    read openParenthesesPosition: (self: CstTypePackExplicit) -> AstPosition?,
    read closeParenthesesPosition: (self: CstTypePackExplicit) -> AstPosition?,
    read commaPositions: (self: CstTypePackExplicit) -> {AstPosition},
}

export type CstTypePackGeneric = CstNodeCommon & { read kind: "CstTypePackGeneric", read ellipsisPosition: (self: CstTypePackGeneric) -> AstPosition? }

export type CstNode =
    | CstAttr | CstParametrizedAttr | CstExprGroup | CstExprConstantNumber | CstExprConstantInteger
    | CstExprConstantString | CstExprCall | CstExprIndexExpr | CstExprFunction | CstExprTable
    | CstExprOp | CstExprTypeAssertion | CstExprIfElse | CstExprInterpString | CstExprExplicitTypeInstantiation
    | CstStatDo | CstStatRepeat | CstStatReturn | CstStatLocal | CstStatFor
    | CstStatForIn | CstStatAssign | CstStatCompoundAssign | CstStatFunction | CstStatLocalFunction
    | CstGenericType | CstGenericTypePack | CstStatTypeAlias | CstStatTypeFunction | CstTypeReference
    | CstTypeTable | CstTypeFunction | CstTypeTypeof | CstTypeUnion | CstTypeIntersection
    | CstTypeSingletonString | CstTypeGroup | CstTypePackExplicit | CstTypePackGeneric
    | (CstNodeCommon & { read kind: string })

-- Common AST node methods and fields
export type AstNodeCommon = {
    read id: Id,
    read prettyprint: (self: AstNode) -> string,
    read location: (self: AstNode) -> AstLocation,
    read setLocation: (self: AstNode, location: AstLocation) -> AstNode,
    read cst: (self: AstNode) -> CstNode?,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?, filter: AstFilter?) -> (),
    read properties: (self: AstNode) -> { [string]: any },
}

export type AstStatCommon = AstNodeCommon & {
    read category: "stat",
    read hasSemicolon: (self: AstStat) -> boolean,
    read setHasSemicolon: (self: AstStat, hasSemicolon: boolean) -> AstStat,
}

export type AstExprCommon = AstNodeCommon & {
    read category: "expr",
}

export type AstTypeCommon = AstNodeCommon & {
    read category: "type",
}

export type AstTypePackCommon = AstNodeCommon & {
    read category: "typePack",
}

-- Statements
export type AstStatBlock = AstStatCommon & {
    read kind: "AstStatBlock",
    --[[ Indicates whether or not this block has been terminated in a
     * syntactically valid way.
     *
     * This is usually but not always done with the 'end' keyword.  AstStatIf
     * and AstStatRepeat are the two main exceptions to this.
     *
     * The 'then' clause of an if statement can properly be closed by the
     * keywords 'else' or 'elseif'.  A 'repeat' loop's body is closed with the
     * 'until' keyword.
     ]]
    read hasEnd: (self: AstStatBlock) -> boolean,
    read setHasEnd: (self: AstStatBlock, hasEnd: boolean) -> AstStatBlock,
    read body: (self: AstStatBlock) -> {AstStat},
    read setBody: (self: AstStatBlock, body: {AstStat}) -> AstStatBlock,
}

export type AstStatIf = AstStatCommon & {
    read kind: "AstStatIf",
    read condition: (self: AstStatIf) -> AstExpr,
    read setCondition: (self: AstStatIf, condition: AstExpr) -> AstStatIf,
    read thenbody: (self: AstStatIf) -> AstStatBlock,
    read setThenBody: (self: AstStatIf, thenbody: AstStatBlock) -> AstStatIf,
    -- Active for 'elseif' as well
    read elsebody: (self: AstStatIf) -> AstStat?,
    read setElseBody: (self: AstStatIf, elsebody: AstStat?) -> AstStatIf,
}

export type AstStatWhile = AstStatCommon & {
    read kind: "AstStatWhile",
    read hasDo: (self: AstStatWhile) -> boolean,
    read setHasDo: (self: AstStatWhile, hasDo: boolean) -> AstStatWhile,
    read condition: (self: AstStatWhile) -> AstExpr,
    read setCondition: (self: AstStatWhile, condition: AstExpr) -> AstStatWhile,
    read body: (self: AstStatWhile) -> AstStatBlock,
    read setBody: (self: AstStatWhile, body: AstStatBlock) -> AstStatWhile,
}

export type AstStatRepeat = AstStatCommon & {
    read kind: "AstStatRepeat",
    read condition: (self: AstStatRepeat) -> AstExpr,
    read setCondition: (self: AstStatRepeat, condition: AstExpr) -> AstStatRepeat,
    read body: (self: AstStatRepeat) -> AstStatBlock,
    read setBody: (self: AstStatRepeat, body: AstStatBlock) -> AstStatRepeat,
}

export type AstStatBreak = AstStatCommon & { read kind: "AstStatBreak" }
export type AstStatContinue = AstStatCommon & { read kind: "AstStatContinue" }

export type AstStatReturn = AstStatCommon & {
    read kind: "AstStatReturn",
    read list: (self: AstStatReturn) -> {AstExpr},
    read setList: (self: AstStatReturn, list: {AstExpr}) -> AstStatReturn,
}

export type AstStatExpr = AstStatCommon & {
    read kind: "AstStatExpr",
    read expr: (self: AstStatExpr) -> AstExpr,
    read setExpr: (self: AstStatExpr, expr: AstExpr) -> AstStatExpr,
}

export type AstStatLocal = AstStatCommon & {
    read kind: "AstStatLocal",
    read isConst: (self: AstStatLocal) -> boolean,
    read setIsConst: (self: AstStatLocal, isConst: boolean) -> AstStatLocal,
    read exported: (self: AstStatLocal) -> boolean,
    read setExported: (self: AstStatLocal, exported: boolean) -> AstStatLocal,
    read vars: (self: AstStatLocal) -> {AstLocal},
    read setVars: (self: AstStatLocal, vars: {AstLocal}) -> AstStatLocal,
    read values: (self: AstStatLocal) -> {AstExpr},
    read setValues: (self: AstStatLocal, values: {AstExpr}) -> AstStatLocal,
}

export type AstStatFor = AstStatCommon & {
    read kind: "AstStatFor",
    read hasDo: (self: AstStatFor) -> boolean,
    read setHasDo: (self: AstStatFor, hasDo: boolean) -> AstStatFor,
    read var: (self: AstStatFor) -> AstLocal,
    read setVar: (self: AstStatFor, var: AstLocal) -> AstStatFor,
    read from: (self: AstStatFor) -> AstExpr,
    read setFrom: (self: AstStatFor, from: AstExpr) -> AstStatFor,
    read to: (self: AstStatFor) -> AstExpr,
    read setTo: (self: AstStatFor, to: AstExpr) -> AstStatFor,
    read step: (self: AstStatFor) -> AstExpr?,
    read setStep: (self: AstStatFor, step: AstExpr?) -> AstStatFor,
    read body: (self: AstStatFor) -> AstStatBlock,
    read setBody: (self: AstStatFor, body: AstStatBlock) -> AstStatFor,
}

export type AstStatForIn = AstStatCommon & {
    read kind: "AstStatForIn",
    read hasIn: (self: AstStatForIn) -> boolean,
    read setHasIn: (self: AstStatForIn, hasIn: boolean) -> AstStatForIn,
    read hasDo: (self: AstStatForIn) -> boolean,
    read setHasDo: (self: AstStatForIn, hasDo: boolean) -> AstStatForIn,
    read vars: (self: AstStatForIn) -> {AstLocal},
    read setVars: (self: AstStatForIn, vars: {AstLocal}) -> AstStatForIn,
    read values: (self: AstStatForIn) -> {AstExpr},
    read setValues: (self: AstStatForIn, values: {AstExpr}) -> AstStatForIn,
    read body: (self: AstStatForIn) -> AstStatBlock,
    read setBody: (self: AstStatForIn, body: AstStatBlock) -> AstStatForIn,
}

export type AstStatAssign = AstStatCommon & {
    read kind: "AstStatAssign",
    read vars: (self: AstStatAssign) -> {AstExpr},
    read setVars: (self: AstStatAssign, vars: {AstExpr}) -> AstStatAssign,
    read values: (self: AstStatAssign) -> {AstExpr},
    read setValues: (self: AstStatAssign, values: {AstExpr}) -> AstStatAssign,
}

export type AstStatCompoundAssign = AstStatCommon & {
    read kind: "AstStatCompoundAssign",
    read op: (self: AstStatCompoundAssign) -> string,
    read setOp: (self: AstStatCompoundAssign, op: string) -> AstStatCompoundAssign,
    read var: (self: AstStatCompoundAssign) -> AstExpr,
    read setVar: (self: AstStatCompoundAssign, var: AstExpr) -> AstStatCompoundAssign,
    read value: (self: AstStatCompoundAssign) -> AstExpr,
    read setValue: (self: AstStatCompoundAssign, value: AstExpr) -> AstStatCompoundAssign,
}

export type AstStatFunction = AstStatCommon & {
    read kind: "AstStatFunction",
    read name: (self: AstStatFunction) -> AstExpr,
    read setName: (self: AstStatFunction, name: AstExpr) -> AstStatFunction,
    read func: (self: AstStatFunction) -> AstExprFunction,
    read setFunc: (self: AstStatFunction, func: AstExprFunction) -> AstStatFunction,
}

export type AstStatLocalFunction = AstStatCommon & {
    read kind: "AstStatLocalFunction",
    read isConst: (self: AstStatLocalFunction) -> boolean,
    read setIsConst: (self: AstStatLocalFunction, isConst: boolean) -> AstStatLocalFunction,
    read name: (self: AstStatLocalFunction) -> AstLocal,
    read setName: (self: AstStatLocalFunction, name: AstLocal) -> AstStatLocalFunction,
    read func: (self: AstStatLocalFunction) -> AstExprFunction,
    read setFunc: (self: AstStatLocalFunction, func: AstExprFunction) -> AstStatLocalFunction,
}

export type AstStatTypeAlias = AstStatCommon & {
    read kind: "AstStatTypeAlias",
    read name: (self: AstStatTypeAlias) -> string,
    read setName: (self: AstStatTypeAlias, name: string) -> AstStatTypeAlias,
    read exported: (self: AstStatTypeAlias) -> boolean,
    read setExported: (self: AstStatTypeAlias, exported: boolean) -> AstStatTypeAlias,
    read type: (self: AstStatTypeAlias) -> AstType,
    read setType: (self: AstStatTypeAlias, type: AstType) -> AstStatTypeAlias,
    read generics: (self: AstStatTypeAlias) -> {AstGenericType},
    read setGenerics: (self: AstStatTypeAlias, generics: {AstGenericType}) -> AstStatTypeAlias,
    read genericPacks: (self: AstStatTypeAlias) -> {AstGenericTypePack},
    read setGenericPacks: (self: AstStatTypeAlias, genericPacks: {AstGenericTypePack}) -> AstStatTypeAlias,
}

export type AstStatTypeFunction = AstStatCommon & {
    read kind: "AstStatTypeFunction",
    read name: (self: AstStatTypeFunction) -> string,
    read setName: (self: AstStatTypeFunction, name: string) -> AstStatTypeFunction,
    read exported: (self: AstStatTypeFunction) -> boolean,
    read setExported: (self: AstStatTypeFunction, exported: boolean) -> AstStatTypeFunction,
    read hasErrors: (self: AstStatTypeFunction) -> boolean,
    read setHasErrors: (self: AstStatTypeFunction, hasErrors: boolean) -> AstStatTypeFunction,
    read body: (self: AstStatTypeFunction) -> AstExprFunction,
    read setBody: (self: AstStatTypeFunction, body: AstExprFunction) -> AstStatTypeFunction,
}

export type AstStatDeclareGlobal = AstStatCommon & {
    read kind: "AstStatDeclareGlobal",
    read name: (self: AstStatDeclareGlobal) -> string,
    read setName: (self: AstStatDeclareGlobal, name: string) -> AstStatDeclareGlobal,
    read type: (self: AstStatDeclareGlobal) -> AstType,
    read setType: (self: AstStatDeclareGlobal, type: AstType) -> AstStatDeclareGlobal,
}

export type AstStatDeclareFunction = AstStatCommon & {
    read kind: "AstStatDeclareFunction",
    read name: (self: AstStatDeclareFunction) -> string,
    read setName: (self: AstStatDeclareFunction, name: string) -> AstStatDeclareFunction,
    read vararg: (self: AstStatDeclareFunction) -> boolean,
    read setVararg: (self: AstStatDeclareFunction, vararg: boolean) -> AstStatDeclareFunction,
    read generics: (self: AstStatDeclareFunction) -> {AstGenericType},
    read setGenerics: (self: AstStatDeclareFunction, generics: {AstGenericType}) -> AstStatDeclareFunction,
    read genericPacks: (self: AstStatDeclareFunction) -> {AstGenericTypePack},
    read setGenericPacks: (self: AstStatDeclareFunction, genericPacks: {AstGenericTypePack}) -> AstStatDeclareFunction,
    read params: (self: AstStatDeclareFunction) -> AstTypeList,
    read setParams: (self: AstStatDeclareFunction, params: AstTypeList) -> AstStatDeclareFunction,
    read returnTypes: (self: AstStatDeclareFunction) -> AstTypePack?,
    read setReturnTypes: (self: AstStatDeclareFunction, returnTypes: AstTypePack?) -> AstStatDeclareFunction,
    read attributes: (self: AstStatDeclareFunction) -> {AstAttr},
    read setAttributes: (self: AstStatDeclareFunction, attributes: {AstAttr}) -> AstStatDeclareFunction,
}

export type AstStatDeclareExternType = AstStatCommon & {
    read kind: "AstStatDeclareExternType",
    read name: (self: AstStatDeclareExternType) -> string,
    read setName: (self: AstStatDeclareExternType, name: string) -> AstStatDeclareExternType,
    read superName: (self: AstStatDeclareExternType) -> string?,
    read setSuperName: (self: AstStatDeclareExternType, superName: string?) -> AstStatDeclareExternType,
    read props: (self: AstStatDeclareExternType) -> {AstDeclaredExternTypeProperty},
    read setProps: (self: AstStatDeclareExternType, props: {AstDeclaredExternTypeProperty}) -> AstStatDeclareExternType,
    read indexer: (self: AstStatDeclareExternType) -> AstTableIndexer?,
    read setIndexer: (self: AstStatDeclareExternType, indexer: AstTableIndexer?) -> AstStatDeclareExternType,
    read generics: (self: AstStatDeclareExternType) -> {AstGenericType},
    read setGenerics: (self: AstStatDeclareExternType, generics: {AstGenericType}) -> AstStatDeclareExternType,
    read genericPacks: (self: AstStatDeclareExternType) -> {AstGenericTypePack},
    read setGenericPacks: (self: AstStatDeclareExternType, genericPacks: {AstGenericTypePack}) -> AstStatDeclareExternType,
}

export type AstStatClass = AstStatCommon & {
    read kind: "AstStatClass",
    read exported: (self: AstStatClass) -> boolean,
    read setExported: (self: AstStatClass, exported: boolean) -> AstStatClass,
    read name: (self: AstStatClass) -> AstLocal,
    read setName: (self: AstStatClass, name: AstLocal) -> AstStatClass,
    read members: (self: AstStatClass) -> {AstClassProperty | AstClassMethod},
    read setMembers: (self: AstStatClass, members: {AstClassProperty | AstClassMethod}) -> AstStatClass,
}

export type AstStatError = AstStatCommon & {
    read kind: "AstStatError",
    read messageIndex: (self: AstStatError) -> number,
    read expressions: (self: AstStatError) -> {AstExpr},
    read statements: (self: AstStatError) -> {AstStat},
}

export type AstStat =
    | AstStatBlock | AstStatIf | AstStatWhile | AstStatRepeat | AstStatBreak | AstStatContinue
    | AstStatReturn | AstStatExpr | AstStatLocal | AstStatFor | AstStatForIn | AstStatAssign
    | AstStatCompoundAssign | AstStatFunction | AstStatLocalFunction | AstStatTypeAlias
    | AstStatTypeFunction | AstStatDeclareGlobal | AstStatDeclareFunction | AstStatDeclareExternType
    | AstStatClass | AstStatError
    | (AstStatCommon & { read kind: string })

-- Expressions
export type AstExprGroup = AstExprCommon & {
    read kind: "AstExprGroup",
    read expr: (self: AstExprGroup) -> AstExpr,
    read setExpr: (self: AstExprGroup, expr: AstExpr) -> AstExprGroup,
}

export type AstExprConstantNil = AstExprCommon & { read kind: "AstExprConstantNil" }

export type AstExprConstantBool = AstExprCommon & {
    read kind: "AstExprConstantBool",
    read value: (self: AstExprConstantBool) -> boolean,
    read setValue: (self: AstExprConstantBool, value: boolean) -> AstExprConstantBool,
}

export type AstExprConstantNumber = AstExprCommon & {
    read kind: "AstExprConstantNumber",
    read value: (self: AstExprConstantNumber) -> number,
    read setValue: (self: AstExprConstantNumber, value: number) -> AstExprConstantNumber,
}

export type AstExprConstantInteger = AstExprCommon & {
    read kind: "AstExprConstantInteger",
    read value: (self: AstExprConstantInteger) -> number,
    read setValue: (self: AstExprConstantInteger, value: number) -> AstExprConstantInteger,
}

export type AstExprConstantString = AstExprCommon & {
    read kind: "AstExprConstantString",
    read value: (self: AstExprConstantString) -> string,
    read setValue: (self: AstExprConstantString, value: string) -> AstExprConstantString,
    read quoteStyle: (self: AstExprConstantString) -> ("single" | "double" | "raw" | "unquoted" | "simple"),
    read setQuoteStyle: (self: AstExprConstantString, quoteStyle: "single" | "double" | "raw" | "unquoted" | "simple") -> AstExprConstantString,
}

export type AstExprLocal = AstExprCommon & {
    read kind: "AstExprLocal",
    read upvalue: (self: AstExprLocal) -> boolean,
    read setUpvalue: (self: AstExprLocal, upvalue: boolean) -> AstExprLocal,
    read ["local"]: (self: AstExprLocal) -> AstLocal,
    read setLocal: (self: AstExprLocal, loc: AstLocal) -> AstExprLocal,
}

export type AstExprGlobal = AstExprCommon & {
    read kind: "AstExprGlobal",
    read name: (self: AstExprGlobal) -> string,
    read setName: (self: AstExprGlobal, name: string) -> AstExprGlobal,
}

export type AstExprVarargs = AstExprCommon & { read kind: "AstExprVarargs" }

export type AstExprCall = AstExprCommon & {
    read kind: "AstExprCall",
    -- True for method call syntax t:m(...)
    read self: (self: AstExprCall) -> boolean,
    read setSelf: (self: AstExprCall, self: boolean) -> AstExprCall,
    read func: (self: AstExprCall) -> AstExpr,
    read setFunc: (self: AstExprCall, func: AstExpr) -> AstExprCall,
    read args: (self: AstExprCall) -> {AstExpr},
    read setArgs: (self: AstExprCall, args: {AstExpr}) -> AstExprCall,
    read typeArguments: (self: AstExprCall) -> {AstNode},
    read setTypeArguments: (self: AstExprCall, typeArguments: {AstNode}) -> AstExprCall,
}

export type AstExprIndexName = AstExprCommon & {
    read kind: "AstExprIndexName",
    read index: (self: AstExprIndexName) -> string,
    read setIndex: (self: AstExprIndexName, index: string) -> AstExprIndexName,
    read op: (self: AstExprIndexName) -> string,
    read setOp: (self: AstExprIndexName, op: string) -> AstExprIndexName,
    read expr: (self: AstExprIndexName) -> AstExpr,
    read setExpr: (self: AstExprIndexName, expr: AstExpr) -> AstExprIndexName,
}

export type AstExprIndexExpr = AstExprCommon & {
    read kind: "AstExprIndexExpr",
    read expr: (self: AstExprIndexExpr) -> AstExpr,
    read setExpr: (self: AstExprIndexExpr, expr: AstExpr) -> AstExprIndexExpr,
    read index: (self: AstExprIndexExpr) -> AstExpr,
    read setIndex: (self: AstExprIndexExpr, index: AstExpr) -> AstExprIndexExpr,
}

export type AstExprFunction = AstExprCommon & {
    read kind: "AstExprFunction",
    read vararg: (self: AstExprFunction) -> boolean,
    read setVararg: (self: AstExprFunction, vararg: boolean) -> AstExprFunction,
    read debugname: (self: AstExprFunction) -> string,
    read setDebugName: (self: AstExprFunction, debugname: string) -> AstExprFunction,
    read args: (self: AstExprFunction) -> {AstLocal},
    read setArgs: (self: AstExprFunction, args: {AstLocal}) -> AstExprFunction,
    read body: (self: AstExprFunction) -> AstStatBlock,
    read setBody: (self: AstExprFunction, body: AstStatBlock) -> AstExprFunction,
    read generics: (self: AstExprFunction) -> {AstGenericType},
    read setGenerics: (self: AstExprFunction, generics: {AstGenericType}) -> AstExprFunction,
    read genericPacks: (self: AstExprFunction) -> {AstGenericTypePack},
    read setGenericPacks: (self: AstExprFunction, genericPacks: {AstGenericTypePack}) -> AstExprFunction,
    read returnAnnotation: (self: AstExprFunction) -> AstNode?,
    read setReturnAnnotation: (self: AstExprFunction, returnAnnotation: AstNode?) -> AstExprFunction,
    read attributes: (self: AstExprFunction) -> {AstAttr},
    read setAttributes: (self: AstExprFunction, attributes: {AstAttr}) -> AstExprFunction,
}

export type AstExprTable = AstExprCommon & {
    read kind: "AstExprTable",
    read items: (self: AstExprTable) -> {AstTableItem},
    read setItems: (self: AstExprTable, items: {AstTableItem}) -> AstExprTable,
}

export type AstExprUnary = AstExprCommon & {
    read kind: "AstExprUnary",
    read op: (self: AstExprUnary) -> ("not" | "-" | "#"),
    read setOp: (self: AstExprUnary, op: "not" | "-" | "#") -> AstExprUnary,
    read expr: (self: AstExprUnary) -> AstExpr,
    read setExpr: (self: AstExprUnary, expr: AstExpr) -> AstExprUnary,
}

export type AstExprBinary = AstExprCommon & {
    read kind: "AstExprBinary",
    read op: (self: AstExprBinary) -> string,
    read setOp: (self: AstExprBinary, op: string) -> AstExprBinary,
    read left: (self: AstExprBinary) -> AstExpr,
    read setLeft: (self: AstExprBinary, left: AstExpr) -> AstExprBinary,
    read right: (self: AstExprBinary) -> AstExpr,
    read setRight: (self: AstExprBinary, right: AstExpr) -> AstExprBinary,
}

export type AstExprTypeAssertion = AstExprCommon & {
    read kind: "AstExprTypeAssertion",
    read expr: (self: AstExprTypeAssertion) -> AstExpr,
    read setExpr: (self: AstExprTypeAssertion, expr: AstExpr) -> AstExprTypeAssertion,
    read annotation: (self: AstExprTypeAssertion) -> AstType,
    read setAnnotation: (self: AstExprTypeAssertion, annotation: AstType) -> AstExprTypeAssertion,
}

export type AstExprIfElse = AstExprCommon & {
    read kind: "AstExprIfElse",
    read hasElse: (self: AstExprIfElse) -> boolean,
    read setHasElse: (self: AstExprIfElse, hasElse: boolean) -> AstExprIfElse,
    read condition: (self: AstExprIfElse) -> AstExpr,
    read setCondition: (self: AstExprIfElse, condition: AstExpr) -> AstExprIfElse,
    read trueExpr: (self: AstExprIfElse) -> AstExpr,
    read setTrueExpr: (self: AstExprIfElse, trueExpr: AstExpr) -> AstExprIfElse,
    read falseExpr: (self: AstExprIfElse) -> AstExpr,
    read setFalseExpr: (self: AstExprIfElse, falseExpr: AstExpr) -> AstExprIfElse,
}

export type AstExprInterpString = AstExprCommon & {
    read kind: "AstExprInterpString",
    read strings: (self: AstExprInterpString) -> {string},
    read setStrings: (self: AstExprInterpString, strings: {string}) -> AstExprInterpString,
    read expressions: (self: AstExprInterpString) -> {AstExpr},
    read setExpressions: (self: AstExprInterpString, expressions: {AstExpr}) -> AstExprInterpString,
}

export type AstExprInstantiate = AstExprCommon & {
    read kind: "AstExprInstantiate",
    read expr: (self: AstExprInstantiate) -> AstExpr,
    read setExpr: (self: AstExprInstantiate, expr: AstExpr) -> AstExprInstantiate,
    read typeArguments: (self: AstExprInstantiate) -> {AstNode},
    read setTypeArguments: (self: AstExprInstantiate, typeArguments: {AstNode}) -> AstExprInstantiate,
}

export type AstExprError = AstExprCommon & {
    read kind: "AstExprError",
    read messageIndex: (self: AstExprError) -> number,
    read expressions: (self: AstExprError) -> {AstExpr},
}

export type AstExpr =
    | AstExprGroup | AstExprConstantNil | AstExprConstantBool | AstExprConstantNumber | AstExprConstantInteger
    | AstExprConstantString | AstExprLocal | AstExprGlobal | AstExprVarargs | AstExprCall
    | AstExprIndexName | AstExprIndexExpr | AstExprFunction | AstExprTable | AstExprUnary
    | AstExprBinary | AstExprTypeAssertion | AstExprIfElse | AstExprInterpString | AstExprInstantiate
    | AstExprError
    | (AstExprCommon & { read kind: string })

-- Types
export type AstTypeReference = AstTypeCommon & {
    read kind: "AstTypeReference",
    read name: (self: AstTypeReference) -> string,
    read setName: (self: AstTypeReference, name: string) -> AstTypeReference,
    read prefix: (self: AstTypeReference) -> string?,
    read setPrefix: (self: AstTypeReference, prefix: string?) -> AstTypeReference,
    read hasParameterList: (self: AstTypeReference) -> boolean,
    read setHasParameterList: (self: AstTypeReference, hasParameterList: boolean) -> AstTypeReference,
    read parameters: (self: AstTypeReference) -> {AstNode},
    read setParameters: (self: AstTypeReference, parameters: {AstNode}) -> AstTypeReference,
}

export type AstTypeTable = AstTypeCommon & {
    read kind: "AstTypeTable",
    read props: (self: AstTypeTable) -> {AstTableProp},
    read setProps: (self: AstTypeTable, props: {AstTableProp}) -> AstTypeTable,
    read indexer: (self: AstTypeTable) -> AstTableIndexer?,
    read setIndexer: (self: AstTypeTable, indexer: AstTableIndexer?) -> AstTypeTable,
}

export type AstTypeFunction = AstTypeCommon & {
    read kind: "AstTypeFunction",
    read generics: (self: AstTypeFunction) -> {AstGenericType},
    read setGenerics: (self: AstTypeFunction, generics: {AstGenericType}) -> AstTypeFunction,
    read genericPacks: (self: AstTypeFunction) -> {AstGenericTypePack},
    read setGenericPacks: (self: AstTypeFunction, genericPacks: {AstGenericTypePack}) -> AstTypeFunction,
    read argTypes: (self: AstTypeFunction) -> AstTypeList,
    read setArgTypes: (self: AstTypeFunction, argTypes: AstTypeList) -> AstTypeFunction,
    read returnTypes: (self: AstTypeFunction) -> AstTypePack,
    read setReturnTypes: (self: AstTypeFunction, returnTypes: AstTypePack) -> AstTypeFunction,
    read attributes: (self: AstTypeFunction) -> {AstAttr},
    read setAttributes: (self: AstTypeFunction, attributes: {AstAttr}) -> AstTypeFunction,
}

export type AstTypeTypeof = AstTypeCommon & {
    read kind: "AstTypeTypeof",
    read expr: (self: AstTypeTypeof) -> AstExpr,
    read setExpr: (self: AstTypeTypeof, expr: AstExpr) -> AstTypeTypeof,
}

export type AstTypeOptional = AstTypeCommon & { read kind: "AstTypeOptional" }

export type AstTypeUnion = AstTypeCommon & {
    read kind: "AstTypeUnion",
    read types: (self: AstTypeUnion) -> {AstType},
    read setTypes: (self: AstTypeUnion, types: {AstType}) -> AstTypeUnion,
}

export type AstTypeIntersection = AstTypeCommon & {
    read kind: "AstTypeIntersection",
    read types: (self: AstTypeIntersection) -> {AstType},
    read setTypes: (self: AstTypeIntersection, types: {AstType}) -> AstTypeIntersection,
}

export type AstTypeSingletonBool = AstTypeCommon & {
    read kind: "AstTypeSingletonBool",
    read value: (self: AstTypeSingletonBool) -> boolean,
    read setValue: (self: AstTypeSingletonBool, value: boolean) -> AstTypeSingletonBool,
}

export type AstTypeSingletonString = AstTypeCommon & {
    read kind: "AstTypeSingletonString",
    read value: (self: AstTypeSingletonString) -> string,
    read setValue: (self: AstTypeSingletonString, value: string) -> AstTypeSingletonString,
}

export type AstTypeGroup = AstTypeCommon & {
    read kind: "AstTypeGroup",
    read type: (self: AstTypeGroup) -> AstType,
    read setType: (self: AstTypeGroup, type: AstType) -> AstTypeGroup,
}

export type AstTypeError = AstTypeCommon & {
    read kind: "AstTypeError",
    read isMissing: (self: AstTypeError) -> boolean,
    read messageIndex: (self: AstTypeError) -> number,
    read types: (self: AstTypeError) -> {AstType},
}

export type AstType =
    | AstTypeReference | AstTypeTable | AstTypeFunction | AstTypeTypeof | AstTypeOptional
    | AstTypeUnion | AstTypeIntersection | AstTypeSingletonBool | AstTypeSingletonString
    | AstTypeGroup | AstTypeError
    | (AstTypeCommon & { read kind: string })

-- Type Packs
export type AstTypePackExplicit = AstTypePackCommon & {
    read kind: "AstTypePackExplicit",
    read typeList: (self: AstTypePackExplicit) -> AstTypeList,
    read setTypeList: (self: AstTypePackExplicit, typeList: AstTypeList) -> AstTypePackExplicit,
}

export type AstTypePackVariadic = AstTypePackCommon & {
    read kind: "AstTypePackVariadic",
    read variadicType: (self: AstTypePackVariadic) -> AstType,
    read setVariadicType: (self: AstTypePackVariadic, variadicType: AstType) -> AstTypePackVariadic,
}

export type AstTypePackGeneric = AstTypePackCommon & {
    read kind: "AstTypePackGeneric",
    read name: (self: AstTypePackGeneric) -> string,
    read setName: (self: AstTypePackGeneric, name: string) -> AstTypePackGeneric,
}

export type AstTypePack =
    | AstTypePackExplicit | AstTypePackVariadic | AstTypePackGeneric
    | (AstTypePackCommon & { read kind: string })

-- Generics & Attributes
export type AstGenericType = AstNodeCommon & {
    read kind: "AstGenericType",
    read category: "generic",
    read name: (self: AstGenericType) -> string,
    read setName: (self: AstGenericType, name: string) -> AstGenericType,
    read type: (self: AstGenericType) -> AstNode?,
    read setType: (self: AstGenericType, type: AstNode?) -> AstGenericType,
}

export type AstGenericTypePack = AstNodeCommon & {
    read kind: "AstGenericTypePack",
    read category: "generic",
    read name: (self: AstGenericTypePack) -> string,
    read setName: (self: AstGenericTypePack, name: string) -> AstGenericTypePack,
    read type: (self: AstGenericTypePack) -> AstNode?,
    read setType: (self: AstGenericTypePack, type: AstNode?) -> AstGenericTypePack,
}

export type AstAttr = AstNodeCommon & {
    read kind: "AstAttr",
    read category: "attr",
    read type: (self: AstAttr) -> ("checked" | "native" | "deprecated" | "debugNoinline" | "unknown"),
    read setType: (self: AstAttr, type: "checked" | "native" | "deprecated" | "debugNoinline" | "unknown") -> AstAttr,
    read name: (self: AstAttr) -> string,
    read setName: (self: AstAttr, name: string) -> AstAttr,
    read args: (self: AstAttr) -> {AstExpr},
    read setArgs: (self: AstAttr, args: {AstExpr}) -> AstAttr,
}

-- General AST Node Union
export type AstNode =
    | AstStat | AstExpr | AstType | AstTypePack | AstGenericType | AstGenericTypePack | AstAttr
    | (AstNodeCommon & {
        read kind: string,
        read category: "stat" | "expr" | "type" | "typePack" | "generic" | "attr" | "unknown",
    })

-- AST Traversal Filter
export type AstFilter = {
    read id: Id,
    read kind: "AstFilter",
    read matches: (self: AstFilter, node: AstNode) -> boolean,
}

export type AstAllocator = {
    read id: Id,
    read parse: (self: AstAllocator, source: string, includeCst: boolean?) -> AstDocument,
    read parseexpr: (self: AstAllocator, source: string, includeCst: boolean?) -> AstNode,
    read defaultnode: (self: AstAllocator, kind: string) -> AstNode | AstLocal | AstAux | CstNode,
    read properties: (self: AstAllocator) -> { [string]: any },
}

-- Parsed Document Root State
export type AstDocument = {
    read id: Id,
    read allocator: (self: AstDocument) -> AstAllocator,
    read root: (self: AstDocument) -> AstStatBlock?,
    read source: (self: AstDocument) -> string,
    read errors: (self: AstDocument) -> {ParseError},
    read comments: (self: AstDocument) -> {AstComment},
    read lineOffsets: (self: AstDocument) -> {number},
    read properties: (self: AstDocument) -> { [string]: any },
}

local function allocator(): AstAllocator
    error("Not implemented")
end

local function filter(...: string | { string } | AstFilter): AstFilter
    error("Not implemented")
end

return {
    allocator = allocator,
    filter = filter,
}
