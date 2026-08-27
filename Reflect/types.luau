--!strict
-- This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details

-- Source Position (1-indexed line and column)
export type AstPosition = {
    read line: number,
    read column: number,
    read computedOffset: number,
}

-- Luau::Location (1-indexed begin and end lines/columns)
export type AstLocation = {
    read beginLine: number,
    read beginColumn: number,
    read endLine: number,
    read endColumn: number,
    read startOffset: number,
    read endOffset: number,
    read text: string,
}

-- comment block
export type AstComment = {
    read kind: "AstComment",
    -- Comment, BlockComment, or BrokenComment
    read type: "single" | "block" | "broken" | "unknown",
    read location: AstLocation,
    read text: string,
}

-- local variable definition/binding
export type AstLocal = {
    read kind: "AstLocal",
    read name: string,
    read location: AstLocation,
    -- Shadowed local variable if this declaration shadows a variable from an outer scope
    read shadow: AstLocal?,
    read isConst: boolean,
    read depth: number,
    read annotation: AstNode?,
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

local stat = doc.root:body()[1]        -- AstStatLocal
local tableExpr = stat:values()[1]     -- AstExprTable
local items = tableExpr:items()        -- { AstTableItem }
```
]=]
export type AstTableItem = {
    read kind: "list" | "record" | "general",
    -- can be nullptr! (foo: nullptr for List, foo=bar: AstExprConstantString for Record, [foo]=bar: AstExpr for General)
    read key: AstNode?,
    read value: AstNode,
}

-- Auxiliary AST structures
export type AstTableProp = {
    read kind: "AstTableProp",
    read name: string,
    read location: AstLocation,
    read type: AstType,
    read access: "read" | "write" | "readwrite",
}

export type AstTableIndexer = {
    read kind: "AstTableIndexer",
    read indexType: AstType,
    read resultType: AstType,
    read location: AstLocation,
    read access: "read" | "write" | "readwrite",
}

export type AstDeclaredExternTypeProperty = {
    read kind: "AstDeclaredExternTypeProperty",
    read name: string,
    read location: AstLocation,
    read type: AstType,
    read isMethod: boolean,
    read access: "read" | "write" | "readwrite",
}

export type AstClassProperty = {
    read kind: "AstClassProperty",
    read name: string,
    read type: AstType?,
}

export type AstClassMethod = {
    read kind: "AstClassMethod",
    read name: string,
    read func: AstExprFunction,
}

export type AstAux =
    | AstTableProp
    | AstTableIndexer
    | AstDeclaredExternTypeProperty
    | AstClassProperty
    | AstClassMethod
    | AstLocal
    | AstComment
    | { read kind: string }

-- CST for tableitem
export type CstTableItem = {
    -- '[', only if Kind == General
    read indexerOpenPosition: AstPosition?,
    -- ']', only if Kind == General
    read indexerClosePosition: AstPosition?,
    -- only if Kind != List
    read equalsPosition: AstPosition?,
    read separatorPosition: AstPosition?,
    -- may be missing for last Item
    read separator: "comma" | "semicolon" | "missing",
}

export type CstNodeCommon = {
    read category: "generic",
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

export type CstExprGroup = CstNodeCommon & {
    read kind: "CstExprGroup",
    read closePosition: (self: CstExprGroup) -> AstPosition?,
}

export type CstExprConstantNumber = CstNodeCommon & {
    read kind: "CstExprConstantNumber",
    read value: (self: CstExprConstantNumber) -> string,
}

export type CstExprConstantInteger = CstNodeCommon & {
    read kind: "CstExprConstantInteger",
    read value: (self: CstExprConstantInteger) -> string,
}

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

export type CstExprTable = CstNodeCommon & {
    read kind: "CstExprTable",
    read items: (self: CstExprTable) -> {CstTableItem},
}

export type CstExprOp = CstNodeCommon & {
    read kind: "CstExprOp",
    read opPosition: (self: CstExprOp) -> AstPosition?,
}

export type CstExprTypeAssertion = CstNodeCommon & {
    read kind: "CstExprTypeAssertion",
    read opPosition: (self: CstExprTypeAssertion) -> AstPosition?,
}

export type CstExprIfElse = CstNodeCommon & {
    read kind: "CstExprIfElse",
    read thenPosition: (self: CstExprIfElse) -> AstPosition?,
    read elsePosition: (self: CstExprIfElse) -> AstPosition?,
    read isElseIf: (self: CstExprIfElse) -> boolean,
}

export type CstExprInterpString = CstNodeCommon & {
    read kind: "CstExprInterpString",
    read commaPositions: (self: CstExprInterpString) -> {AstPosition},
}

export type CstExprExplicitTypeInstantiation = CstNodeCommon & {
    read kind: "CstExprExplicitTypeInstantiation",
}

export type CstStatDo = CstNodeCommon & {
    read kind: "CstStatDo",
    read statsStartPosition: (self: CstStatDo) -> AstPosition?,
    read endPosition: (self: CstStatDo) -> AstPosition?,
}

export type CstStatRepeat = CstNodeCommon & {
    read kind: "CstStatRepeat",
    read untilPosition: (self: CstStatRepeat) -> AstPosition?,
}

export type CstStatReturn = CstNodeCommon & {
    read kind: "CstStatReturn",
    read commaPositions: (self: CstStatReturn) -> {AstPosition},
}

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

export type CstStatCompoundAssign = CstNodeCommon & {
    read kind: "CstStatCompoundAssign",
    read opPosition: (self: CstStatCompoundAssign) -> AstPosition?,
}

export type CstStatFunction = CstNodeCommon & {
    read kind: "CstStatFunction",
    read functionKeywordPosition: (self: CstStatFunction) -> AstPosition?,
}

export type CstStatLocalFunction = CstNodeCommon & {
    read kind: "CstStatLocalFunction",
    read localKeywordPosition: (self: CstStatLocalFunction) -> AstPosition?,
    read functionKeywordPosition: (self: CstStatLocalFunction) -> AstPosition?,
}

export type CstGenericType = CstNodeCommon & {
    read kind: "CstGenericType",
    read defaultEqualsPosition: (self: CstGenericType) -> AstPosition?,
}

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

export type CstTypeTable = CstNodeCommon & {
    read kind: "CstTypeTable",
    read isArray: (self: CstTypeTable) -> boolean,
}

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

export type CstTypeGroup = CstNodeCommon & {
    read kind: "CstTypeGroup",
    read closePosition: (self: CstTypeGroup) -> AstPosition?,
}

export type CstTypePackExplicit = CstNodeCommon & {
    read kind: "CstTypePackExplicit",
    read openParenthesesPosition: (self: CstTypePackExplicit) -> AstPosition?,
    read closeParenthesesPosition: (self: CstTypePackExplicit) -> AstPosition?,
    read commaPositions: (self: CstTypePackExplicit) -> {AstPosition},
}

export type CstTypePackGeneric = CstNodeCommon & {
    read kind: "CstTypePackGeneric",
    read ellipsisPosition: (self: CstTypePackGeneric) -> AstPosition?,
}

export type CstNode =
    | CstAttr
    | CstParametrizedAttr
    | CstExprGroup
    | CstExprConstantNumber
    | CstExprConstantInteger
    | CstExprConstantString
    | CstExprCall
    | CstExprIndexExpr
    | CstExprFunction
    | CstExprTable
    | CstExprOp
    | CstExprTypeAssertion
    | CstExprIfElse
    | CstExprInterpString
    | CstExprExplicitTypeInstantiation
    | CstStatDo
    | CstStatRepeat
    | CstStatReturn
    | CstStatLocal
    | CstStatFor
    | CstStatForIn
    | CstStatAssign
    | CstStatCompoundAssign
    | CstStatFunction
    | CstStatLocalFunction
    | CstGenericType
    | CstGenericTypePack
    | CstStatTypeAlias
    | CstStatTypeFunction
    | CstTypeReference
    | CstTypeTable
    | CstTypeFunction
    | CstTypeTypeof
    | CstTypeUnion
    | CstTypeIntersection
    | CstTypeSingletonString
    | CstTypeGroup
    | CstTypePackExplicit
    | CstTypePackGeneric
    | (CstNodeCommon & { read kind: string })

-- Common AST node methods and fields
export type AstNodeCommon = {
    read text: (self: AstNode) -> string,
    read location: (self: AstNode) -> AstLocation,
    read cst: (self: AstNode) -> CstNode?,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
    read find: (self: AstNode, kind: string) -> {AstNode},
}

export type AstStatCommon = AstNodeCommon & {
    read category: "stat",
    read hasSemicolon: (self: AstNode) -> boolean,
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
    read body: (self: AstStatBlock) -> {AstStat},
}

export type AstStatIf = AstStatCommon & {
    read kind: "AstStatIf",
    read condition: (self: AstStatIf) -> AstExpr,
    read thenbody: (self: AstStatIf) -> AstStatBlock,
    -- Active for 'elseif' as well
    read elsebody: (self: AstStatIf) -> AstStat?,
}

export type AstStatWhile = AstStatCommon & {
    read kind: "AstStatWhile",
    read hasDo: (self: AstStatWhile) -> boolean,
    read condition: (self: AstStatWhile) -> AstExpr,
    read body: (self: AstStatWhile) -> AstStatBlock,
}

export type AstStatRepeat = AstStatCommon & {
    read kind: "AstStatRepeat",
    read condition: (self: AstStatRepeat) -> AstExpr,
    read body: (self: AstStatRepeat) -> AstStatBlock,
}

export type AstStatBreak = AstStatCommon & {
    read kind: "AstStatBreak",
}

export type AstStatContinue = AstStatCommon & {
    read kind: "AstStatContinue",
}

export type AstStatReturn = AstStatCommon & {
    read kind: "AstStatReturn",
    read list: (self: AstStatReturn) -> {AstExpr},
}

export type AstStatExpr = AstStatCommon & {
    read kind: "AstStatExpr",
    read expr: (self: AstStatExpr) -> AstExpr,
}

export type AstStatLocal = AstStatCommon & {
    read kind: "AstStatLocal",
    read isConst: (self: AstStatLocal) -> boolean,
    read exported: (self: AstStatLocal) -> boolean,
    read vars: (self: AstStatLocal) -> {AstLocal},
    read values: (self: AstStatLocal) -> {AstExpr},
}

export type AstStatFor = AstStatCommon & {
    read kind: "AstStatFor",
    read hasDo: (self: AstStatFor) -> boolean,
    read var: (self: AstStatFor) -> AstLocal,
    read from: (self: AstStatFor) -> AstExpr,
    read to: (self: AstStatFor) -> AstExpr,
    read step: (self: AstStatFor) -> AstExpr?,
    read body: (self: AstStatFor) -> AstStatBlock,
}

export type AstStatForIn = AstStatCommon & {
    read kind: "AstStatForIn",
    read hasIn: (self: AstStatForIn) -> boolean,
    read hasDo: (self: AstStatForIn) -> boolean,
    read vars: (self: AstStatForIn) -> {AstLocal},
    read values: (self: AstStatForIn) -> {AstExpr},
    read body: (self: AstStatForIn) -> AstStatBlock,
}

export type AstStatAssign = AstStatCommon & {
    read kind: "AstStatAssign",
    read vars: (self: AstStatAssign) -> {AstExpr},
    read values: (self: AstStatAssign) -> {AstExpr},
}

export type AstStatCompoundAssign = AstStatCommon & {
    read kind: "AstStatCompoundAssign",
    read op: (self: AstStatCompoundAssign) -> string,
    read var: (self: AstStatCompoundAssign) -> AstExpr,
    read value: (self: AstStatCompoundAssign) -> AstExpr,
}

export type AstStatFunction = AstStatCommon & {
    read kind: "AstStatFunction",
    read name: (self: AstStatFunction) -> AstExpr,
    read func: (self: AstStatFunction) -> AstExprFunction,
}

export type AstStatLocalFunction = AstStatCommon & {
    read kind: "AstStatLocalFunction",
    read isConst: (self: AstStatLocalFunction) -> boolean,
    read name: (self: AstStatLocalFunction) -> AstLocal,
    read func: (self: AstStatLocalFunction) -> AstExprFunction,
}

export type AstStatTypeAlias = AstStatCommon & {
    read kind: "AstStatTypeAlias",
    read name: (self: AstStatTypeAlias) -> string,
    read exported: (self: AstStatTypeAlias) -> boolean,
    read type: (self: AstStatTypeAlias) -> AstType,
    read generics: (self: AstStatTypeAlias) -> {AstGenericType},
    read genericPacks: (self: AstStatTypeAlias) -> {AstGenericTypePack},
}

export type AstStatTypeFunction = AstStatCommon & {
    read kind: "AstStatTypeFunction",
    read name: (self: AstStatTypeFunction) -> string,
    read exported: (self: AstStatTypeFunction) -> boolean,
    read hasErrors: (self: AstStatTypeFunction) -> boolean,
    read body: (self: AstStatTypeFunction) -> AstExprFunction,
}

export type AstStatDeclareGlobal = AstStatCommon & {
    read kind: "AstStatDeclareGlobal",
    read name: (self: AstStatDeclareGlobal) -> string,
    read type: (self: AstStatDeclareGlobal) -> AstType,
}

export type AstStatDeclareFunction = AstStatCommon & {
    read kind: "AstStatDeclareFunction",
    read name: (self: AstStatDeclareFunction) -> string,
    read vararg: (self: AstStatDeclareFunction) -> boolean,
    read generics: (self: AstStatDeclareFunction) -> {AstGenericType},
    read genericPacks: (self: AstStatDeclareFunction) -> {AstGenericTypePack},
    read params: (self: AstStatDeclareFunction) -> {AstType},
    read returnTypes: (self: AstStatDeclareFunction) -> AstTypePack?,
    read attributes: (self: AstStatDeclareFunction) -> {AstAttr},
}

export type AstStatDeclareExternType = AstStatCommon & {
    read kind: "AstStatDeclareExternType",
    read name: (self: AstStatDeclareExternType) -> string,
    read superName: (self: AstStatDeclareExternType) -> string?,
    read props: (self: AstStatDeclareExternType) -> {AstDeclaredExternTypeProperty},
    read indexer: (self: AstStatDeclareExternType) -> AstTableIndexer?,
    read generics: (self: AstStatDeclareExternType) -> {AstGenericType},
    read genericPacks: (self: AstStatDeclareExternType) -> {AstGenericTypePack},
}

export type AstStatClass = AstStatCommon & {
    read kind: "AstStatClass",
    read exported: (self: AstStatClass) -> boolean,
    read name: (self: AstStatClass) -> AstLocal,
    read members: (self: AstStatClass) -> {AstClassProperty | AstClassMethod},
}

export type AstStatError = AstStatCommon & {
    read kind: "AstStatError",
    read messageIndex: (self: AstStatError) -> number,
    read expressions: (self: AstStatError) -> {AstExpr},
    read statements: (self: AstStatError) -> {AstStat},
}

export type AstStat =
    | AstStatBlock
    | AstStatIf
    | AstStatWhile
    | AstStatRepeat
    | AstStatBreak
    | AstStatContinue
    | AstStatReturn
    | AstStatExpr
    | AstStatLocal
    | AstStatFor
    | AstStatForIn
    | AstStatAssign
    | AstStatCompoundAssign
    | AstStatFunction
    | AstStatLocalFunction
    | AstStatTypeAlias
    | AstStatTypeFunction
    | AstStatDeclareGlobal
    | AstStatDeclareFunction
    | AstStatDeclareExternType
    | AstStatClass
    | AstStatError
    | (AstStatCommon & { read kind: string })

-- Expressions
export type AstExprGroup = AstExprCommon & {
    read kind: "AstExprGroup",
    read expr: (self: AstExprGroup) -> AstExpr,
}

export type AstExprConstantNil = AstExprCommon & {
    read kind: "AstExprConstantNil",
}

export type AstExprConstantBool = AstExprCommon & {
    read kind: "AstExprConstantBool",
    read value: (self: AstExprConstantBool) -> boolean,
}

export type AstExprConstantNumber = AstExprCommon & {
    read kind: "AstExprConstantNumber",
    read value: (self: AstExprConstantNumber) -> number,
}

export type AstExprConstantInteger = AstExprCommon & {
    read kind: "AstExprConstantInteger",
    read value: (self: AstExprConstantInteger) -> number,
}

export type AstExprConstantString = AstExprCommon & {
    read kind: "AstExprConstantString",
    read value: (self: AstExprConstantString) -> string,
    read quoteStyle: (self: AstExprConstantString) -> ("single" | "double" | "raw" | "unquoted" | "simple"),
}

export type AstExprLocal = AstExprCommon & {
    read kind: "AstExprLocal",
    read upvalue: (self: AstExprLocal) -> boolean,
    read ["local"]: (self: AstExprLocal) -> AstLocal,
}

export type AstExprGlobal = AstExprCommon & {
    read kind: "AstExprGlobal",
    read name: (self: AstExprGlobal) -> string,
}

export type AstExprVarargs = AstExprCommon & {
    read kind: "AstExprVarargs",
}

export type AstExprCall = AstExprCommon & {
    read kind: "AstExprCall",
    -- True for method call syntax t:m(...)
    read self: (self: AstExprCall) -> boolean,
    read func: (self: AstExprCall) -> AstExpr,
    read args: (self: AstExprCall) -> {AstExpr},
    read typeArguments: (self: AstExprCall) -> {AstNode},
}

export type AstExprIndexName = AstExprCommon & {
    read kind: "AstExprIndexName",
    read index: (self: AstExprIndexName) -> string,
    read op: (self: AstExprIndexName) -> string,
    read expr: (self: AstExprIndexName) -> AstExpr,
}

export type AstExprIndexExpr = AstExprCommon & {
    read kind: "AstExprIndexExpr",
    read expr: (self: AstExprIndexExpr) -> AstExpr,
    read index: (self: AstExprIndexExpr) -> AstExpr,
}

export type AstExprFunction = AstExprCommon & {
    read kind: "AstExprFunction",
    read vararg: (self: AstExprFunction) -> boolean,
    read debugname: (self: AstExprFunction) -> string,
    read args: (self: AstExprFunction) -> {AstLocal},
    read body: (self: AstExprFunction) -> AstStatBlock,
    read generics: (self: AstExprFunction) -> {AstGenericType},
    read genericPacks: (self: AstExprFunction) -> {AstGenericTypePack},
    read returnAnnotation: (self: AstExprFunction) -> AstNode?,
    read attributes: (self: AstExprFunction) -> {AstAttr},
}

export type AstExprTable = AstExprCommon & {
    read kind: "AstExprTable",
    read items: (self: AstExprTable) -> {AstTableItem},
}

export type AstExprUnary = AstExprCommon & {
    read kind: "AstExprUnary",
    read op: (self: AstExprUnary) -> ("not" | "-" | "#"),
    read expr: (self: AstExprUnary) -> AstExpr,
}

export type AstExprBinary = AstExprCommon & {
    read kind: "AstExprBinary",
    read op: (self: AstExprBinary) -> string,
    read left: (self: AstExprBinary) -> AstExpr,
    read right: (self: AstExprBinary) -> AstExpr,
}

export type AstExprTypeAssertion = AstExprCommon & {
    read kind: "AstExprTypeAssertion",
    read expr: (self: AstExprTypeAssertion) -> AstExpr,
    read annotation: (self: AstExprTypeAssertion) -> AstType,
}

export type AstExprIfElse = AstExprCommon & {
    read kind: "AstExprIfElse",
    read hasElse: (self: AstExprIfElse) -> boolean,
    read condition: (self: AstExprIfElse) -> AstExpr,
    read trueExpr: (self: AstExprIfElse) -> AstExpr,
    read falseExpr: (self: AstExprIfElse) -> AstExpr,
}

export type AstExprInterpString = AstExprCommon & {
    read kind: "AstExprInterpString",
    read strings: (self: AstExprInterpString) -> {string},
    read expressions: (self: AstExprInterpString) -> {AstExpr},
}

export type AstExprInstantiate = AstExprCommon & {
    read kind: "AstExprInstantiate",
    read expr: (self: AstExprInstantiate) -> AstExpr,
    read typeArguments: (self: AstExprInstantiate) -> {AstNode},
}

export type AstExprError = AstExprCommon & {
    read kind: "AstExprError",
    read messageIndex: (self: AstExprError) -> number,
    read expressions: (self: AstExprError) -> {AstExpr},
}

export type AstExpr =
    | AstExprGroup
    | AstExprConstantNil
    | AstExprConstantBool
    | AstExprConstantNumber
    | AstExprConstantInteger
    | AstExprConstantString
    | AstExprLocal
    | AstExprGlobal
    | AstExprVarargs
    | AstExprCall
    | AstExprIndexName
    | AstExprIndexExpr
    | AstExprFunction
    | AstExprTable
    | AstExprUnary
    | AstExprBinary
    | AstExprTypeAssertion
    | AstExprIfElse
    | AstExprInterpString
    | AstExprInstantiate
    | AstExprError
    | (AstExprCommon & { read kind: string })

-- Types
export type AstTypeReference = AstTypeCommon & {
    read kind: "AstTypeReference",
    read name: (self: AstTypeReference) -> string,
    read prefix: (self: AstTypeReference) -> string?,
    read hasParameterList: (self: AstTypeReference) -> boolean,
    read parameters: (self: AstTypeReference) -> {AstNode},
}

export type AstTypeTable = AstTypeCommon & {
    read kind: "AstTypeTable",
    read props: (self: AstTypeTable) -> {AstTableProp},
    read indexer: (self: AstTypeTable) -> AstTableIndexer?,
}

export type AstTypeFunction = AstTypeCommon & {
    read kind: "AstTypeFunction",
    read generics: (self: AstTypeFunction) -> {AstGenericType},
    read genericPacks: (self: AstTypeFunction) -> {AstGenericTypePack},
    read argTypes: (self: AstTypeFunction) -> {AstType},
    read returnTypes: (self: AstTypeFunction) -> AstTypePack,
    read attributes: (self: AstTypeFunction) -> {AstAttr},
}

export type AstTypeTypeof = AstTypeCommon & {
    read kind: "AstTypeTypeof",
    read expr: (self: AstTypeTypeof) -> AstExpr,
}

export type AstTypeOptional = AstTypeCommon & {
    read kind: "AstTypeOptional",
}

export type AstTypeUnion = AstTypeCommon & {
    read kind: "AstTypeUnion",
    read types: (self: AstTypeUnion) -> {AstType},
}

export type AstTypeIntersection = AstTypeCommon & {
    read kind: "AstTypeIntersection",
    read types: (self: AstTypeIntersection) -> {AstType},
}

export type AstTypeSingletonBool = AstTypeCommon & {
    read kind: "AstTypeSingletonBool",
    read value: (self: AstTypeSingletonBool) -> boolean,
}

export type AstTypeSingletonString = AstTypeCommon & {
    read kind: "AstTypeSingletonString",
    read value: (self: AstTypeSingletonString) -> string,
}

export type AstTypeGroup = AstTypeCommon & {
    read kind: "AstTypeGroup",
    read type: (self: AstTypeGroup) -> AstType,
}

export type AstTypeError = AstTypeCommon & {
    read kind: "AstTypeError",
    read isMissing: (self: AstTypeError) -> boolean,
    read messageIndex: (self: AstTypeError) -> number,
    read types: (self: AstTypeError) -> {AstType},
}

export type AstType =
    | AstTypeReference
    | AstTypeTable
    | AstTypeFunction
    | AstTypeTypeof
    | AstTypeOptional
    | AstTypeUnion
    | AstTypeIntersection
    | AstTypeSingletonBool
    | AstTypeSingletonString
    | AstTypeGroup
    | AstTypeError
    | (AstTypeCommon & { read kind: string })

-- Type Packs
export type AstTypePackExplicit = AstTypePackCommon & {
    read kind: "AstTypePackExplicit",
    read types: (self: AstTypePackExplicit) -> {AstType},
    read tailType: (self: AstTypePackExplicit) -> AstTypePack?,
}

export type AstTypePackVariadic = AstTypePackCommon & {
    read kind: "AstTypePackVariadic",
    read variadicType: (self: AstTypePackVariadic) -> AstType,
}

export type AstTypePackGeneric = AstTypePackCommon & {
    read kind: "AstTypePackGeneric",
    read name: (self: AstTypePackGeneric) -> string,
}

export type AstTypePack =
    | AstTypePackExplicit
    | AstTypePackVariadic
    | AstTypePackGeneric
    | (AstTypePackCommon & { read kind: string })

-- Generics & Attributes
export type AstGenericType = AstNodeCommon & {
    read kind: "AstGenericType",
    read category: "generic",
    read name: (self: AstGenericType) -> string,
    read type: (self: AstGenericType) -> AstNode?,
}

export type AstGenericTypePack = AstNodeCommon & {
    read kind: "AstGenericTypePack",
    read category: "generic",
    read name: (self: AstGenericTypePack) -> string,
    read type: (self: AstGenericTypePack) -> AstNode?,
}

export type AstAttr = AstNodeCommon & {
    read kind: "AstAttr",
    read category: "attr",
    read type: (self: AstAttr) -> ("checked" | "native" | "deprecated" | "debugNoinline" | "unknown"),
    read name: (self: AstAttr) -> string,
    read args: (self: AstAttr) -> {AstExpr},
}

-- General AST Node Union
export type AstNode =
    | AstStat
    | AstExpr
    | AstType
    | AstTypePack
    | AstGenericType
    | AstGenericTypePack
    | AstAttr
    | (AstNodeCommon & {
        read kind: string,
        read category: "stat" | "expr" | "type" | "typePack" | "generic" | "attr" | "unknown",
    })

-- Parsed Document Root State
export type AstDocument = {
    read root: (self: AstDocument) -> AstStatBlock?,
    read source: (self: AstDocument) -> string,
    read errors: (self: AstDocument) -> {ParseError},
    read comments: (self: AstDocument) -> {AstComment},
    read lineOffsets: (self: AstDocument) -> {number},
    read walk: (self: AstDocument, visitor: (node: AstNode) -> boolean?) -> (),
    read find: (self: AstDocument, kind: string) -> {AstNode},
}

local function parse(source: string): AstDocument
    error("Not implemented")
end

local function parseExpr(source: string): AstNode
    error("Not implemented")
end

return {
    parse = parse,
    parseExpr = parseExpr,
}
