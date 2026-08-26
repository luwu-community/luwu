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
    -- Comment, BlockComment, or BrokenComment
    read type: "single" | "block" | "broken" | "unknown",
    read location: AstLocation,
    read text: string,
}

-- local variable definition/binding
export type AstLocal = {
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

local stat = doc.root.body[1]          -- AstStatLocal
local tableExpr = stat.values[1]       -- AstExprTable
local items = tableExpr.items          -- { AstTableItem }
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

export type CstAttr = {
    read kind: "CstAttr",
    -- false when inside an attribute list, ie @[native checked]
    read hasAt: boolean,
}

export type CstParametrizedAttr = {
    read kind: "CstParametrizedAttr",
    -- for `@x(args)` form
    read openParenPosition: AstPosition?,
    read closeParenPosition: AstPosition?,
    -- Commas inside the `(a, b, c)` arg list
    read argsCommaPositions: {AstPosition},
}

export type CstExprGroup = {
    read kind: "CstExprGroup",
    read closePosition: AstPosition?,
}

export type CstExprConstantNumber = {
    read kind: "CstExprConstantNumber",
    read value: string,
}

export type CstExprConstantInteger = {
    read kind: "CstExprConstantInteger",
    read value: string,
}

export type CstExprConstantString = {
    read kind: "CstExprConstantString",
    read quoteStyle: "single" | "double" | "raw" | "interp",
    read sourceString: string,
    read blockDepth: number,
}

export type CstExprCall = {
    read kind: "CstExprCall",
    read openParens: AstPosition?,
    read closeParens: AstPosition?,
    read commaPositions: {AstPosition},
}

export type CstExprIndexExpr = {
    read kind: "CstExprIndexExpr",
    read openBracketPosition: AstPosition?,
    read closeBracketPosition: AstPosition?,
}

export type CstExprFunction = {
    read kind: "CstExprFunction",
    read functionKeywordPosition: AstPosition?,
    read openGenericsPosition: AstPosition?,
    read genericsCommaPositions: {AstPosition},
    read closeGenericsPosition: AstPosition?,
    read argsAnnotationColonPositions: {AstPosition},
    read argsCommaPositions: {AstPosition},
    read varargAnnotationColonPosition: AstPosition?,
    read returnSpecifierPosition: AstPosition?,
}

export type CstExprTable = {
    read kind: "CstExprTable",
    read items: {CstTableItem},
}

export type CstExprOp = {
    read kind: "CstExprOp",
    read opPosition: AstPosition?,
}

export type CstExprTypeAssertion = {
    read kind: "CstExprTypeAssertion",
    read opPosition: AstPosition?,
}

export type CstExprIfElse = {
    read kind: "CstExprIfElse",
    read thenPosition: AstPosition?,
    read elsePosition: AstPosition?,
    read isElseIf: boolean,
}

export type CstExprInterpString = {
    read kind: "CstExprInterpString",
    read commaPositions: {AstPosition},
}

export type CstStatDo = {
    read kind: "CstStatDo",
    read statsStartPosition: AstPosition?,
    read endPosition: AstPosition?,
}

export type CstStatRepeat = {
    read kind: "CstStatRepeat",
    read untilPosition: AstPosition?,
}

export type CstStatReturn = {
    read kind: "CstStatReturn",
    read commaPositions: {AstPosition},
}

export type CstStatLocal = {
    read kind: "CstStatLocal",
    read varsAnnotationColonPositions: {AstPosition},
    read varsCommaPositions: {AstPosition},
    read valuesCommaPositions: {AstPosition},
}

export type CstStatFor = {
    read kind: "CstStatFor",
    read annotationColonPosition: AstPosition?,
    read equalsPosition: AstPosition?,
    read endCommaPosition: AstPosition?,
    read stepCommaPosition: AstPosition?,
}

export type CstStatForIn = {
    read kind: "CstStatForIn",
    read varsAnnotationColonPositions: {AstPosition},
    read varsCommaPositions: {AstPosition},
    read valuesCommaPositions: {AstPosition},
}

export type CstStatAssign = {
    read kind: "CstStatAssign",
    read varsCommaPositions: {AstPosition},
    read equalsPosition: AstPosition?,
    read valuesCommaPositions: {AstPosition},
}

export type CstStatCompoundAssign = {
    read kind: "CstStatCompoundAssign",
    read opPosition: AstPosition?,
}

export type CstStatFunction = {
    read kind: "CstStatFunction",
    read functionKeywordPosition: AstPosition?,
}

export type CstStatLocalFunction = {
    read kind: "CstStatLocalFunction",
    read localKeywordPosition: AstPosition?,
    read functionKeywordPosition: AstPosition?,
}

export type CstGenericType = {
    read kind: "CstGenericType",
    read defaultEqualsPosition: AstPosition?,
}

export type CstGenericTypePack = {
    read kind: "CstGenericTypePack",
    read ellipsisPosition: AstPosition?,
    read defaultEqualsPosition: AstPosition?,
}

export type CstStatTypeAlias = {
    read kind: "CstStatTypeAlias",
    read typeKeywordPosition: AstPosition?,
    read genericsOpenPosition: AstPosition?,
    read genericsCommaPositions: {AstPosition},
    read genericsClosePosition: AstPosition?,
    read equalsPosition: AstPosition?,
}

export type CstStatTypeFunction = {
    read kind: "CstStatTypeFunction",
    read typeKeywordPosition: AstPosition?,
    read functionKeywordPosition: AstPosition?,
}

export type CstTypeReference = {
    read kind: "CstTypeReference",
    read prefixPointPosition: AstPosition?,
    read openParametersPosition: AstPosition?,
    read parametersCommaPositions: {AstPosition},
    read closeParametersPosition: AstPosition?,
}

export type CstTypeTable = {
    read kind: "CstTypeTable",
    read isArray: boolean,
}

export type CstTypeFunction = {
    read kind: "CstTypeFunction",
    read openGenericsPosition: AstPosition?,
    read genericsCommaPositions: {AstPosition},
    read closeGenericsPosition: AstPosition?,
    read openArgsPosition: AstPosition?,
    read argumentNameColonPositions: {AstPosition},
    read argumentsCommaPositions: {AstPosition},
    read closeArgsPosition: AstPosition?,
    read returnArrowPosition: AstPosition?,
}

export type CstTypeTypeof = {
    read kind: "CstTypeTypeof",
    read openPosition: AstPosition?,
    read closePosition: AstPosition?,
}

export type CstTypeUnion = {
    read kind: "CstTypeUnion",
    read leadingPosition: AstPosition?,
    read separatorPositions: {AstPosition},
}

export type CstTypeIntersection = {
    read kind: "CstTypeIntersection",
    read leadingPosition: AstPosition?,
    read separatorPositions: {AstPosition},
}

export type CstTypeSingletonString = {
    read kind: "CstTypeSingletonString",
    read sourceString: string,
    read blockDepth: number,
}

export type CstTypeGroup = {
    read kind: "CstTypeGroup",
    read closePosition: AstPosition?,
}

export type CstTypePackExplicit = {
    read kind: "CstTypePackExplicit",
    read openParenthesesPosition: AstPosition?,
    read closeParenthesesPosition: AstPosition?,
    read commaPositions: {AstPosition},
}

export type CstTypePackGeneric = {
    read kind: "CstTypePackGeneric",
    read ellipsisPosition: AstPosition?,
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
    | { read kind: string }

-- Statements
export type AstStatBlock = {
    read kind: "AstStatBlock",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read body: {AstStat},
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
    read hasEnd: boolean,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatIf = {
    read kind: "AstStatIf",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read condition: AstExpr,
    read thenbody: AstStatBlock,
    -- Active for 'elseif' as well
    read elsebody: AstStat?,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatWhile = {
    read kind: "AstStatWhile",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read condition: AstExpr,
    read body: AstStatBlock,
    read hasDo: boolean,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatRepeat = {
    read kind: "AstStatRepeat",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read condition: AstExpr,
    read body: AstStatBlock,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatBreak = {
    read kind: "AstStatBreak",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatContinue = {
    read kind: "AstStatContinue",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatReturn = {
    read kind: "AstStatReturn",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read list: {AstExpr},
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatExpr = {
    read kind: "AstStatExpr",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read expr: AstExpr,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatLocal = {
    read kind: "AstStatLocal",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read vars: {AstLocal},
    read values: {AstExpr},
    read isConst: boolean,
    read exported: boolean,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatFor = {
    read kind: "AstStatFor",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read var: AstLocal,
    read from: AstExpr,
    read to: AstExpr,
    read step: AstExpr?,
    read body: AstStatBlock,
    read hasDo: boolean,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatForIn = {
    read kind: "AstStatForIn",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read vars: {AstLocal},
    read values: {AstExpr},
    read body: AstStatBlock,
    read hasIn: boolean,
    read hasDo: boolean,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatAssign = {
    read kind: "AstStatAssign",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read vars: {AstExpr},
    read values: {AstExpr},
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatCompoundAssign = {
    read kind: "AstStatCompoundAssign",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read var: AstExpr,
    read op: string,
    read value: AstExpr,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatFunction = {
    read kind: "AstStatFunction",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read name: AstExpr,
    read func: AstExprFunction,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatLocalFunction = {
    read kind: "AstStatLocalFunction",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read name: AstLocal,
    read func: AstExprFunction,
    read isConst: boolean,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatTypeAlias = {
    read kind: "AstStatTypeAlias",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read name: string,
    read type: AstType,
    read generics: {AstGenericType},
    read genericPacks: {AstGenericTypePack},
    read exported: boolean,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatTypeFunction = {
    read kind: "AstStatTypeFunction",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read name: string,
    read body: AstExprFunction,
    read exported: boolean,
    read hasErrors: boolean,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatDeclareGlobal = {
    read kind: "AstStatDeclareGlobal",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read name: string,
    read type: AstType,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatDeclareFunction = {
    read kind: "AstStatDeclareFunction",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read name: string,
    read generics: {AstGenericType},
    read genericPacks: {AstGenericTypePack},
    read params: {AstType},
    read vararg: boolean,
    read returnTypes: AstTypePack?,
    read attributes: {AstAttr},
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatDeclareExternType = {
    read kind: "AstStatDeclareExternType",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read name: string,
    read superName: string?,
    read props: {AstDeclaredExternTypeProperty},
    read indexer: AstTableIndexer?,
    read generics: {AstGenericType},
    read genericPacks: {AstGenericTypePack},
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatClass = {
    read kind: "AstStatClass",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read name: AstLocal,
    read members: {AstClassProperty | AstClassMethod},
    read exported: boolean,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstStatError = {
    read kind: "AstStatError",
    read category: "stat",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read hasSemicolon: boolean,
    read expressions: {AstExpr},
    read statements: {AstStat},
    read messageIndex: number,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
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
    | { read kind: string, read category: "stat", read location: AstLocation, read text: string, read cst: CstNode?, read hasSemicolon: boolean, read children: (self: AstNode) -> {AstNode}, read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> () }

-- Expressions
export type AstExprGroup = {
    read kind: "AstExprGroup",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read expr: AstExpr,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprConstantNil = {
    read kind: "AstExprConstantNil",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprConstantBool = {
    read kind: "AstExprConstantBool",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read value: boolean,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprConstantNumber = {
    read kind: "AstExprConstantNumber",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read value: number,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprConstantInteger = {
    read kind: "AstExprConstantInteger",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read value: number,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprConstantString = {
    read kind: "AstExprConstantString",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read value: string,
    read quoteStyle: "single" | "double" | "raw" | "unquoted" | "simple",
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprLocal = {
    read kind: "AstExprLocal",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read ["local"]: AstLocal,
    read upvalue: boolean,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprGlobal = {
    read kind: "AstExprGlobal",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read name: string,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprVarargs = {
    read kind: "AstExprVarargs",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprCall = {
    read kind: "AstExprCall",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read func: AstExpr,
    read args: {AstExpr},
    -- True for method call syntax t:m(...)
    read self: boolean,
    read typeArguments: {AstNode},
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprIndexName = {
    read kind: "AstExprIndexName",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read expr: AstExpr,
    read index: string,
    read op: string,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprIndexExpr = {
    read kind: "AstExprIndexExpr",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read expr: AstExpr,
    read index: AstExpr,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprFunction = {
    read kind: "AstExprFunction",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read args: {AstLocal},
    read body: AstStatBlock,
    read vararg: boolean,
    read generics: {AstGenericType},
    read genericPacks: {AstGenericTypePack},
    read returnAnnotation: AstNode?,
    read debugname: string,
    read attributes: {AstAttr},
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprTable = {
    read kind: "AstExprTable",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read items: {AstTableItem},
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprUnary = {
    read kind: "AstExprUnary",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read op: string,
    read expr: AstExpr,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprBinary = {
    read kind: "AstExprBinary",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read op: string,
    read left: AstExpr,
    read right: AstExpr,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprTypeAssertion = {
    read kind: "AstExprTypeAssertion",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read expr: AstExpr,
    read annotation: AstType,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprIfElse = {
    read kind: "AstExprIfElse",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read condition: AstExpr,
    read trueExpr: AstExpr,
    read falseExpr: AstExpr,
    read hasElse: boolean,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprInterpString = {
    read kind: "AstExprInterpString",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read strings: {string},
    read expressions: {AstExpr},
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprInstantiate = {
    read kind: "AstExprInstantiate",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read expr: AstExpr,
    read typeArguments: {AstNode},
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstExprError = {
    read kind: "AstExprError",
    read category: "expr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read expressions: {AstExpr},
    read messageIndex: number,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
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
    | { read kind: string, read category: "expr", read location: AstLocation, read text: string, read cst: CstNode?, read children: (self: AstNode) -> {AstNode}, read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> () }

-- Types
export type AstTypeReference = {
    read kind: "AstTypeReference",
    read category: "type",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read name: string,
    read prefix: string?,
    read hasParameterList: boolean,
    read parameters: {AstNode},
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstTypeTable = {
    read kind: "AstTypeTable",
    read category: "type",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read props: {AstTableProp},
    read indexer: AstTableIndexer?,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstTypeFunction = {
    read kind: "AstTypeFunction",
    read category: "type",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read generics: {AstGenericType},
    read genericPacks: {AstGenericTypePack},
    read argTypes: {AstType},
    read returnTypes: AstTypePack,
    read attributes: {AstAttr},
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstTypeTypeof = {
    read kind: "AstTypeTypeof",
    read category: "type",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read expr: AstExpr,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstTypeOptional = {
    read kind: "AstTypeOptional",
    read category: "type",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstTypeUnion = {
    read kind: "AstTypeUnion",
    read category: "type",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read types: {AstType},
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstTypeIntersection = {
    read kind: "AstTypeIntersection",
    read category: "type",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read types: {AstType},
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstTypeSingletonBool = {
    read kind: "AstTypeSingletonBool",
    read category: "type",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read value: boolean,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstTypeSingletonString = {
    read kind: "AstTypeSingletonString",
    read category: "type",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read value: string,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstTypeGroup = {
    read kind: "AstTypeGroup",
    read category: "type",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read type: AstType,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstTypeError = {
    read kind: "AstTypeError",
    read category: "type",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read types: {AstType},
    read isMissing: boolean,
    read messageIndex: number,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
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
    | { read kind: string, read category: "type", read location: AstLocation, read text: string, read cst: CstNode?, read children: (self: AstNode) -> {AstNode}, read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> () }

-- Type Packs
export type AstTypePackExplicit = {
    read kind: "AstTypePackExplicit",
    read category: "typePack",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read types: {AstType},
    read tailType: AstTypePack?,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstTypePackVariadic = {
    read kind: "AstTypePackVariadic",
    read category: "typePack",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read variadicType: AstType,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstTypePackGeneric = {
    read kind: "AstTypePackGeneric",
    read category: "typePack",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read name: string,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstTypePack =
    | AstTypePackExplicit
    | AstTypePackVariadic
    | AstTypePackGeneric
    | { read kind: string, read category: "typePack", read location: AstLocation, read text: string, read cst: CstNode?, read children: (self: AstNode) -> {AstNode}, read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> () }

-- Generics & Attributes
export type AstGenericType = {
    read kind: "AstGenericType",
    read category: "generic",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read name: string,
    read type: AstNode?,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstGenericTypePack = {
    read kind: "AstGenericTypePack",
    read category: "generic",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read name: string,
    read type: AstNode?,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
}

export type AstAttr = {
    read kind: "AstAttr",
    read category: "attr",
    read location: AstLocation,
    read text: string,
    read cst: CstNode?,
    read type: "checked" | "native" | "deprecated" | "debugNoinline" | "unknown",
    read name: string,
    read args: {AstExpr},
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
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
    | {
        read kind: string,
        read category: "stat" | "expr" | "type" | "typePack" | "generic" | "attr" | "unknown",
        read location: AstLocation,
        read text: string,
        read cst: CstNode?,
        read children: (self: AstNode) -> {AstNode},
        read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
    }

-- Parsed Document Root State
export type AstDocument = {
    read root: AstStatBlock?,
    read source: string,
    read errors: {ParseError},
    read comments: {AstComment},
    read lineOffsets: {number},
    read walk: (self: AstDocument, visitor: (node: AstNode) -> boolean?) -> (),
    read find: (self: AstDocument, kind: string) -> {AstNode},
}

-- Module Surface
export type ReflectModule = {
    parse: (source: string) -> AstDocument,
    parseExpr: (source: string) -> AstNode,
}

return {} :: ReflectModule
