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
    read category: "aux",
    -- Comment, BlockComment, or BrokenComment
    read type: (self: AstComment) -> ("single" | "block" | "broken" | "unknown"),
    read setType: (self: AstComment, type: "single" | "block" | "broken") -> AstComment,
    read origlocation: (self: AstComment) -> AstLocation,
    read text: (self: AstComment) -> string,
    read setText: (self: AstComment, text: string) -> AstComment,
    read properties: (self: AstComment) -> { [string]: any },
}

-- local variable definition/binding
export type AstLocal = {
    read id: Id,
    read kind: "AstLocal",
    read category: "local",
    read name: (self: AstLocal) -> string,
    read setName: (self: AstLocal, name: string) -> AstLocal,
    read origlocation: (self: AstLocal) -> AstLocation,
    -- Shadowed local variable if this declaration shadows a variable from an outer scope
    read shadow: (self: AstLocal) -> AstLocal?,
    read setShadow: (self: AstLocal, shadow: AstLocal?) -> AstLocal,
    read isConst: (self: AstLocal) -> boolean,
    read setIsConst: (self: AstLocal, isConst: boolean) -> AstLocal,
    read exported: (self: AstLocal) -> boolean,
    read setExported: (self: AstLocal, exported: boolean) -> AstLocal,
    read depth: (self: AstLocal) -> integer,
    read setDepth: (self: AstLocal, depth: number | integer) -> AstLocal,
    read annotation: (self: AstLocal) -> AstNode?,
    read setAnnotation: (self: AstLocal, annotation: AstNode?) -> AstLocal,
    read loopDepth: (self: AstLocal) -> integer,
    read setLoopDepth: (self: AstLocal, loopDepth: number | integer) -> AstLocal,
    read properties: (self: AstLocal) -> { [string]: any },
}

-- parser errored
export type ParseError = {
    read message: string,
    read location: AstLocation,
}

export type HotComment = {
    read header: boolean,
    read content: string,
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
    read kind: "AstTableItem",
    read category: "aux",
    read itemKind: (self: AstTableItem) -> ("list" | "record" | "general"),
    read setItemKind: (self: AstTableItem, itemKind: "list" | "record" | "general") -> AstTableItem,
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
    read category: "aux",
    read name: (self: AstTableProp) -> string,
    read setName: (self: AstTableProp, name: string) -> AstTableProp,
    read origlocation: (self: AstTableProp) -> AstLocation,
    read type: (self: AstTableProp) -> AstType,
    read setType: (self: AstTableProp, type: AstType) -> AstTableProp,
    read access: (self: AstTableProp) -> ("read" | "write" | "readwrite"),
    read setAccess: (self: AstTableProp, access: "read" | "write" | "readwrite") -> AstTableProp,
    read accessLocation: (self: AstTableProp) -> AstLocation?,
    read properties: (self: AstTableProp) -> { [string]: any },
}

export type AstTableIndexer = {
    read id: Id,
    read kind: "AstTableIndexer",
    read category: "aux",
    read indexType: (self: AstTableIndexer) -> AstType,
    read setIndexType: (self: AstTableIndexer, indexType: AstType) -> AstTableIndexer,
    read resultType: (self: AstTableIndexer) -> AstType,
    read setResultType: (self: AstTableIndexer, resultType: AstType) -> AstTableIndexer,
    read origlocation: (self: AstTableIndexer) -> AstLocation,
    read access: (self: AstTableIndexer) -> ("read" | "write" | "readwrite"),
    read setAccess: (self: AstTableIndexer, access: "read" | "write" | "readwrite") -> AstTableIndexer,
    read accessLocation: (self: AstTableIndexer) -> AstLocation?,
    read properties: (self: AstTableIndexer) -> { [string]: any },
}

export type AstDeclaredExternTypeProperty = {
    read id: Id,
    read kind: "AstDeclaredExternTypeProperty",
    read category: "aux",
    read name: (self: AstDeclaredExternTypeProperty) -> string,
    read setName: (self: AstDeclaredExternTypeProperty, name: string) -> AstDeclaredExternTypeProperty,
    read origlocation: (self: AstDeclaredExternTypeProperty) -> AstLocation,
    read type: (self: AstDeclaredExternTypeProperty) -> AstType,
    read setType: (self: AstDeclaredExternTypeProperty, type: AstType) -> AstDeclaredExternTypeProperty,
    read isMethod: (self: AstDeclaredExternTypeProperty) -> boolean,
    read setIsMethod: (self: AstDeclaredExternTypeProperty, isMethod: boolean) -> AstDeclaredExternTypeProperty,
    read access: (self: AstDeclaredExternTypeProperty) -> ("read" | "write" | "readwrite"),
    read setAccess: (self: AstDeclaredExternTypeProperty, access: "read" | "write" | "readwrite") -> AstDeclaredExternTypeProperty,
    read nameLocation: (self: AstDeclaredExternTypeProperty) -> AstLocation,
    read properties: (self: AstDeclaredExternTypeProperty) -> { [string]: any },
}

export type AstClassProperty = {
    read id: Id,
    read kind: "AstClassProperty",
    read category: "aux",
    read name: (self: AstClassProperty) -> string,
    read setName: (self: AstClassProperty, name: string) -> AstClassProperty,
    read type: (self: AstClassProperty) -> AstType?,
    read setType: (self: AstClassProperty, type: AstType?) -> AstClassProperty,
    read visibility: (self: AstClassProperty) -> ("public" | "private"),
    read setVisibility: (self: AstClassProperty, visibility: "public" | "private") -> AstClassProperty,
    read isConst: (self: AstClassProperty) -> boolean,
    read setIsConst: (self: AstClassProperty, isConst: boolean) -> AstClassProperty,
    read defaultValue: (self: AstClassProperty) -> AstExpr?,
    read setDefaultValue: (self: AstClassProperty, defaultValue: AstExpr?) -> AstClassProperty,
    read hasSemicolon: (self: AstClassProperty) -> boolean,
    read setHasSemicolon: (self: AstClassProperty, hasSemicolon: boolean) -> AstClassProperty,
    read qualifierLocation: (self: AstClassProperty) -> AstLocation?,
    read nameLocation: (self: AstClassProperty) -> AstLocation,
    read typeColonLocation: (self: AstClassProperty) -> AstLocation?,
    read constLocation: (self: AstClassProperty) -> AstLocation?,
    read equalsLocation: (self: AstClassProperty) -> AstLocation?,
    read properties: (self: AstClassProperty) -> { [string]: any },
}

export type AstClassMethod = {
    read id: Id,
    read kind: "AstClassMethod",
    read category: "aux",
    read name: (self: AstClassMethod) -> string,
    read setName: (self: AstClassMethod, name: string) -> AstClassMethod,
    read func: (self: AstClassMethod) -> AstExprFunction,
    read setFunc: (self: AstClassMethod, func: AstExprFunction) -> AstClassMethod,
    read visibility: (self: AstClassMethod) -> ("public" | "private"),
    read setVisibility: (self: AstClassMethod, visibility: "public" | "private") -> AstClassMethod,
    read hasSemicolon: (self: AstClassMethod) -> boolean,
    read setHasSemicolon: (self: AstClassMethod, hasSemicolon: boolean) -> AstClassMethod,
    read qualifierLocation: (self: AstClassMethod) -> AstLocation?,
    read keywordLocation: (self: AstClassMethod) -> AstLocation,
    read nameLocation: (self: AstClassMethod) -> AstLocation,
    read properties: (self: AstClassMethod) -> { [string]: any },
}

export type AstClassPrimaryConstructor = {
    read id: Id,
    read kind: "AstClassPrimaryConstructor",
    read category: "aux",
    read visibility: (self: AstClassPrimaryConstructor) -> ("public" | "private"),
    read setVisibility: (self: AstClassPrimaryConstructor, visibility: "public" | "private") -> AstClassPrimaryConstructor,
    read args: (self: AstClassPrimaryConstructor) -> {AstLocal},
    read setArgs: (self: AstClassPrimaryConstructor, args: {AstLocal}) -> AstClassPrimaryConstructor,
    read argsDefaults: (self: AstClassPrimaryConstructor) -> {AstNode | false},
    read setArgsDefaults: (self: AstClassPrimaryConstructor, argsDefaults: {AstNode | false}) -> AstClassPrimaryConstructor,
    read argsQualifiers: (self: AstClassPrimaryConstructor) -> {AstClassPrimaryConstructorParamQualifiers},
    read setArgsQualifiers: (self: AstClassPrimaryConstructor, argsQualifiers: {AstClassPrimaryConstructorParamQualifiers}) -> AstClassPrimaryConstructor,
    read explicitVisibility: (self: AstClassPrimaryConstructor) -> boolean,
    read setExplicitVisibility: (self: AstClassPrimaryConstructor, explicitVisibility: boolean) -> AstClassPrimaryConstructor,
    read qualifierLocation: (self: AstClassPrimaryConstructor) -> AstLocation?,
    read argLocation: (self: AstClassPrimaryConstructor) -> AstLocation,
    read properties: (self: AstClassPrimaryConstructor) -> { [string]: any },
}

export type AstClassPrimaryConstructorParamQualifiers = {
    read id: Id,
    read kind: "AstClassPrimaryConstructorParamQualifiers",
    read category: "aux",
    read visibility: (self: AstClassPrimaryConstructorParamQualifiers) -> ("public" | "private"),
    read setVisibility: (self: AstClassPrimaryConstructorParamQualifiers, visibility: "public" | "private") -> AstClassPrimaryConstructorParamQualifiers,
    read isConst: (self: AstClassPrimaryConstructorParamQualifiers) -> boolean,
    read setIsConst: (self: AstClassPrimaryConstructorParamQualifiers, isConst: boolean) -> AstClassPrimaryConstructorParamQualifiers,
    read explicitVisibility: (self: AstClassPrimaryConstructorParamQualifiers) -> boolean,
    read setExplicitVisibility: (self: AstClassPrimaryConstructorParamQualifiers, explicitVisibility: boolean) -> AstClassPrimaryConstructorParamQualifiers,
    read qualifierLocation: (self: AstClassPrimaryConstructorParamQualifiers) -> AstLocation?,
    read constLocation: (self: AstClassPrimaryConstructorParamQualifiers) -> AstLocation?,
    read properties: (self: AstClassPrimaryConstructorParamQualifiers) -> { [string]: any },
}

export type AstTypeList = {
    read id: Id,
    read kind: "AstTypeList",
    read category: "aux",
    read types: (self: AstTypeList) -> {AstType},
    read setTypes: (self: AstTypeList, types: {AstType}) -> AstTypeList,
    read tailType: (self: AstTypeList) -> AstTypePack?,
    read setTailType: (self: AstTypeList, tailType: AstTypePack?) -> AstTypeList,
    read properties: (self: AstTypeList) -> { [string]: any },
}

-- CST for tableitem
export type CstTableItem = {
    read kind: "CstTableItem",
    read category: "aux",
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

export type CstTypeTableItem = {
    read id: Id,
    read kind: "CstTypeTableItem",
    read category: "aux",
    read itemKind: (self: CstTypeTableItem) -> ("indexer" | "property" | "stringProperty"),
    read indexerOpenPosition: (self: CstTypeTableItem) -> AstPosition?,
    read indexerClosePosition: (self: CstTypeTableItem) -> AstPosition?,
    read colonPosition: (self: CstTypeTableItem) -> AstPosition?,
    read separator: (self: CstTypeTableItem) -> ("comma" | "semicolon" | "missing"),
    read separatorPosition: (self: CstTypeTableItem) -> AstPosition?,
    read stringInfo: (self: CstTypeTableItem) -> CstExprConstantString?,
    read stringPosition: (self: CstTypeTableItem) -> AstPosition?,
    read properties: (self: CstTypeTableItem) -> { [string]: any },
}

export type CstAttrList = {
    read id: Id,
    read kind: "CstAttrList",
    read category: "aux",
    read atBracketPosition: (self: CstAttrList) -> AstPosition?,
    read closeBracketPosition: (self: CstAttrList) -> AstPosition?,
    read commaPositions: (self: CstAttrList) -> {AstPosition},
    read properties: (self: CstAttrList) -> { [string]: any },
}

export type CstTypeInstantiation = {
    read id: Id,
    read kind: "CstTypeInstantiation",
    read category: "aux",
    read leftArrow1Position: (self: CstTypeInstantiation) -> AstPosition?,
    read leftArrow2Position: (self: CstTypeInstantiation) -> AstPosition?,
    read commaPositions: (self: CstTypeInstantiation) -> {AstPosition},
    read rightArrow1Position: (self: CstTypeInstantiation) -> AstPosition?,
    read rightArrow2Position: (self: CstTypeInstantiation) -> AstPosition?,
    read properties: (self: CstTypeInstantiation) -> { [string]: any },
}

export type CstNodeCommon = {
    read id: Id,
    read category: "cst",
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
    read explicitTypes: (self: CstExprCall) -> CstTypeInstantiation?,
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
    read attrLists: (self: CstExprFunction) -> {CstAttrList | false},
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

export type CstExprInterpString = CstNodeCommon & {
    read kind: "CstExprInterpString",
    read commaPositions: (self: CstExprInterpString) -> {AstPosition},
    read sourceStrings: (self: CstExprInterpString) -> {string},
}
export type CstExprExplicitTypeInstantiation = CstNodeCommon & {
    read kind: "CstExprExplicitTypeInstantiation",
    read instantiation: (self: CstExprExplicitTypeInstantiation) -> CstTypeInstantiation,
}

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
export type CstStatFunction = CstNodeCommon & {
    read kind: "CstStatFunction",
    read functionKeywordPosition: (self: CstStatFunction) -> AstPosition?,
    read attrLists: (self: CstStatFunction) -> {CstAttrList | false},
}

export type CstStatLocalFunction = CstNodeCommon & {
    read kind: "CstStatLocalFunction",
    read localKeywordPosition: (self: CstStatLocalFunction) -> AstPosition?,
    read functionKeywordPosition: (self: CstStatLocalFunction) -> AstPosition?,
    read attrLists: (self: CstStatLocalFunction) -> {CstAttrList | false},
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

export type CstTypeTable = CstNodeCommon & {
    read kind: "CstTypeTable",
    read isArray: (self: CstTypeTable) -> boolean,
    read items: (self: CstTypeTable) -> {CstTypeTableItem},
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
    read quoteStyle: (self: CstTypeSingletonString) -> ("single" | "double" | "raw" | "interp"),
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
    read untaint: (self: AstNode) -> AstNode,
    read tainted: (self: AstNode) -> boolean,
    read leadingComments: (self: AstNode) -> {AstComment},
    read setLeadingComments: (self: AstNode, comments: {AstComment}) -> AstNode,
    read trailingComments: (self: AstNode) -> {AstComment},
    read setTrailingComments: (self: AstNode, comments: {AstComment}) -> AstNode,
    read origlocation: (self: AstNode) -> AstLocation,
    read cst: (self: AstNode) -> CstNode?,
    read children: (self: AstNode) -> {AstNode},
    read walk: (self: AstNode, visitor: (node: AstNode) -> boolean?, filter: AstFilter?) -> (),
    read luwuOnly: (self: AstNode) -> boolean,
    read setLuwuOnly: (self: AstNode, luwuOnly: boolean) -> AstNode,
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
    read ifLocation: (self: AstStatIf) -> AstLocation,
    read thenLocation: (self: AstStatIf) -> AstLocation?,
    read elseLocation: (self: AstStatIf) -> AstLocation?,
}

export type AstStatWhile = AstStatCommon & {
    read kind: "AstStatWhile",
    read hasDo: (self: AstStatWhile) -> boolean,
    read setHasDo: (self: AstStatWhile, hasDo: boolean) -> AstStatWhile,
    read condition: (self: AstStatWhile) -> AstExpr,
    read setCondition: (self: AstStatWhile, condition: AstExpr) -> AstStatWhile,
    read body: (self: AstStatWhile) -> AstStatBlock,
    read setBody: (self: AstStatWhile, body: AstStatBlock) -> AstStatWhile,
    read whileLocation: (self: AstStatWhile) -> AstLocation,
    read doLocation: (self: AstStatWhile) -> AstLocation,
}

export type AstStatRepeat = AstStatCommon & {
    read kind: "AstStatRepeat",
    read condition: (self: AstStatRepeat) -> AstExpr,
    read setCondition: (self: AstStatRepeat, condition: AstExpr) -> AstStatRepeat,
    read body: (self: AstStatRepeat) -> AstStatBlock,
    read setBody: (self: AstStatRepeat, body: AstStatBlock) -> AstStatRepeat,
    read repeatLocation: (self: AstStatRepeat) -> AstLocation,
    read untilLocation: (self: AstStatRepeat) -> AstLocation,
}

export type AstStatBreak = AstStatCommon & { read kind: "AstStatBreak" }
export type AstStatContinue = AstStatCommon & { read kind: "AstStatContinue" }

export type AstStatReturn = AstStatCommon & {
    read kind: "AstStatReturn",
    read list: (self: AstStatReturn) -> {AstExpr},
    read setList: (self: AstStatReturn, list: {AstExpr}) -> AstStatReturn,
    read returnLocation: (self: AstStatReturn) -> AstLocation,
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
    read keywordLocation: (self: AstStatLocal) -> AstLocation?,
    read equalsSignLocation: (self: AstStatLocal) -> AstLocation?,
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
    read forLocation: (self: AstStatFor) -> AstLocation,
    read doLocation: (self: AstStatFor) -> AstLocation,
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
    read forLocation: (self: AstStatForIn) -> AstLocation,
    read inLocation: (self: AstStatForIn) -> AstLocation,
    read doLocation: (self: AstStatForIn) -> AstLocation,
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
    read functionLocation: (self: AstStatFunction) -> AstLocation,
}

export type AstStatLocalFunction = AstStatCommon & {
    read kind: "AstStatLocalFunction",
    read isConst: (self: AstStatLocalFunction) -> boolean,
    read setIsConst: (self: AstStatLocalFunction, isConst: boolean) -> AstStatLocalFunction,
    read name: (self: AstStatLocalFunction) -> AstLocal,
    read setName: (self: AstStatLocalFunction, name: AstLocal) -> AstStatLocalFunction,
    read func: (self: AstStatLocalFunction) -> AstExprFunction,
    read setFunc: (self: AstStatLocalFunction, func: AstExprFunction) -> AstStatLocalFunction,
    read constKeywordBegin: (self: AstStatLocalFunction) -> AstPosition?,
    read keywordLocation: (self: AstStatLocalFunction) -> AstLocation,
    read functionLocation: (self: AstStatLocalFunction) -> AstLocation,
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
    read nameLocation: (self: AstStatTypeAlias) -> AstLocation,
    read typeLocation: (self: AstStatTypeAlias) -> AstLocation,
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
    read nameLocation: (self: AstStatTypeFunction) -> AstLocation,
}

export type AstStatDeclareGlobal = AstStatCommon & {
    read kind: "AstStatDeclareGlobal",
    read name: (self: AstStatDeclareGlobal) -> string,
    read setName: (self: AstStatDeclareGlobal, name: string) -> AstStatDeclareGlobal,
    read type: (self: AstStatDeclareGlobal) -> AstType,
    read setType: (self: AstStatDeclareGlobal, type: AstType) -> AstStatDeclareGlobal,
    read nameLocation: (self: AstStatDeclareGlobal) -> AstLocation,
    read declareLocation: (self: AstStatDeclareGlobal) -> AstLocation,
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
    read paramNames: (self: AstStatDeclareFunction) -> {string | false},
    read setParamNames: (self: AstStatDeclareFunction, paramNames: {string | false}) -> AstStatDeclareFunction,
    read paramNameLocations: (self: AstStatDeclareFunction) -> {AstLocation | false},
    read nameLocation: (self: AstStatDeclareFunction) -> AstLocation,
    read varargLocation: (self: AstStatDeclareFunction) -> AstLocation,
    read declareLocation: (self: AstStatDeclareFunction) -> AstLocation,
    read functionLocation: (self: AstStatDeclareFunction) -> AstLocation,
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
    read declareLocation: (self: AstStatDeclareExternType) -> AstLocation,
    read classLocation: (self: AstStatDeclareExternType) -> AstLocation,
    read extendsLocation: (self: AstStatDeclareExternType) -> AstLocation?,
}

export type AstStatClass = AstStatCommon & {
    read kind: "AstStatClass",
    read exported: (self: AstStatClass) -> boolean,
    read setExported: (self: AstStatClass, exported: boolean) -> AstStatClass,
    read name: (self: AstStatClass) -> AstLocal,
    read setName: (self: AstStatClass, name: AstLocal) -> AstStatClass,
    read members: (self: AstStatClass) -> {AstClassProperty | AstClassMethod},
    read setMembers: (self: AstStatClass, members: {AstClassProperty | AstClassMethod}) -> AstStatClass,
    read primaryConstructor: (self: AstStatClass) -> AstClassPrimaryConstructor?,
    read setPrimaryConstructor: (self: AstStatClass, primaryConstructor: AstClassPrimaryConstructor?) -> AstStatClass,
    read explicitVisibility: (self: AstStatClass) -> boolean,
    read setExplicitVisibility: (self: AstStatClass, explicitVisibility: boolean) -> AstStatClass,
    read generics: (self: AstStatClass) -> {AstGenericType},
    read setGenerics: (self: AstStatClass, generics: {AstGenericType}) -> AstStatClass,
    read genericPacks: (self: AstStatClass) -> {AstGenericTypePack},
    read setGenericPacks: (self: AstStatClass, genericPacks: {AstGenericTypePack}) -> AstStatClass,
    read hasEnd: (self: AstStatClass) -> boolean,
    read setHasEnd: (self: AstStatClass, hasEnd: boolean) -> AstStatClass,
    read keywordLocation: (self: AstStatClass) -> AstLocation,
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
    read parseResult: (self: AstExprConstantNumber) -> ("ok" | "imprecise" | "malformed" | "binoverflow" | "hexoverflow" | "intoverflow"),
    read setParseResult: (self: AstExprConstantNumber, parseResult: "ok" | "imprecise" | "malformed" | "binoverflow" | "hexoverflow" | "intoverflow") -> AstExprConstantNumber,
}

export type AstExprConstantInteger = AstExprCommon & {
    read kind: "AstExprConstantInteger",
    read value: (self: AstExprConstantInteger) -> number,
    read setValue: (self: AstExprConstantInteger, value: number) -> AstExprConstantInteger,
    read parseResult: (self: AstExprConstantInteger) -> ("ok" | "imprecise" | "malformed" | "binoverflow" | "hexoverflow" | "intoverflow"),
    read setParseResult: (self: AstExprConstantInteger, parseResult: "ok" | "imprecise" | "malformed" | "binoverflow" | "hexoverflow" | "intoverflow") -> AstExprConstantInteger,
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
    read argLocation: (self: AstExprCall) -> AstLocation,
}

export type AstExprIndexName = AstExprCommon & {
    read kind: "AstExprIndexName",
    read index: (self: AstExprIndexName) -> string,
    read setIndex: (self: AstExprIndexName, index: string) -> AstExprIndexName,
    read op: (self: AstExprIndexName) -> string,
    read setOp: (self: AstExprIndexName, op: string) -> AstExprIndexName,
    read expr: (self: AstExprIndexName) -> AstExpr,
    read setExpr: (self: AstExprIndexName, expr: AstExpr) -> AstExprIndexName,
    read indexLocation: (self: AstExprIndexName) -> AstLocation,
    read opPosition: (self: AstExprIndexName) -> AstPosition?,
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
    read argsDefaults: (self: AstExprFunction) -> {AstNode | false},
    read setArgsDefaults: (self: AstExprFunction, argsDefaults: {AstNode | false}) -> AstExprFunction,
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
    read varargAnnotation: (self: AstExprFunction) -> AstTypePack?,
    read setVarargAnnotation: (self: AstExprFunction, varargAnnotation: AstTypePack?) -> AstExprFunction,
    read self: (self: AstExprFunction) -> AstLocal?,
    read setSelf: (self: AstExprFunction, value: AstLocal?) -> AstExprFunction,
    read functionDepth: (self: AstExprFunction) -> integer,
    read setFunctionDepth: (self: AstExprFunction, functionDepth: number | integer) -> AstExprFunction,
    read varargLocation: (self: AstExprFunction) -> AstLocation,
    read argLocation: (self: AstExprFunction) -> AstLocation?,
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
    read hasThen: (self: AstExprIfElse) -> boolean,
    read setHasThen: (self: AstExprIfElse, hasThen: boolean) -> AstExprIfElse,
    read ifLocation: (self: AstExprIfElse) -> AstLocation,
    read thenLocation: (self: AstExprIfElse) -> AstLocation?,
    read elseLocation: (self: AstExprIfElse) -> AstLocation?,
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
    read prefixLocal: (self: AstTypeReference) -> AstLocal?,
    read setPrefixLocal: (self: AstTypeReference, prefixLocal: AstLocal?) -> AstTypeReference,
    read prefixLocation: (self: AstTypeReference) -> AstLocation?,
    read nameLocation: (self: AstTypeReference) -> AstLocation,
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
    read argNames: (self: AstTypeFunction) -> {string | false},
    read setArgNames: (self: AstTypeFunction, argNames: {string | false}) -> AstTypeFunction,
    read argNameLocations: (self: AstTypeFunction) -> {AstLocation | false},
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
    read match: (self: AstFilter, node: AstNode) -> boolean,
}

export type AstAllocator = {
    read id: Id,
    read parse: (self: AstAllocator, source: string, includeCst: boolean?, processComments: boolean?) -> AstDocument,
    read parseexpr: (self: AstAllocator, source: string, includeCst: boolean?, processComments: boolean?) -> AstNode,
    read defaultnode: (self: AstAllocator, kind: string) ->
        | AstNode | CstNode | AstLocal
        | AstTableProp | AstTableIndexer | AstDeclaredExternTypeProperty | AstTableItem | AstTypeList | AstComment
        | AstClassProperty | AstClassMethod | AstClassPrimaryConstructor | AstClassPrimaryConstructorParamQualifiers
        | CstTableItem | CstTypeTableItem | CstAttrList | CstTypeInstantiation,
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
    read hotcomments: (self: AstDocument) -> {HotComment},
    read lines: (self: AstDocument) -> number,
    read properties: (self: AstDocument) -> { [string]: any },
}

local function allocator(): AstAllocator
    error("Not implemented")
end

local function filter(...: string | AstFilter): AstFilter
    error("Not implemented")
end

return {
    allocator = allocator,
    filter = filter,
}
