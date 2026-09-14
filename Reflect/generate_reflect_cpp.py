from __future__ import annotations

import sys
from pathlib import Path
from typing import Any, Literal, Optional, Union
from pydantic import BaseModel, field_validator


class FieldAccessor(BaseModel):
    read: Union[bool, str] = True
    write: Union[bool, str] = True

    @property
    def is_readable(self) -> bool:
        return bool(self.read)

    @property
    def is_writable(self) -> bool:
        return bool(self.write)

    def read_expr(self, target: str = "") -> Optional[str]:
        if not self.read:
            return None
        return self.read if isinstance(self.read, str) else (target or None)

    def write_expr(self, target: str = "") -> Optional[str]:
        if not self.write:
            return None
        return self.write if isinstance(self.write, str) else (target or None)


class FieldDef(BaseModel):
    name: str
    accessor: FieldAccessor = FieldAccessor()
    member: Optional[str] = None
    atom: Optional[str] = None
    set_atom: Optional[str] = None
    doc: Optional[str] = None

    @field_validator("accessor", mode="before")
    @classmethod
    def coerce_accessor(cls, v: Any) -> Any:
        if isinstance(v, dict):
            return FieldAccessor(**v)
        return v

    @property
    def member_expr(self) -> str:
        return self.member or self.name

    @property
    def atom_name(self) -> str:
        if self.atom:
            return self.atom
        special = {
            "thenbody": "ThenBody",
            "elsebody": "ElseBody",
            "debugname": "DebugName",
            "origlocation": "OrigLocation",
        }
        return special.get(self.name, self.name[0].upper() + self.name[1:])

    @property
    def set_atom_name(self) -> str:
        return self.set_atom or f"Set{self.atom_name}"

    @property
    def is_readable(self) -> bool:
        return self.accessor.is_readable

    @property
    def is_writable(self) -> bool:
        return self.accessor.is_writable

    def read_expr(self, prefix: str = "") -> Optional[str]:
        target = f"{prefix}{self.member_expr}" if prefix else self.member_expr
        return self.accessor.read_expr(target)

    def write_expr(self, prefix: str = "") -> Optional[str]:
        target = f"{prefix}{self.member_expr}" if prefix else self.member_expr
        return self.accessor.write_expr(target)


class FactoryDef(BaseModel):
    strategy: Literal["alloc", "aux_union", "empty", "custom"] = "alloc"
    args: list[str] = []
    inner_type: Optional[str] = None
    custom_expr: Optional[str] = None


class NodeDef(BaseModel):
    name: str
    category: Optional[str] = None
    base: Optional[str] = None
    factory: Optional[FactoryDef] = None
    fields: list[FieldDef] = []
    doc: Optional[str] = None

    enum_name: Optional[str] = None
    union_member: Optional[str] = None

    @field_validator("fields", mode="before")
    @classmethod
    def coerce_fields(cls, v: Any) -> list[Any]:
        res = []
        if isinstance(v, list):
            for item in v:
                if isinstance(item, str):
                    res.append(FieldDef(name=item))
                elif isinstance(item, dict):
                    res.append(FieldDef(**item))
                else:
                    res.append(item)
        return res

    @property
    def is_empty(self) -> bool:
        return len(self.fields) == 0


class UserdataDef(BaseModel):
    name: str
    tag: str
    storage: Literal["pointer", "union", "value"] = "pointer"
    is_const: bool = False
    output_file: str = ""
    register_func: Optional[str] = None
    headers: list[str] = []
    categories: list[str] = []
    base_fields: list[FieldDef] = []
    stat_fields: list[FieldDef] = []
    nodes: list[NodeDef] = []


class ReflectSchema(BaseModel):
    userdatas: list[UserdataDef] = []

    def get_userdata(self, name: str) -> Optional[UserdataDef]:
        for u in self.userdatas:
            if u.name == name:
                return u
        return None

    def all_property_atoms(self) -> list[tuple[str, str, bool]]:
        setters: dict[str, tuple[str, str]] = {}
        getters: dict[str, str] = {}

        for ud in self.userdatas:
            base_and_stat = list(ud.base_fields) + list(ud.stat_fields)
            for node in ud.nodes:
                for f in base_and_stat + node.fields:
                    if f.is_writable:
                        setters[f.atom_name] = (f.name, f.set_atom_name)
                    elif f.is_readable:
                        if f.atom_name not in setters:
                            getters[f.atom_name] = f.name

        result: list[tuple[str, str, bool]] = []
        for atom, (name, set_atom) in setters.items():
            result.append((atom, name, True))
        for atom, name in getters.items():
            if atom not in setters:
                result.append((atom, name, False))

        return sorted(result, key=lambda x: x[0])


AST_NODES_USERDATA = UserdataDef(
    name="AstNode",
    tag="TagNode",
    storage="pointer",
    is_const=False,
    output_file="AstNodes.gen.inl",
    register_func="registerNodeClass",
    headers=[
        '"Luau/Ast.h"',
        '"Luau/Cst.h"',
        '"Luau/ReflectCommon.h"',
        '"Luau/ReflectAstHandler.h"',
    ],
    categories=["Stat", "Expr", "Type", "TypePack", "Generic", "Attr"],
    base_fields=[
        FieldDef(name="origlocation", atom="OrigLocation", accessor=FieldAccessor(read="n->location", write=False)),
        FieldDef(name="cst", atom="Cst", accessor=FieldAccessor(read="getNodeCst(handle, n)", write=False)),
    ],
    stat_fields=[
        FieldDef(name="hasSemicolon", atom="HasSemicolon", accessor=FieldAccessor()),
    ],
    nodes=[
        # Statements
        NodeDef(
            name="AstStatBlock",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstArray<Luau::AstStat*>{nullptr, 0}", "false"],
            ),
            fields=["hasEnd", "body"],
        ),
        NodeDef(
            name="AstStatIf",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "nullptr", "nullptr", "nullptr", "std::nullopt", "std::nullopt"],
            ),
            fields=[
                "condition",
                FieldDef(name="thenbody", atom="ThenBody"),
                FieldDef(name="elsebody", atom="ElseBody"),
            ],
        ),
        NodeDef(
            name="AstStatWhile",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "nullptr", "nullptr", "false", "Luau::Location()"],
            ),
            fields=["hasDo", "condition", "body"],
        ),
        NodeDef(
            name="AstStatRepeat",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "nullptr", "nullptr", "false"],
            ),
            fields=["condition", "body"],
        ),
        NodeDef(
            name="AstStatBreak",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()"]),
            fields=[],
        ),
        NodeDef(
            name="AstStatContinue",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()"]),
            fields=[],
        ),
        NodeDef(
            name="AstStatReturn",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstArray<Luau::AstExpr*>{nullptr, 0}"],
            ),
            fields=["list"],
        ),
        NodeDef(
            name="AstStatExpr",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "nullptr"]),
            fields=["expr"],
        ),
        NodeDef(
            name="AstStatLocal",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "Luau::AstArray<Luau::AstLocal*>{nullptr, 0}",
                    "Luau::AstArray<Luau::AstExpr*>{nullptr, 0}",
                    "std::nullopt",
                    "false",
                ],
            ),
            fields=[
                "isConst",
                FieldDef(name="exported", member="isExported", atom="Exported"),
                "vars",
                "values",
            ],
        ),
        NodeDef(
            name="AstStatFor",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "nullptr",
                    "nullptr",
                    "nullptr",
                    "nullptr",
                    "nullptr",
                    "false",
                    "Luau::Location()",
                ],
            ),
            fields=["hasDo", "var", "from", "to", "step", "body"],
        ),
        NodeDef(
            name="AstStatForIn",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "Luau::AstArray<Luau::AstLocal*>{nullptr, 0}",
                    "Luau::AstArray<Luau::AstExpr*>{nullptr, 0}",
                    "nullptr",
                    "false",
                    "Luau::Location()",
                    "false",
                    "Luau::Location()",
                ],
            ),
            fields=["hasIn", "hasDo", "vars", "values", "body"],
        ),
        NodeDef(
            name="AstStatAssign",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "Luau::AstArray<Luau::AstExpr*>{nullptr, 0}",
                    "Luau::AstArray<Luau::AstExpr*>{nullptr, 0}",
                ],
            ),
            fields=["vars", "values"],
        ),
        NodeDef(
            name="AstStatCompoundAssign",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstExprBinary::Op::Add", "nullptr", "nullptr"],
            ),
            fields=["op", "var", "value"],
        ),
        NodeDef(
            name="AstStatFunction",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "nullptr", "nullptr"],
            ),
            fields=["name", "func"],
        ),
        NodeDef(
            name="AstStatLocalFunction",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "nullptr", "nullptr", "false", "Luau::Position::missing()"],
            ),
            fields=["isConst", "name", "func"],
        ),
        NodeDef(
            name="AstStatTypeAlias",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "Luau::AstName()",
                    "Luau::Location()",
                    "Luau::AstArray<Luau::AstGenericType*>{nullptr, 0}",
                    "Luau::AstArray<Luau::AstGenericTypePack*>{nullptr, 0}",
                    "nullptr",
                    "false",
                ],
            ),
            fields=["name", "exported", "type", "generics", "genericPacks"],
        ),
        NodeDef(
            name="AstStatTypeFunction",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstName()", "Luau::Location()", "nullptr", "false", "false"],
            ),
            fields=["name", "exported", "hasErrors", "body"],
        ),
        NodeDef(
            name="AstStatDeclareGlobal",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstName()", "Luau::Location()", "nullptr"],
            ),
            fields=["name", "type"],
        ),
        NodeDef(
            name="AstStatDeclareFunction",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "Luau::AstArray<Luau::AstAttr*>{nullptr, 0}",
                    "Luau::AstName()",
                    "Luau::Location()",
                    "Luau::AstArray<Luau::AstGenericType*>{nullptr, 0}",
                    "Luau::AstArray<Luau::AstGenericTypePack*>{nullptr, 0}",
                    "Luau::AstTypeList{}",
                    "Luau::AstArray<Luau::AstArgumentName>{nullptr, 0}",
                    "false",
                    "Luau::Location()",
                    "nullptr",
                ],
            ),
            fields=[
                "name",
                "vararg",
                "generics",
                "genericPacks",
                "params",
                FieldDef(name="returnTypes", member="retTypes", atom="ReturnTypes"),
                "attributes",
            ],
        ),
        NodeDef(
            name="AstStatClass",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "nullptr", "Luau::AstArray<Luau::AstClassMember>{nullptr, 0}", "false"],
            ),
            fields=["exported", "name", "members"],
        ),
        NodeDef(
            name="AstStatDeclareExternType",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "Luau::AstName()",
                    "std::nullopt",
                    "Luau::AstArray<Luau::AstDeclaredExternTypeProperty>{nullptr, 0}",
                    "nullptr",
                ],
            ),
            fields=["name", "superName", "props", "indexer", "generics", "genericPacks"],
        ),
        NodeDef(
            name="AstStatError",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "Luau::AstArray<Luau::AstExpr*>{nullptr, 0}",
                    "Luau::AstArray<Luau::AstStat*>{nullptr, 0}",
                    "0",
                ],
            ),
            fields=[
                FieldDef(name="messageIndex", accessor=FieldAccessor(write=False)),
                FieldDef(name="expressions", accessor=FieldAccessor(write=False)),
                FieldDef(name="statements", accessor=FieldAccessor(write=False)),
            ],
        ),
        # Expressions
        NodeDef(
            name="AstExprGroup",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "nullptr"]),
            fields=["expr"],
        ),
        NodeDef(
            name="AstExprConstantNil",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()"]),
            fields=[],
        ),
        NodeDef(
            name="AstExprConstantBool",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "false"]),
            fields=["value"],
        ),
        NodeDef(
            name="AstExprConstantNumber",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "0.0"]),
            fields=["value"],
        ),
        NodeDef(
            name="AstExprConstantInteger",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "int64_t(0)"]),
            fields=["value"],
        ),
        NodeDef(
            name="AstExprConstantString",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "Luau::AstArray<char>{nullptr, 0}",
                    "Luau::AstExprConstantString::QuoteStyle::QuotedSimple",
                ],
            ),
            fields=["value", "quoteStyle"],
        ),
        NodeDef(
            name="AstExprLocal",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "nullptr", "false"]),
            fields=["upvalue", "local"],
        ),
        NodeDef(
            name="AstExprGlobal",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "Luau::AstName()"]),
            fields=["name"],
        ),
        NodeDef(
            name="AstExprVarargs",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()"]),
            fields=[],
        ),
        NodeDef(
            name="AstExprCall",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "nullptr",
                    "Luau::AstArray<Luau::AstExpr*>{nullptr, 0}",
                    "false",
                    "Luau::AstArray<Luau::AstTypeOrPack>{nullptr, 0}",
                    "Luau::Location()",
                ],
            ),
            fields=["self", "func", "args", "typeArguments"],
        ),
        NodeDef(
            name="AstExprIndexName",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "nullptr",
                    "Luau::AstName()",
                    "Luau::Location()",
                    "Luau::Position::missing()",
                    "'.'",
                ],
            ),
            fields=["index", "op", "expr"],
        ),
        NodeDef(
            name="AstExprIndexExpr",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "nullptr", "nullptr"]),
            fields=["expr", "index"],
        ),
        NodeDef(
            name="AstExprFunction",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "Luau::AstArray<Luau::AstAttr*>{nullptr, 0}",
                    "Luau::AstArray<Luau::AstGenericType*>{nullptr, 0}",
                    "Luau::AstArray<Luau::AstGenericTypePack*>{nullptr, 0}",
                    "nullptr",
                    "Luau::AstArray<Luau::AstLocal*>{nullptr, 0}",
                    "Luau::AstArray<Luau::AstExpr*>{nullptr, 0}",
                    "false",
                    "Luau::Location()",
                    "nullptr",
                    "0",
                    "Luau::AstName()",
                    "nullptr",
                    "nullptr",
                    "std::nullopt",
                ],
            ),
            fields=[
                "vararg",
                FieldDef(name="debugname", atom="DebugName"),
                "args",
                "body",
                "generics",
                "genericPacks",
                "returnAnnotation",
                "attributes",
            ],
        ),
        NodeDef(
            name="AstExprTable",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstArray<Luau::AstExprTable::Item>{nullptr, 0}"],
            ),
            fields=["items"],
        ),
        NodeDef(
            name="AstExprUnary",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstExprUnary::Op::Not", "nullptr"],
            ),
            fields=["op", "expr"],
        ),
        NodeDef(
            name="AstExprBinary",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstExprBinary::Op::Add", "nullptr", "nullptr"],
            ),
            fields=["op", "left", "right"],
        ),
        NodeDef(
            name="AstExprTypeAssertion",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "nullptr", "nullptr"]),
            fields=["expr", "annotation"],
        ),
        NodeDef(
            name="AstExprIfElse",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "nullptr", "true", "nullptr", "true", "nullptr"],
            ),
            fields=["hasElse", "condition", "trueExpr", "falseExpr"],
        ),
        NodeDef(
            name="AstExprInterpString",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "Luau::AstArray<Luau::AstArray<char>>{nullptr, 0}",
                    "Luau::AstArray<Luau::AstExpr*>{nullptr, 0}",
                ],
            ),
            fields=["strings", "expressions"],
        ),
        NodeDef(
            name="AstExprInstantiate",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "nullptr", "Luau::AstArray<Luau::AstTypeOrPack>{nullptr, 0}"],
            ),
            fields=["expr", "typeArguments"],
        ),
        NodeDef(
            name="AstExprError",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstArray<Luau::AstExpr*>{nullptr, 0}", "0"],
            ),
            fields=[
                FieldDef(name="messageIndex", accessor=FieldAccessor(write=False)),
                FieldDef(name="expressions", accessor=FieldAccessor(write=False)),
            ],
        ),
        # Types, TypePacks, Generics, Attr
        NodeDef(
            name="AstTypeReference",
            category="Type",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "std::nullopt",
                    "Luau::AstName()",
                    "std::nullopt",
                    "Luau::Location()",
                    "false",
                    "Luau::AstArray<Luau::AstTypeOrPack>{nullptr, 0}",
                ],
            ),
            fields=["name", "prefix", "hasParameterList", "parameters"],
        ),
        NodeDef(
            name="AstTypeTable",
            category="Type",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstArray<Luau::AstTableProp>{nullptr, 0}", "nullptr"],
            ),
            fields=["props", "indexer"],
        ),
        NodeDef(
            name="AstTypeFunction",
            category="Type",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "Luau::AstArray<Luau::AstAttr*>{nullptr, 0}",
                    "Luau::AstArray<Luau::AstGenericType*>{nullptr, 0}",
                    "Luau::AstArray<Luau::AstGenericTypePack*>{nullptr, 0}",
                    "Luau::AstTypeList{}",
                    "Luau::AstArray<std::optional<Luau::AstArgumentName>>{nullptr, 0}",
                    "nullptr",
                ],
            ),
            fields=["generics", "genericPacks", "argTypes", "returnTypes", "attributes"],
        ),
        NodeDef(
            name="AstTypeTypeof",
            category="Type",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "nullptr"]),
            fields=["expr"],
        ),
        NodeDef(
            name="AstTypeOptional",
            category="Type",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()"]),
            fields=[],
        ),
        NodeDef(
            name="AstTypeUnion",
            category="Type",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstArray<Luau::AstType*>{nullptr, 0}"],
            ),
            fields=["types"],
        ),
        NodeDef(
            name="AstTypeIntersection",
            category="Type",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstArray<Luau::AstType*>{nullptr, 0}"],
            ),
            fields=["types"],
        ),
        NodeDef(
            name="AstTypeSingletonBool",
            category="Type",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "false"]),
            fields=["value"],
        ),
        NodeDef(
            name="AstTypeSingletonString",
            category="Type",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstArray<char>{nullptr, 0}"],
            ),
            fields=["value"],
        ),
        NodeDef(
            name="AstTypeGroup",
            category="Type",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "nullptr"]),
            fields=["type"],
        ),
        NodeDef(
            name="AstTypeError",
            category="Type",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstArray<Luau::AstType*>{nullptr, 0}", "false", "0"],
            ),
            fields=[
                FieldDef(name="isMissing", accessor=FieldAccessor(write=False)),
                FieldDef(name="messageIndex", accessor=FieldAccessor(write=False)),
                FieldDef(name="types", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="AstTypePackExplicit",
            category="TypePack",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "Luau::AstTypeList{}"]),
            fields=["typeList"],
        ),
        NodeDef(
            name="AstTypePackVariadic",
            category="TypePack",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "nullptr"]),
            fields=["variadicType"],
        ),
        NodeDef(
            name="AstTypePackGeneric",
            category="TypePack",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "Luau::AstName()"]),
            fields=[FieldDef(name="name", member="genericName")],
        ),
        NodeDef(
            name="AstGenericType",
            category="Generic",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "Luau::AstName()", "nullptr"]),
            fields=["name", FieldDef(name="type", member="defaultValue")],
        ),
        NodeDef(
            name="AstGenericTypePack",
            category="Generic",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "Luau::AstName()", "nullptr"]),
            fields=["name", FieldDef(name="type", member="defaultValue")],
        ),
        NodeDef(
            name="AstAttr",
            category="Attr",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "Luau::AstAttr::Type::Checked",
                    "Luau::AstArray<Luau::AstExpr*>{nullptr, 0}",
                    "Luau::AstName()",
                ],
            ),
            fields=["type", "name", "args"],
        ),
    ],
)


CST_NODES_USERDATA = UserdataDef(
    name="CstNode",
    tag="TagCstNode",
    storage="pointer",
    is_const=True,
    output_file="CstNodes.gen.inl",
    register_func="registerCstNodeClass",
    headers=[
        '"Luau/Cst.h"',
        '"Luau/ReflectCommon.h"',
        '"Luau/ReflectAstHandler.h"',
    ],
    nodes=[
        NodeDef(
            name="CstAttr",
            factory=FactoryDef(strategy="alloc", args=["false"]),
            fields=[FieldDef(name="hasAt", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstParametrizedAttr",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Position::missing()", "Luau::Position::missing()", "Luau::AstArray<Luau::Position>{nullptr, 0}"],
            ),
            fields=[
                FieldDef(name="openParenPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="closeParenPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="argsCommaPositions", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstExprGroup",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[FieldDef(name="closePosition", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstExprConstantNumber",
            factory=FactoryDef(strategy="alloc", args=["Luau::AstArray<char>{nullptr, 0}"]),
            fields=[FieldDef(name="value", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstExprConstantInteger",
            factory=FactoryDef(strategy="alloc", args=["Luau::AstArray<char>{nullptr, 0}"]),
            fields=[FieldDef(name="value", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstExprConstantString",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::AstArray<char>{nullptr, 0}", "Luau::CstExprConstantString::QuoteStyle::QuotedSingle", "0"],
            ),
            fields=[
                FieldDef(name="quoteStyle", accessor=FieldAccessor(write=False)),
                FieldDef(name="blockDepth", accessor=FieldAccessor(write=False)),
                FieldDef(name="sourceString", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstExprCall",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Position::missing()", "Luau::Position::missing()", "Luau::AstArray<Luau::Position>{nullptr, 0}"],
            ),
            fields=[
                FieldDef(name="openParens", accessor=FieldAccessor(write=False)),
                FieldDef(name="closeParens", accessor=FieldAccessor(write=False)),
                FieldDef(name="commaPositions", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstExprIndexExpr",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Position::missing()", "Luau::Position::missing()"],
            ),
            fields=[
                FieldDef(name="openBracketPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="closeBracketPosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstExprFunction",
            factory=FactoryDef(strategy="empty", args=[]),
            fields=[
                FieldDef(name="functionKeywordPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="openGenericsPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="genericsCommaPositions", accessor=FieldAccessor(write=False)),
                FieldDef(name="closeGenericsPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="argsAnnotationColonPositions", accessor=FieldAccessor(write=False)),
                FieldDef(name="argsCommaPositions", accessor=FieldAccessor(write=False)),
                FieldDef(name="varargAnnotationColonPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="returnSpecifierPosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstExprTable",
            factory=FactoryDef(strategy="alloc", args=["Luau::AstArray<Luau::CstExprTable::Item>{nullptr, 0}"]),
            fields=[FieldDef(name="items", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstExprOp",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[FieldDef(name="opPosition", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstExprTypeAssertion",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[FieldDef(name="opPosition", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstExprIfElse",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Position::missing()", "Luau::Position::missing()", "false"],
            ),
            fields=[
                FieldDef(name="isElseIf", accessor=FieldAccessor(write=False)),
                FieldDef(name="thenPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="elsePosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstExprInterpString",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::AstArray<Luau::AstArray<char>>{nullptr, 0}", "Luau::AstArray<Luau::Position>{nullptr, 0}"],
            ),
            fields=[FieldDef(name="commaPositions", member="stringPositions", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstExprExplicitTypeInstantiation",
            factory=FactoryDef(strategy="empty", args=["Luau::CstTypeInstantiation{}"]),
            fields=[],
        ),
        NodeDef(
            name="CstStatDo",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()", "Luau::Position::missing()"]),
            fields=[
                FieldDef(name="statsStartPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="endPosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstStatRepeat",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[FieldDef(name="untilPosition", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstStatReturn",
            factory=FactoryDef(strategy="alloc", args=["Luau::AstArray<Luau::Position>{nullptr, 0}"]),
            fields=[FieldDef(name="commaPositions", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstStatLocal",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::AstArray<Luau::Position>{nullptr, 0}",
                    "Luau::AstArray<Luau::Position>{nullptr, 0}",
                    "Luau::AstArray<Luau::Position>{nullptr, 0}",
                ],
            ),
            fields=[
                FieldDef(name="varsAnnotationColonPositions", accessor=FieldAccessor(write=False)),
                FieldDef(name="varsCommaPositions", accessor=FieldAccessor(write=False)),
                FieldDef(name="valuesCommaPositions", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstStatFor",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Position::missing()",
                    "Luau::Position::missing()",
                    "Luau::Position::missing()",
                    "Luau::Position::missing()",
                ],
            ),
            fields=[
                FieldDef(name="annotationColonPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="equalsPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="endCommaPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="stepCommaPosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstStatForIn",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::AstArray<Luau::Position>{nullptr, 0}",
                    "Luau::AstArray<Luau::Position>{nullptr, 0}",
                    "Luau::AstArray<Luau::Position>{nullptr, 0}",
                ],
            ),
            fields=[
                FieldDef(name="varsAnnotationColonPositions", accessor=FieldAccessor(write=False)),
                FieldDef(name="varsCommaPositions", accessor=FieldAccessor(write=False)),
                FieldDef(name="valuesCommaPositions", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstStatAssign",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::AstArray<Luau::Position>{nullptr, 0}",
                    "Luau::Position::missing()",
                    "Luau::AstArray<Luau::Position>{nullptr, 0}",
                ],
            ),
            fields=[
                FieldDef(name="varsCommaPositions", accessor=FieldAccessor(write=False)),
                FieldDef(name="equalsPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="valuesCommaPositions", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstStatCompoundAssign",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[FieldDef(name="opPosition", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstStatFunction",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[FieldDef(name="functionKeywordPosition", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstStatLocalFunction",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()", "Luau::Position::missing()"]),
            fields=[
                FieldDef(name="localKeywordPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="functionKeywordPosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstGenericType",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[FieldDef(name="defaultEqualsPosition", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstGenericTypePack",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()", "Luau::Position::missing()"]),
            fields=[
                FieldDef(name="ellipsisPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="defaultEqualsPosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstStatTypeAlias",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Position::missing()",
                    "Luau::Position::missing()",
                    "Luau::AstArray<Luau::Position>{nullptr, 0}",
                    "Luau::Position::missing()",
                    "Luau::Position::missing()",
                ],
            ),
            fields=[
                FieldDef(name="typeKeywordPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="genericsOpenPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="genericsCommaPositions", accessor=FieldAccessor(write=False)),
                FieldDef(name="genericsClosePosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="equalsPosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstStatTypeFunction",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()", "Luau::Position::missing()"]),
            fields=[
                FieldDef(name="typeKeywordPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="functionKeywordPosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstTypeReference",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Position::missing()",
                    "Luau::Position::missing()",
                    "Luau::AstArray<Luau::Position>{nullptr, 0}",
                    "Luau::Position::missing()",
                ],
            ),
            fields=[
                FieldDef(name="prefixPointPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="openParametersPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="parametersCommaPositions", accessor=FieldAccessor(write=False)),
                FieldDef(name="closeParametersPosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstTypeTable",
            factory=FactoryDef(strategy="alloc", args=["Luau::AstArray<Luau::CstTypeTable::Item>{nullptr, 0}", "false"]),
            fields=[FieldDef(name="isArray", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstTypeFunction",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Position::missing()",
                    "Luau::AstArray<Luau::Position>{nullptr, 0}",
                    "Luau::Position::missing()",
                    "Luau::Position::missing()",
                    "Luau::AstArray<Luau::Position>{nullptr, 0}",
                    "Luau::AstArray<Luau::Position>{nullptr, 0}",
                    "Luau::Position::missing()",
                    "Luau::Position::missing()",
                ],
            ),
            fields=[
                FieldDef(name="openGenericsPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="genericsCommaPositions", accessor=FieldAccessor(write=False)),
                FieldDef(name="closeGenericsPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="openArgsPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="argumentNameColonPositions", accessor=FieldAccessor(write=False)),
                FieldDef(name="argumentsCommaPositions", accessor=FieldAccessor(write=False)),
                FieldDef(name="closeArgsPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="returnArrowPosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstTypeTypeof",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()", "Luau::Position::missing()"]),
            fields=[
                FieldDef(name="openPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="closePosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstTypeUnion",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()", "Luau::AstArray<Luau::Position>{nullptr, 0}"]),
            fields=[
                FieldDef(name="leadingPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="separatorPositions", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstTypeIntersection",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()", "Luau::AstArray<Luau::Position>{nullptr, 0}"]),
            fields=[
                FieldDef(name="leadingPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="separatorPositions", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstTypeSingletonString",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::AstArray<char>{nullptr, 0}", "Luau::CstExprConstantString::QuoteStyle::QuotedSingle", "0"],
            ),
            fields=[
                FieldDef(name="quoteStyle", accessor=FieldAccessor(write=False)),
                FieldDef(name="blockDepth", accessor=FieldAccessor(write=False)),
                FieldDef(name="sourceString", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstTypeGroup",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[FieldDef(name="closePosition", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstTypePackExplicit",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Position::missing()", "Luau::Position::missing()", "Luau::AstArray<Luau::Position>{nullptr, 0}"],
            ),
            fields=[
                FieldDef(name="openParenthesesPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="closeParenthesesPosition", accessor=FieldAccessor(write=False)),
                FieldDef(name="commaPositions", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstTypePackGeneric",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[FieldDef(name="ellipsisPosition", accessor=FieldAccessor(write=False))],
        ),
    ],
)


AUX_NODES_USERDATA = UserdataDef(
    name="AstAux",
    tag="TagAux",
    storage="union",
    output_file="AuxNodes.gen.inl",
    headers=[
        '"Luau/Ast.h"',
        '"Luau/Cst.h"',
        '"Luau/ReflectCommon.h"',
        '"Luau/ReflectAstHandler.h"',
    ],
    nodes=[
        NodeDef(
            name="AstTableProp",
            enum_name="Aux_TableProp",
            union_member="tableProp",
            factory=FactoryDef(
                strategy="aux_union",
                inner_type="Luau::AstTableProp",
                args=[
                    "Luau::AstName()",
                    "Luau::Location()",
                    "nullptr",
                    "Luau::AstTableAccess::ReadWrite",
                    "std::nullopt",
                ],
            ),
            fields=[
                FieldDef(name="name", member="n.name"),
                FieldDef(name="origlocation", accessor=FieldAccessor(write=False), member="n.location"),
                FieldDef(name="type", member="n.type"),
                FieldDef(name="access", member="n.access"),
            ],
        ),
        NodeDef(
            name="AstTableIndexer",
            enum_name="Aux_TableIndexer",
            union_member="tableIndexer",
            factory=FactoryDef(
                strategy="aux_union",
                inner_type="Luau::AstTableIndexer",
                args=[
                    "nullptr",
                    "nullptr",
                    "Luau::Location()",
                    "Luau::AstTableAccess::ReadWrite",
                    "std::nullopt",
                ],
            ),
            fields=[
                FieldDef(name="origlocation", accessor=FieldAccessor(write=False), member="n.location"),
                FieldDef(name="indexType", member="n.indexType"),
                FieldDef(name="resultType", member="n.resultType"),
                FieldDef(name="access", member="n.access"),
            ],
        ),
        NodeDef(
            name="AstDeclaredExternTypeProperty",
            enum_name="Aux_DeclaredExternTypeProperty",
            union_member="declaredExternProp",
            factory=FactoryDef(
                strategy="aux_union",
                inner_type="Luau::AstDeclaredExternTypeProperty",
                args=[
                    "Luau::AstName()",
                    "Luau::Location()",
                    "nullptr",
                    "false",
                    "Luau::Location()",
                    "Luau::AstTableAccess::ReadWrite",
                ],
            ),
            fields=[
                FieldDef(name="name", member="n.name"),
                FieldDef(name="origlocation", accessor=FieldAccessor(write=False), member="n.location"),
                FieldDef(name="type", member="n.ty"),
                FieldDef(name="isMethod", member="n.isMethod"),
                FieldDef(name="access", member="n.access"),
            ],
        ),
        NodeDef(
            name="AstClassProperty",
            enum_name="Aux_ClassProperty",
            union_member="classProp",
            factory=FactoryDef(
                strategy="aux_union",
                inner_type="Luau::AstClassProperty",
                args=[
                    "Luau::Location()",
                    "Luau::AstName()",
                    "Luau::Location()",
                    "std::nullopt",
                    "nullptr",
                ],
            ),
            fields=[
                FieldDef(name="name", member="n.name"),
                FieldDef(name="type", member="n.ty"),
            ],
        ),
        NodeDef(
            name="AstClassMethod",
            enum_name="Aux_ClassMethod",
            union_member="classMethod",
            factory=FactoryDef(
                strategy="aux_union",
                inner_type="Luau::AstClassMethod",
                args=[
                    "std::nullopt",
                    "Luau::Location()",
                    "Luau::AstName()",
                    "Luau::Location()",
                    "nullptr",
                ],
            ),
            fields=[
                FieldDef(name="name", member="n.functionName"),
                FieldDef(name="func", member="n.function"),
            ],
        ),
        NodeDef(
            name="AstComment",
            enum_name="Aux_Comment",
            union_member="comment",
            factory=FactoryDef(
                strategy="custom",
                custom_expr='AstAuxData(doc, ReflectComment{Luau::Lexeme::Type::Comment, "--", Luau::Location()})',
            ),
            fields=[
                FieldDef(name="type", member="handle.comment.type"),
                FieldDef(name="text", member="handle.comment.text"),
                FieldDef(name="origlocation", accessor=FieldAccessor(write=False), member="handle.comment.location"),
            ],
        ),
        NodeDef(
            name="AstTableItem",
            enum_name="Aux_TableItem",
            union_member="tableItem",
            factory=FactoryDef(
                strategy="aux_union",
                inner_type="Luau::AstExprTable::Item",
                args=[
                    "Luau::AstExprTable::Item::Kind::List",
                    "nullptr",
                    "nullptr",
                ],
            ),
            fields=[
                FieldDef(name="key", member="n.key"),
                FieldDef(name="value", member="n.value"),
                FieldDef(name="kind", member="n.kind"),
            ],
        ),
        NodeDef(
            name="AstTypeList",
            enum_name="Aux_TypeList",
            union_member="typeList",
            factory=FactoryDef(
                strategy="aux_union",
                inner_type="Luau::AstTypeList",
                args=[
                    "Luau::AstArray<Luau::AstType*>{nullptr, 0}",
                    "nullptr",
                ],
            ),
            fields=[
                FieldDef(name="types", member="n.types"),
                FieldDef(name="tailType", member="n.tailType"),
            ],
        ),
        NodeDef(
            name="CstTableItem",
            enum_name="Aux_CstTableItem",
            union_member="cstTableItem",
            factory=FactoryDef(
                strategy="aux_union",
                inner_type="Luau::CstExprTable::Item",
                args=[
                    "Luau::Position::missing()",
                    "Luau::Position::missing()",
                    "Luau::Position::missing()",
                    "Luau::CstExprTable::Separator::Missing",
                    "Luau::Position::missing()",
                ],
            ),
            fields=[
                FieldDef(name="indexerOpenPosition", accessor=FieldAccessor(write=False), member="n.indexerOpenPosition"),
                FieldDef(name="indexerClosePosition", accessor=FieldAccessor(write=False), member="n.indexerClosePosition"),
                FieldDef(name="equalsPosition", accessor=FieldAccessor(write=False), member="n.equalsPosition"),
                FieldDef(name="separatorPosition", accessor=FieldAccessor(write=False), member="n.separatorPosition"),
                FieldDef(name="separator", accessor=FieldAccessor(write=False), member="n.separator"),
            ],
        ),
    ],
)


GLOBAL_SCHEMA = ReflectSchema(
    userdatas=[AST_NODES_USERDATA, CST_NODES_USERDATA, AUX_NODES_USERDATA]
)


class CppGenerator:
    def __init__(self, schema: ReflectSchema):
        self.schema = schema

    def generate_inl(self, target: Union[str, UserdataDef]) -> str:
        ud = self.schema.get_userdata(target) if isinstance(target, str) else target
        if ud is None:
            raise ValueError(f"Userdata not found: {target}")
        if ud.storage == "pointer":
            return self._generate_pointer_inl(ud)
        elif ud.storage == "union":
            return self._generate_union_inl(ud)
        raise ValueError(f"Unsupported storage type: {ud.storage}")

    def _generate_pointer_inl(self, ud: UserdataDef) -> str:
        out: list[str] = [
            "// Auto-generated by Reflect/generate_reflect_cpp.py. DO NOT EDIT!",
            "#pragma once",
            "",
        ]
        for h in ud.headers:
            out.append(f"#include {h}")
        out.append("")
        out.append("namespace Luau")
        out.append("{")
        out.append("")

        const_prefix = "const " if ud.is_const else ""
        base_ptr = f"{const_prefix}Luau::{ud.name}*"
        handle_type = f"{ud.name}Data"

        if ud.register_func:
            cat_param = "NodeCategory category, " if ud.categories else ""
            out.append("template<typename T>")
            out.append(f"void {ud.register_func}(")
            out.append("    const char* kind,")
            if cat_param:
                out.append(f"    {cat_param}")
            out.append(f"    bool (*methodHandler)(lua_State* L, {handle_type}& handle, ReflectAtom atom),")
            out.append(f"    void (*propCollector)(lua_State* L, {handle_type}& handle),")
            out.append(f"    {base_ptr} (*factory)(Luau::Allocator& alloc)")
            out.append(");")
            out.append("")

        # Global factory function
        out.append(f"{base_ptr} createDefault{ud.name}(ReflectAtom atom, Luau::Allocator& alloc)")
        out.append("{")
        out.append("    switch (atom)")
        out.append("    {")
        for node in ud.nodes:
            if node.factory and node.factory.strategy == "alloc":
                args_str = ", ".join(node.factory.args)
                out.append(f"    case ReflectAtom::{node.name}:")
                out.append(f"        return alloc.alloc<Luau::{node.name}>({args_str});")
        out.append("    default:")
        out.append("        return nullptr;")
        out.append("    }")
        out.append("}")
        out.append("")

        # Per-class factory, method handler, and prop collector
        for node in ud.nodes:
            class_name = node.name
            class_ptr = f"{const_prefix}Luau::{class_name}*"

            fields: list[FieldDef] = list(ud.base_fields)
            if node.base == "AstStat":
                fields.extend(ud.stat_fields)
            fields.extend(node.fields)

            # Factory
            args_str = ", ".join(node.factory.args) if node.factory and node.factory.args else ""
            out.append(f"static {base_ptr} createDefault{class_name}(Luau::Allocator& alloc)")
            out.append("{")
            out.append(f"    return alloc.alloc<Luau::{class_name}>({args_str});")
            out.append("}")
            out.append("")

            # Method handler switch(atom)
            out.append(f"static bool handle{class_name}Methods(lua_State* L, {handle_type}& handle, ReflectAtom atom)")
            out.append("{")
            if not fields:
                out.append("    return false;")
            else:
                out.append(f"    auto* n = static_cast<{class_ptr}>(handle.node);")
                out.append("    switch (atom)")
                out.append("    {")
                for f in fields:
                    read_target = f.read_expr("n->")
                    if read_target:
                        out.append(f"    case ReflectAtom::{f.atom_name}:")
                        out.append(f"        pushReflectValue(L, handle.doc, {read_target});")
                        out.append("        return true;")
                    write_target = f.write_expr("n->")
                    if write_target:
                        out.append(f"    case ReflectAtom::{f.set_atom_name}:")
                        out.append(f"        readReflectValue(L, handle.doc, 2, {write_target});")
                        out.append("        lua_pushvalue(L, 1);")
                        out.append("        return true;")
                out.append("    default:")
                out.append("        return false;")
                out.append("    }")
            out.append("}")
            out.append("")

            # Prop collector
            out.append(f"static void collect{class_name}Props(lua_State* L, {handle_type}& handle)")
            out.append("{")
            if fields:
                out.append(f"    auto* n = static_cast<{class_ptr}>(handle.node);")
                out.append("    (void)n;")
                for f in fields:
                    read_target = f.read_expr("n->")
                    if read_target:
                        out.append(f"    pushReflectValue(L, handle.doc, {read_target});")
                        out.append(f'    lua_setfield(L, -2, "{f.name}");')
            out.append("}")
            out.append("")

        # Registration function
        out.append(f"static void register{ud.name}Classes()")
        out.append("{")
        for node in ud.nodes:
            cat_arg = f"NodeCategory::{node.category}, " if node.category else ""
            out.append(
                f'    {ud.register_func}<Luau::{node.name}>("{node.name}", {cat_arg}'
                f"handle{node.name}Methods, collect{node.name}Props, createDefault{node.name});"
            )
        out.append("}")
        out.append("")
        out.append("} // namespace Luau")
        out.append("")

        return "\n".join(out)

    def _generate_union_inl(self, ud: UserdataDef) -> str:
        out: list[str] = [
            "// Auto-generated by Reflect/generate_reflect_cpp.py. DO NOT EDIT!",
            "#pragma once",
            "",
        ]
        for h in ud.headers:
            out.append(f"#include {h}")
        out.append("")
        out.append("namespace Luau")
        out.append("{")
        out.append("")

        # Factory function createDefaultAstAux(ReflectAtom atom, ...)
        out.append("bool createDefaultAstAux(ReflectAtom atom, const std::shared_ptr<AstDocumentState>& doc, AstAuxData& out)")
        out.append("{")
        out.append("    switch (atom)")
        out.append("    {")
        for node in ud.nodes:
            out.append(f"    case ReflectAtom::{node.name}:")
            if node.factory and node.factory.strategy == "aux_union" and node.factory.inner_type:
                args_str = ", ".join(node.factory.args)
                out.append(f"        out = AstAuxData(doc, {node.factory.inner_type}{{{args_str}}});")
                out.append("        return true;")
            elif node.factory and node.factory.custom_expr:
                out.append(f"        out = {node.factory.custom_expr};")
                out.append("        return true;")
            else:
                out.append("        return false;")
        out.append("    default:")
        out.append("        return false;")
        out.append("    }")
        out.append("}")
        out.append("")

        # dispatchAux with direct switch(handle.kind) and nested switch(atom)
        out.append("static bool dispatchAux(lua_State* L, AstAuxData& handle, ReflectAtom atom)")
        out.append("{")
        out.append("    switch (handle.kind)")
        out.append("    {")
        for node in ud.nodes:
            enum_name = node.enum_name or f"Aux_{node.name}"
            out.append(f"    case {enum_name}:")
            out.append("    {")
            if node.union_member:
                out.append(f"        auto& n = handle.{node.union_member};")
                out.append("        (void)n;")
            out.append("        switch (atom)")
            out.append("        {")
            for f in node.fields:
                read_target = f.read_expr()
                if read_target:
                    out.append(f"        case ReflectAtom::{f.atom_name}:")
                    out.append(f"            pushReflectValue(L, handle.doc, {read_target});")
                    out.append("            return true;")
                write_target = f.write_expr()
                if write_target:
                    out.append(f"        case ReflectAtom::{f.set_atom_name}:")
                    out.append(f"            readReflectValue(L, handle.doc, 2, {write_target});")
                    out.append("            lua_pushvalue(L, 1);")
                    out.append("            return true;")
            out.append("        default:")
            out.append("            return false;")
            out.append("        }")
            out.append("    }")
        out.append("    default:")
        out.append("        return false;")
        out.append("    }")
        out.append("}")
        out.append("")

        # collectAuxProps
        out.append("static void collectAuxProps(lua_State* L, AstAuxData& handle)")
        out.append("{")
        out.append("    switch (handle.kind)")
        out.append("    {")
        for node in ud.nodes:
            enum_name = node.enum_name or f"Aux_{node.name}"
            out.append(f"    case {enum_name}:")
            out.append("    {")
            if node.union_member:
                out.append(f"        auto& n = handle.{node.union_member};")
                out.append("        (void)n;")
            for f in node.fields:
                read_target = f.read_expr()
                if read_target:
                    out.append(f"        pushReflectValue(L, handle.doc, {read_target});")
                    out.append(f'        lua_setfield(L, -2, "{f.name}");')
            out.append("        break;")
            out.append("    }")
        out.append("    default:")
        out.append("        break;")
        out.append("    }")
        out.append("}")
        out.append("")
        out.append("} // namespace Luau")
        out.append("")

        return "\n".join(out)


def main() -> int:
    base_dir = Path(__file__).resolve().parent
    src_dir = base_dir / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    generator = CppGenerator(GLOBAL_SCHEMA)

    for ud in GLOBAL_SCHEMA.userdatas:
        if ud.output_file:
            inl_path = src_dir / ud.output_file
            content = generator.generate_inl(ud)
            inl_path.write_text(content, encoding="utf-8")
            print(f"Generated: {inl_path}")

    print("\nSummary:")
    for ud in GLOBAL_SCHEMA.userdatas:
        print(f"  {ud.name:<10} : {len(ud.nodes)} nodes")
    print("Code generation successful.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
