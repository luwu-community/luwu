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

export type CstAttr = {
    read kind: "CstAttr",
    -- false when inside an attribute list, ie @[native checked]
    read hasAt: boolean,
}

export type CstParametrizedAttr = {
    read kind: "CstParametrizedAttr",
    -- for `@x(args)` form
    read openParenPosition: (self: CstParametrizedAttr) -> AstPosition?,
    read closeParenPosition: (self: CstParametrizedAttr) -> AstPosition?,
    -- Commas inside the `(a, b, c)` arg list
    read argsCommaPositions: (self: CstParametrizedAttr) -> {AstPosition},
}

export type CstExprGroup = {
    read kind: "CstExprGroup",
    read closePosition: (self: CstExprGroup) -> AstPosition?,
}

export type CstExprConstantNumber = {
    read kind: "CstExprConstantNumber",
    read value: (self: CstExprConstantNumber) -> string,
}

export type CstExprConstantInteger = {
    read kind: "CstExprConstantInteger",
    read value: (self: CstExprConstantInteger) -> string,
}

export type CstExprConstantString = {
    read kind: "CstExprConstantString",
    read quoteStyle: "single" | "double" | "raw" | "interp",
    read sourceString: (self: CstExprConstantString) -> string,
    read blockDepth: number,
}

export type CstExprCall = {
    read kind: "CstExprCall",
    read openParens: (self: CstExprCall) -> AstPosition?,
    read closeParens: (self: CstExprCall) -> AstPosition?,
    read commaPositions: (self: CstExprCall) -> {AstPosition},
}

export type CstExprIndexExpr = {
    read kind: "CstExprIndexExpr",
    read openBracketPosition: (self: CstExprIndexExpr) -> AstPosition?,
    read closeBracketPosition: (self: CstExprIndexExpr) -> AstPosition?,
}

export type CstExprFunction = {
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

export type CstExprTable = {
    read kind: "CstExprTable",
    read items: (self: CstExprTable) -> {CstTableItem},
}

export type CstExprOp = {
    read kind: "CstExprOp",
    read opPosition: (self: CstExprOp) -> AstPosition?,
}

export type CstExprTypeAssertion = {
    read kind: "CstExprTypeAssertion",
    read opPosition: (self: CstExprTypeAssertion) -> AstPosition?,
}

export type CstExprIfElse = {
    read kind: "CstExprIfElse",
    read thenPosition: (self: CstExprIfElse) -> AstPosition?,
    read elsePosition: (self: CstExprIfElse) -> AstPosition?,
    read isElseIf: boolean,
}

export type CstExprInterpString = {
    read kind: "CstExprInterpString",
    read commaPositions: (self: CstExprInterpString) -> {AstPosition},
}

export type CstStatDo = {
    read kind: "CstStatDo",
    read statsStartPosition: (self: CstStatDo) -> AstPosition?,
    read endPosition: (self: CstStatDo) -> AstPosition?,
}

export type CstStatRepeat = {
    read kind: "CstStatRepeat",
    read untilPosition: (self: CstStatRepeat) -> AstPosition?,
}

export type CstStatReturn = {
    read kind: "CstStatReturn",
    read commaPositions: (self: CstStatReturn) -> {AstPosition},
}

export type CstStatLocal = {
    read kind: "CstStatLocal",
    read varsAnnotationColonPositions: (self: CstStatLocal) -> {AstPosition},
    read varsCommaPositions: (self: CstStatLocal) -> {AstPosition},
    read valuesCommaPositions: (self: CstStatLocal) -> {AstPosition},
}

export type CstStatFor = {
    read kind: "CstStatFor",
    read annotationColonPosition: (self: CstStatFor) -> AstPosition?,
    read equalsPosition: (self: CstStatFor) -> AstPosition?,
    read endCommaPosition: (self: CstStatFor) -> AstPosition?,
    read stepCommaPosition: (self: CstStatFor) -> AstPosition?,
}

export type CstStatForIn = {
    read kind: "CstStatForIn",
    read varsAnnotationColonPositions: (self: CstStatForIn) -> {AstPosition},
    read varsCommaPositions: (self: CstStatForIn) -> {AstPosition},
    read valuesCommaPositions: (self: CstStatForIn) -> {AstPosition},
}

export type CstStatAssign = {
    read kind: "CstStatAssign",
    read varsCommaPositions: (self: CstStatAssign) -> {AstPosition},
    read equalsPosition: (self: CstStatAssign) -> AstPosition?,
    read valuesCommaPositions: (self: CstStatAssign) -> {AstPosition},
}

export type CstStatCompoundAssign = {
    read kind: "CstStatCompoundAssign",
    read opPosition: (self: CstStatCompoundAssign) -> AstPosition?,
}

export type CstStatFunction = {
    read kind: "CstStatFunction",
    read functionKeywordPosition: (self: CstStatFunction) -> AstPosition?,
}

export type CstStatLocalFunction = {
    read kind: "CstStatLocalFunction",
    read localKeywordPosition: (self: CstStatLocalFunction) -> AstPosition?,
    read functionKeywordPosition: (self: CstStatLocalFunction) -> AstPosition?,
}

export type CstGenericType = {
    read kind: "CstGenericType",
    read defaultEqualsPosition: (self: CstGenericType) -> AstPosition?,
}

export type CstGenericTypePack = {
    read kind: "CstGenericTypePack",
    read ellipsisPosition: (self: CstGenericTypePack) -> AstPosition?,
    read defaultEqualsPosition: (self: CstGenericTypePack) -> AstPosition?,
}

export type CstStatTypeAlias = {
    read kind: "CstStatTypeAlias",
    read typeKeywordPosition: (self: CstStatTypeAlias) -> AstPosition?,
    read genericsOpenPosition: (self: CstStatTypeAlias) -> AstPosition?,
    read genericsCommaPositions: (self: CstStatTypeAlias) -> {AstPosition},
    read genericsClosePosition: (self: CstStatTypeAlias) -> AstPosition?,
    read equalsPosition: (self: CstStatTypeAlias) -> AstPosition?,
}

export type CstStatTypeFunction = {
    read kind: "CstStatTypeFunction",
    read typeKeywordPosition: (self: CstStatTypeFunction) -> AstPosition?,
    read functionKeywordPosition: (self: CstStatTypeFunction) -> AstPosition?,
}

export type CstTypeReference = {
    read kind: "CstTypeReference",
    read prefixPointPosition: (self: CstTypeReference) -> AstPosition?,
    read openParametersPosition: (self: CstTypeReference) -> AstPosition?,
    read parametersCommaPositions: (self: CstTypeReference) -> {AstPosition},
    read closeParametersPosition: (self: CstTypeReference) -> AstPosition?,
}

export type CstTypeTable = {
    read kind: "CstTypeTable",
    read isArray: boolean,
}

export type CstTypeFunction = {
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

export type CstTypeTypeof = {
    read kind: "CstTypeTypeof",
    read openPosition: (self: CstTypeTypeof) -> AstPosition?,
    read closePosition: (self: CstTypeTypeof) -> AstPosition?,
}

export type CstTypeUnion = {
    read kind: "CstTypeUnion",
    read leadingPosition: (self: CstTypeUnion) -> AstPosition?,
    read separatorPositions: (self: CstTypeUnion) -> {AstPosition},
}

export type CstTypeIntersection = {
    read kind: "CstTypeIntersection",
    read leadingPosition: (self: CstTypeIntersection) -> AstPosition?,
    read separatorPositions: (self: CstTypeIntersection) -> {AstPosition},
}

export type CstTypeSingletonString = {
    read kind: "CstTypeSingletonString",
    read sourceString: (self: CstTypeSingletonString) -> string,
    read blockDepth: number,
}

export type CstTypeGroup = {
    read kind: "CstTypeGroup",
    read closePosition: (self: CstTypeGroup) -> AstPosition?,
}

export type CstTypePackExplicit = {
    read kind: "CstTypePackExplicit",
    read openParenthesesPosition: (self: CstTypePackExplicit) -> AstPosition?,
    read closeParenthesesPosition: (self: CstTypePackExplicit) -> AstPosition?,
    read commaPositions: (self: CstTypePackExplicit) -> {AstPosition},
}

export type CstTypePackGeneric = {
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

-- Common AST node methods and fields
export type AstNodeCommon = {
    read text: string,
    read location: (self: AstNode) -> AstLocation,
    read cst: (self: AstNode) -> CstNode?,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?) -> (),
    read find: (self: AstNode, kind: string) -> {AstNode},
}

export type AstStatCommon = AstNodeCommon & {
    read category: "stat",
    read hasSemicolon: boolean,
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
    read hasEnd: boolean,
    read body: (self: AstNode) -> {AstStat},
}

export type AstStatIf = AstStatCommon & {
    read kind: "AstStatIf",
    read condition: (self: AstNode) -> AstExpr,
    read thenbody: (self: AstNode) -> AstStatBlock,
    -- Active for 'elseif' as well
    read elsebody: (self: AstNode) -> AstStat?,
}

export type AstStatWhile = AstStatCommon & {
    read kind: "AstStatWhile",
    read hasDo: boolean,
    read condition: (self: AstNode) -> AstExpr,
    read body: (self: AstNode) -> AstStatBlock,
}

export type AstStatRepeat = AstStatCommon & {
    read kind: "AstStatRepeat",
    read condition: (self: AstNode) -> AstExpr,
    read body: (self: AstNode) -> AstStatBlock,
}

export type AstStatBreak = AstStatCommon & {
    read kind: "AstStatBreak",
}

export type AstStatContinue = AstStatCommon & {
    read kind: "AstStatContinue",
}

export type AstStatReturn = AstStatCommon & {
    read kind: "AstStatReturn",
    read list: (self: AstNode) -> {AstExpr},
}

export type AstStatExpr = AstStatCommon & {
    read kind: "AstStatExpr",
    read expr: (self: AstNode) -> AstExpr,
}

export type AstStatLocal = AstStatCommon & {
    read kind: "AstStatLocal",
    read isConst: boolean,
    read exported: boolean,
    read vars: (self: AstNode) -> {AstLocal},
    read values: (self: AstNode) -> {AstExpr},
}

export type AstStatFor = AstStatCommon & {
    read kind: "AstStatFor",
    read hasDo: boolean,
    read var: (self: AstNode) -> AstLocal,
    read from: (self: AstNode) -> AstExpr,
    read to: (self: AstNode) -> AstExpr,
    read step: (self: AstNode) -> AstExpr?,
    read body: (self: AstNode) -> AstStatBlock,
}

export type AstStatForIn = AstStatCommon & {
    read kind: "AstStatForIn",
    read hasIn: boolean,
    read hasDo: boolean,
    read vars: (self: AstNode) -> {AstLocal},
    read values: (self: AstNode) -> {AstExpr},
    read body: (self: AstNode) -> AstStatBlock,
}

export type AstStatAssign = AstStatCommon & {
    read kind: "AstStatAssign",
    read vars: (self: AstNode) -> {AstExpr},
    read values: (self: AstNode) -> {AstExpr},
}

export type AstStatCompoundAssign = AstStatCommon & {
    read kind: "AstStatCompoundAssign",
    read op: string,
    read var: (self: AstNode) -> AstExpr,
    read value: (self: AstNode) -> AstExpr,
}

export type AstStatFunction = AstStatCommon & {
    read kind: "AstStatFunction",
    read name: (self: AstNode) -> AstExpr,
    read func: (self: AstNode) -> AstExprFunction,
}

export type AstStatLocalFunction = AstStatCommon & {
    read kind: "AstStatLocalFunction",
    read isConst: boolean,
    read name: (self: AstNode) -> AstLocal,
    read func: (self: AstNode) -> AstExprFunction,
}

export type AstStatTypeAlias = AstStatCommon & {
    read kind: "AstStatTypeAlias",
    read name: string,
    read exported: boolean,
    read type: (self: AstNode) -> AstType,
    read generics: (self: AstNode) -> {AstGenericType},
    read genericPacks: (self: AstNode) -> {AstGenericTypePack},
}

export type AstStatTypeFunction = AstStatCommon & {
    read kind: "AstStatTypeFunction",
    read name: string,
    read exported: boolean,
    read hasErrors: boolean,
    read body: (self: AstNode) -> AstExprFunction,
}

export type AstStatDeclareGlobal = AstStatCommon & {
    read kind: "AstStatDeclareGlobal",
    read name: string,
    read type: (self: AstNode) -> AstType,
}

export type AstStatDeclareFunction = AstStatCommon & {
    read kind: "AstStatDeclareFunction",
    read name: string,
    read vararg: boolean,
    read generics: (self: AstNode) -> {AstGenericType},
    read genericPacks: (self: AstNode) -> {AstGenericTypePack},
    read params: (self: AstNode) -> {AstType},
    read returnTypes: (self: AstNode) -> AstTypePack?,
    read attributes: (self: AstNode) -> {AstAttr},
}

export type AstStatDeclareExternType = AstStatCommon & {
    read kind: "AstStatDeclareExternType",
    read name: string,
    read superName: string?,
    read props: (self: AstNode) -> {AstDeclaredExternTypeProperty},
    read indexer: (self: AstNode) -> AstTableIndexer?,
    read generics: (self: AstNode) -> {AstGenericType},
    read genericPacks: (self: AstNode) -> {AstGenericTypePack},
}

export type AstStatClass = AstStatCommon & {
    read kind: "AstStatClass",
    read exported: boolean,
    read name: (self: AstNode) -> AstLocal,
    read members: (self: AstNode) -> {AstClassProperty | AstClassMethod},
}

export type AstStatError = AstStatCommon & {
    read kind: "AstStatError",
    read messageIndex: number,
    read expressions: (self: AstNode) -> {AstExpr},
    read statements: (self: AstNode) -> {AstStat},
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
    read expr: (self: AstNode) -> AstExpr,
}

export type AstExprConstantNil = AstExprCommon & {
    read kind: "AstExprConstantNil",
}

export type AstExprConstantBool = AstExprCommon & {
    read kind: "AstExprConstantBool",
    read value: boolean,
}

export type AstExprConstantNumber = AstExprCommon & {
    read kind: "AstExprConstantNumber",
    read value: number,
}

export type AstExprConstantInteger = AstExprCommon & {
    read kind: "AstExprConstantInteger",
    read value: number,
}

export type AstExprConstantString = AstExprCommon & {
    read kind: "AstExprConstantString",
    read value: string,
    read quoteStyle: "single" | "double" | "raw" | "unquoted" | "simple",
}

export type AstExprLocal = AstExprCommon & {
    read kind: "AstExprLocal",
    read upvalue: boolean,
    read ["local"]: (self: AstNode) -> AstLocal,
}

export type AstExprGlobal = AstExprCommon & {
    read kind: "AstExprGlobal",
    read name: string,
}

export type AstExprVarargs = AstExprCommon & {
    read kind: "AstExprVarargs",
}

export type AstExprCall = AstExprCommon & {
    read kind: "AstExprCall",
    -- True for method call syntax t:m(...)
    read self: boolean,
    read func: (self: AstNode) -> AstExpr,
    read args: (self: AstNode) -> {AstExpr},
    read typeArguments: (self: AstNode) -> {AstNode},
}

export type AstExprIndexName = AstExprCommon & {
    read kind: "AstExprIndexName",
    read index: string,
    read op: string,
    read expr: (self: AstNode) -> AstExpr,
}

export type AstExprIndexExpr = AstExprCommon & {
    read kind: "AstExprIndexExpr",
    read expr: (self: AstNode) -> AstExpr,
    read index: (self: AstNode) -> AstExpr,
}

export type AstExprFunction = AstExprCommon & {
    read kind: "AstExprFunction",
    read vararg: boolean,
    read debugname: string,
    read args: (self: AstNode) -> {AstLocal},
    read body: (self: AstNode) -> AstStatBlock,
    read generics: (self: AstNode) -> {AstGenericType},
    read genericPacks: (self: AstNode) -> {AstGenericTypePack},
    read returnAnnotation: (self: AstNode) -> AstNode?,
    read attributes: (self: AstNode) -> {AstAttr},
}

export type AstExprTable = AstExprCommon & {
    read kind: "AstExprTable",
    read items: (self: AstNode) -> {AstTableItem},
}

export type AstExprUnary = AstExprCommon & {
    read kind: "AstExprUnary",
    read op: string,
    read expr: (self: AstNode) -> AstExpr,
}

export type AstExprBinary = AstExprCommon & {
    read kind: "AstExprBinary",
    read op: string,
    read left: (self: AstNode) -> AstExpr,
    read right: (self: AstNode) -> AstExpr,
}

export type AstExprTypeAssertion = AstExprCommon & {
    read kind: "AstExprTypeAssertion",
    read expr: (self: AstNode) -> AstExpr,
    read annotation: (self: AstNode) -> AstType,
}

export type AstExprIfElse = AstExprCommon & {
    read kind: "AstExprIfElse",
    read hasElse: boolean,
    read condition: (self: AstNode) -> AstExpr,
    read trueExpr: (self: AstNode) -> AstExpr,
    read falseExpr: (self: AstNode) -> AstExpr,
}

export type AstExprInterpString = AstExprCommon & {
    read kind: "AstExprInterpString",
    read strings: {string},
    read expressions: (self: AstNode) -> {AstExpr},
}

export type AstExprInstantiate = AstExprCommon & {
    read kind: "AstExprInstantiate",
    read expr: (self: AstNode) -> AstExpr,
    read typeArguments: (self: AstNode) -> {AstNode},
}

export type AstExprError = AstExprCommon & {
    read kind: "AstExprError",
    read messageIndex: number,
    read expressions: (self: AstNode) -> {AstExpr},
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
    read name: string,
    read prefix: string?,
    read hasParameterList: boolean,
    read parameters: (self: AstNode) -> {AstNode},
}

export type AstTypeTable = AstTypeCommon & {
    read kind: "AstTypeTable",
    read props: (self: AstNode) -> {AstTableProp},
    read indexer: (self: AstNode) -> AstTableIndexer?,
}

export type AstTypeFunction = AstTypeCommon & {
    read kind: "AstTypeFunction",
    read generics: (self: AstNode) -> {AstGenericType},
    read genericPacks: (self: AstNode) -> {AstGenericTypePack},
    read argTypes: (self: AstNode) -> {AstType},
    read returnTypes: (self: AstNode) -> AstTypePack,
    read attributes: (self: AstNode) -> {AstAttr},
}

export type AstTypeTypeof = AstTypeCommon & {
    read kind: "AstTypeTypeof",
    read expr: (self: AstNode) -> AstExpr,
}

export type AstTypeOptional = AstTypeCommon & {
    read kind: "AstTypeOptional",
}

export type AstTypeUnion = AstTypeCommon & {
    read kind: "AstTypeUnion",
    read types: (self: AstNode) -> {AstType},
}

export type AstTypeIntersection = AstTypeCommon & {
    read kind: "AstTypeIntersection",
    read types: (self: AstNode) -> {AstType},
}

export type AstTypeSingletonBool = AstTypeCommon & {
    read kind: "AstTypeSingletonBool",
    read value: boolean,
}

export type AstTypeSingletonString = AstTypeCommon & {
    read kind: "AstTypeSingletonString",
    read value: string,
}

export type AstTypeGroup = AstTypeCommon & {
    read kind: "AstTypeGroup",
    read type: (self: AstNode) -> AstType,
}

export type AstTypeError = AstTypeCommon & {
    read kind: "AstTypeError",
    read isMissing: boolean,
    read messageIndex: number,
    read types: (self: AstNode) -> {AstType},
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
    read types: (self: AstNode) -> {AstType},
    read tailType: (self: AstNode) -> AstTypePack?,
}

export type AstTypePackVariadic = AstTypePackCommon & {
    read kind: "AstTypePackVariadic",
    read variadicType: (self: AstNode) -> AstType,
}

export type AstTypePackGeneric = AstTypePackCommon & {
    read kind: "AstTypePackGeneric",
    read name: string,
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
    read name: string,
    read type: (self: AstNode) -> AstNode?,
}

export type AstGenericTypePack = AstNodeCommon & {
    read kind: "AstGenericTypePack",
    read category: "generic",
    read name: string,
    read type: (self: AstNode) -> AstNode?,
}

export type AstAttr = AstNodeCommon & {
    read kind: "AstAttr",
    read category: "attr",
    read type: "checked" | "native" | "deprecated" | "debugNoinline" | "unknown",
    read name: string,
    read args: (self: AstNode) -> {AstExpr},
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
    read root: AstStatBlock?,
    read source: string,
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
