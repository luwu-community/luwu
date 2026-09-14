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
    class_info_name: Optional[str] = None
    class_table_name: Optional[str] = None
    class_index_getter: str = "T::ClassIndex()"
    headers: list[str] = []
    categories: list[str] = []
    base_fields: list[FieldDef] = []
    stat_fields: list[FieldDef] = []
    nodes: list[NodeDef] = []


BUILTIN_ATOMS: list[tuple[str, str]] = [
    # Special & Document & Allocator
    ("Id", "id"),
    ("Matches", "matches"),
    ("Root", "root"),
    ("Source", "source"),
    ("Prettyprint", "prettyprint"),
    ("Walk", "walk"),
    ("Errors", "errors"),
    ("Comments", "comments"),
    ("LineOffsets", "lineOffsets"),
    ("Properties", "properties"),
    ("Allocator", "allocator"),
    ("Parse", "parse"),
    ("Parseexpr", "parseexpr"),
    ("Defaultnode", "defaultnode"),
    # Node methods
    ("Category", "category"),
    ("Children", "children"),
    # AstLocal kind & properties
    ("AstLocal", "AstLocal"),
    ("Shadow", "shadow"),
    ("SetShadow", "setShadow"),
    ("Depth", "depth"),
    ("SetDepth", "setDepth"),
    # Comment methods
    ("LeadingComments", "leadingComments"),
    ("SetLeadingComments", "setLeadingComments"),
    ("TrailingComments", "trailingComments"),
    ("SetTrailingComments", "setTrailingComments"),
    # Range / Position methods
    ("Begin", "begin"),
    ("SetBegin", "setBegin"),
    ("End", "end"),
    ("SetEnd", "setEnd"),
]


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

    def all_atoms(self) -> list[tuple[str, str]]:
        atoms: dict[str, str] = dict(BUILTIN_ATOMS)

        for ud in self.userdatas:
            for n in ud.nodes:
                atoms[n.name] = n.name

        for ud in self.userdatas:
            for cat in ud.categories:
                atoms[f"Category{cat}"] = cat[0].lower() + cat[1:]

        for atom, name, is_rw in self.all_property_atoms():
            atoms[atom] = name
            if is_rw:
                atoms[f"Set{atom}"] = f"set{atom}"

        return sorted(atoms.items(), key=lambda x: x[0])

    def atom_string_map(self) -> list[tuple[str, str]]:
        mapping: dict[str, str] = {}

        for ud in self.userdatas:
            for cat in ud.categories:
                mapping[cat[0].lower() + cat[1:]] = f"Category{cat}"

        for variant, string_val in BUILTIN_ATOMS:
            mapping[string_val] = variant

        for ud in self.userdatas:
            for n in ud.nodes:
                mapping[n.name] = n.name

        for atom, name, is_rw in self.all_property_atoms():
            mapping[name] = atom
            if is_rw:
                mapping[f"set{atom}"] = f"Set{atom}"

        return sorted(mapping.items(), key=lambda x: x[0])


AST_NODES_USERDATA = UserdataDef(
    name="AstNode",
    tag="TagNode",
    storage="pointer",
    is_const=False,
    output_file="AstNodes.gen.inl",
    register_func="registerNodeClass",
    class_info_name="AstNodeClassInfo",
    class_table_name="s_nodeClassTable",
    class_index_getter="T::ClassIndex()",
    headers=[
        "<vector>",
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
    class_info_name="CstNodeClassInfo",
    class_table_name="s_cstClassTable",
    class_index_getter="T::CstClassIndex()",
    headers=[
        "<vector>",
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


def _indent(level: int) -> str:
    return "    " * level


class CppNode(BaseModel):
    def render(self, indent: int = 0) -> str:
        raise NotImplementedError


class Raw(CppNode):
    text: str

    def render(self, indent: int = 0) -> str:
        if not self.text:
            return ""
        return f"{_indent(indent)}{self.text}"


class CppType(BaseModel):
    def format_decl(self, name: str = "") -> str:
        raise NotImplementedError

    def __str__(self) -> str:
        return self.format_decl()


class RawType(CppType):
    text: str

    def format_decl(self, name: str = "") -> str:
        if name:
            return f"{self.text} {name}"
        return self.text


class FuncPtr(CppType):
    return_type: CppType = RawType(text="void")
    params: list[CppType] = []

    def format_decl(self, name: str = "") -> str:
        param_str = ", ".join(p.format_decl() for p in self.params)
        ptr_part = f"*{name}" if name else "*"

        if isinstance(self.return_type, FuncPtr):
            return self.return_type.format_decl(f"({ptr_part})({param_str})")

        ret = self.return_type.format_decl()
        return f"{ret} ({ptr_part})({param_str})"


class Ptr(CppType):
    pointee: CppType
    is_const: bool = False

    def format_decl(self, name: str = "") -> str:
        const_prefix = "const " if self.is_const else ""
        decl = f"{const_prefix}{self.pointee.format_decl()}*"
        if name:
            return f"{decl} {name}"
        return decl


class Ref(CppType):
    referent: CppType
    is_const: bool = False

    def format_decl(self, name: str = "") -> str:
        const_prefix = "const " if self.is_const else ""
        decl = f"{const_prefix}{self.referent.format_decl()}&"
        if name:
            return f"{decl} {name}"
        return decl


class StructField(BaseModel):
    type: CppType
    name: str
    default: Optional[str] = None

    def render(self, indent: int = 0) -> str:
        decl = self.type.format_decl(self.name)
        if self.default is not None:
            return f"{_indent(indent)}{decl} = {self.default};"
        return f"{_indent(indent)}{decl};"


class Struct(CppNode):
    name: str
    fields: list[StructField] = []

    def render(self, indent: int = 0) -> str:
        lines = [
            f"{_indent(indent)}struct {self.name}",
            f"{_indent(indent)}{{",
        ]
        for f in self.fields:
            lines.append(f.render(indent + 1))
        lines.append(f"{_indent(indent)}}};")
        return "\n".join(lines)


class Variable(CppNode):
    type: CppType
    name: str
    is_static: bool = False
    is_const: bool = False
    init: Optional[str] = None

    def render(self, indent: int = 0) -> str:
        prefix = ""
        if self.is_static:
            prefix += "static "
        if self.is_const:
            prefix += "const "
        decl = self.type.format_decl(self.name)
        init_str = f" = {self.init}" if self.init is not None else ""
        return f"{_indent(indent)}{prefix}{decl}{init_str};"


class LambdaInitVar(CppNode):
    type: CppType
    name: str
    is_static: bool = True
    is_const: bool = True
    body: list[CppNode] = []

    def render(self, indent: int = 0) -> str:
        prefix = ""
        if self.is_static:
            prefix += "static "
        if self.is_const:
            prefix += "const "
        type_str = self.type.format_decl()
        lines = [
            f"{_indent(indent)}{prefix}{type_str} {self.name} = []() {{",
        ]
        if self.body:
            lines.append(CppGenerator.render_items(self.body, indent + 1))
        lines.append(f"{_indent(indent)}}}();")
        return "\n".join(lines)


class Param(BaseModel):
    type: CppType
    name: str = ""
    default: Optional[str] = None

    def render(self) -> str:
        decl = self.type.format_decl(self.name)
        if self.default is not None:
            return f"{decl} = {self.default}"
        return decl


class Block(CppNode):
    body: list[CppNode] = []

    def render(self, indent: int = 0) -> str:
        lines = [f"{_indent(indent)}{{"]
        if self.body:
            lines.append(CppGenerator.render_items(self.body, indent + 1))
        lines.append(f"{_indent(indent)}}}")
        return "\n".join(lines)


class SwitchCase(BaseModel):
    label: str
    body: list[CppNode] = []

    def render(self, indent: int = 0) -> str:
        lines = [f"{_indent(indent)}case {self.label}:"]
        for stmt in self.body:
            assert isinstance(
                stmt, CppNode
            ), f"Items in SwitchCase must be CppNode instances, got {type(stmt).__name__}: {stmt!r}"
            if isinstance(stmt, Block):
                lines.append(stmt.render(indent))
            else:
                lines.append(stmt.render(indent + 1))
        return "\n".join(lines)


class Switch(CppNode):
    expr: str
    cases: list[SwitchCase] = []
    default_body: Optional[list[CppNode]] = None

    def render(self, indent: int = 0) -> str:
        lines = [
            f"{_indent(indent)}switch ({self.expr})",
            f"{_indent(indent)}{{",
        ]
        for c in self.cases:
            lines.append(c.render(indent))
        if self.default_body is not None:
            lines.append(f"{_indent(indent)}default:")
            for stmt in self.default_body:
                assert isinstance(
                    stmt, CppNode
                ), f"Items in Switch default must be CppNode instances, got {type(stmt).__name__}: {stmt!r}"
                if isinstance(stmt, Block):
                    lines.append(stmt.render(indent))
                else:
                    lines.append(stmt.render(indent + 1))
        lines.append(f"{_indent(indent)}}}")
        return "\n".join(lines)


class If(CppNode):
    cond: str
    then_body: list[CppNode] = []
    else_body: list[CppNode] = []

    def render(self, indent: int = 0) -> str:
        lines = [
            f"{_indent(indent)}if ({self.cond})",
            f"{_indent(indent)}{{",
        ]
        if self.then_body:
            lines.append(CppGenerator.render_items(self.then_body, indent + 1))
        lines.append(f"{_indent(indent)}}}")
        if self.else_body:
            lines.append(f"{_indent(indent)}else")
            lines.append(f"{_indent(indent)}{{")
            lines.append(CppGenerator.render_items(self.else_body, indent + 1))
            lines.append(f"{_indent(indent)}}}")
        return "\n".join(lines)


class Function(CppNode):
    name: str
    return_type: CppType = RawType(text="void")
    params: list[Param] = []
    template_params: list[str] = []
    is_static: bool = False
    is_inline: bool = False
    multiline_params: bool = False
    body: list[CppNode] = []

    def render(self, indent: int = 0) -> str:
        lines: list[str] = []
        if self.template_params:
            tp = ", ".join(self.template_params)
            lines.append(f"{_indent(indent)}template<{tp}>")

        prefix = ""
        if self.is_static:
            prefix += "static "
        if self.is_inline:
            prefix += "inline "

        ret_decl = (
            self.return_type.format_decl(self.name)
            if isinstance(self.return_type, FuncPtr)
            else f"{self.return_type.format_decl()} {self.name}"
        )
        prefix += ret_decl

        if self.multiline_params and len(self.params) > 1:
            lines.append(f"{_indent(indent)}{prefix}(")
            for i, p in enumerate(self.params):
                comma = "," if i < len(self.params) - 1 else ""
                lines.append(f"{_indent(indent + 1)}{p.render()}{comma}")
            lines.append(f"{_indent(indent)})")
        else:
            p_str = ", ".join(p.render() for p in self.params)
            lines.append(f"{_indent(indent)}{prefix}({p_str})")

        lines.append(f"{_indent(indent)}{{")
        if self.body:
            lines.append(CppGenerator.render_items(self.body, indent + 1))
        lines.append(f"{_indent(indent)}}}")
        return "\n".join(lines)


class EnumVariant(BaseModel):
    name: str
    value: Optional[Union[int, str]] = None

    def render(self, indent: int = 0) -> str:
        if self.value is not None:
            return f"{_indent(indent)}{self.name} = {self.value},"
        return f"{_indent(indent)}{self.name},"


class Enum(CppNode):
    name: str
    is_class: bool = True
    underlying_type: Optional[str] = None
    variants: list[EnumVariant] = []

    def render(self, indent: int = 0) -> str:
        cls_str = "class " if self.is_class else ""
        type_str = f" : {self.underlying_type}" if self.underlying_type else ""
        lines = [
            f"{_indent(indent)}enum {cls_str}{self.name}{type_str}",
            f"{_indent(indent)}{{",
        ]
        for v in self.variants:
            lines.append(v.render(indent + 1))
        lines.append(f"{_indent(indent)}}};")
        return "\n".join(lines)


class Namespace(CppNode):
    name: str
    body: list[CppNode] = []

    def render(self, indent: int = 0) -> str:
        lines = [
            f"{_indent(indent)}namespace {self.name}",
            f"{_indent(indent)}{{",
            "",
        ]
        for item in self.body:
            assert isinstance(
                item, CppNode
            ), f"Items in Namespace must be CppNode instances, got {type(item).__name__}: {item!r}"
            lines.append(item.render(indent))
            lines.append("")
        lines.append(f"{_indent(indent)}}} // namespace {self.name}")
        return "\n".join(lines)


class TranslationUnit(CppNode):
    comment: str = "// Auto-generated by Reflect/generate_reflect_cpp.py. DO NOT EDIT!"
    pragma_once: bool = True
    includes: list[str] = []
    body: list[CppNode] = []

    def render(self, indent: int = 0) -> str:
        lines: list[str] = []
        if self.comment:
            lines.append(self.comment)
        if self.pragma_once:
            lines.append("#pragma once")
            lines.append("")
        if self.includes:
            for inc in self.includes:
                if inc.startswith("<") or inc.startswith('"'):
                    lines.append(f"#include {inc}")
                else:
                    lines.append(f'#include "{inc}"')
            lines.append("")
        for item in self.body:
            assert isinstance(
                item, CppNode
            ), f"Items in TranslationUnit must be CppNode instances, got {type(item).__name__}: {item!r}"
            lines.append(item.render(indent))
        return "\n".join(lines) + "\n"


class CppGenerator:
    def __init__(self, schema: ReflectSchema):
        self.schema = schema

    @staticmethod
    def render_items(items: list[CppNode], indent: int = 0) -> str:
        lines: list[str] = []
        for item in items:
            assert isinstance(
                item, CppNode
            ), f"Items must be CppNode instances (wrap raw strings in Raw(text=...)), got {type(item).__name__}: {item!r}"
            rendered = item.render(indent)
            if rendered != "":
                lines.append(rendered)
            elif isinstance(item, Raw) and item.text == "":
                lines.append("")
        return "\n".join(lines)

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
        const_prefix = "const " if ud.is_const else ""
        base_ptr = f"{const_prefix}Luau::{ud.name}*"
        handle_type = f"{ud.name}Data"

        ns_items: list[CppNode] = []

        method_handler_type = FuncPtr(
            return_type=RawType(text="bool"),
            params=[
                RawType(text="lua_State* L"),
                RawType(text=f"{handle_type}& handle"),
                RawType(text="ReflectAtom atom"),
            ],
        )
        prop_collector_type = FuncPtr(
            return_type=RawType(text="void"),
            params=[
                RawType(text="lua_State* L"),
                RawType(text=f"{handle_type}& handle"),
            ],
        )
        factory_type = FuncPtr(
            return_type=RawType(text=base_ptr),
            params=[
                RawType(text="Luau::Allocator& alloc"),
            ],
        )

        if ud.register_func and ud.class_info_name and ud.class_table_name:
            fields = [
                StructField(type=RawType(text="const char*"), name="kind", default="nullptr"),
            ]
            if ud.categories:
                fields.append(StructField(type=RawType(text="const char*"), name="category", default="nullptr"))
                fields.append(StructField(type=RawType(text="NodeCategory"), name="categoryEnum", default="NodeCategory::Unknown"))
            else:
                fields.append(StructField(type=RawType(text="const char*"), name="category", default='"generic"'))
            fields.append(
                StructField(
                    type=method_handler_type,
                    name="methodHandler",
                    default="nullptr",
                )
            )
            fields.append(
                StructField(
                    type=prop_collector_type,
                    name="propCollector",
                    default="nullptr",
                )
            )
            fields.append(
                StructField(
                    type=factory_type,
                    name="factory",
                    default="nullptr",
                )
            )
            if ud.categories:
                fields.append(StructField(type=RawType(text="bool"), name="canHoldComments", default="false"))

            ns_items.append(Struct(name=ud.class_info_name, fields=fields))
            ns_items.append(
                Variable(
                    type=RawType(text=f"std::vector<{ud.class_info_name}>"),
                    name=ud.class_table_name,
                    is_static=True,
                )
            )

            reg_params = [
                Param(type=RawType(text="const char*"), name="kind"),
            ]
            if ud.categories:
                reg_params.append(Param(type=RawType(text="NodeCategory"), name="category"))
            reg_params.extend([
                Param(
                    type=method_handler_type,
                    name="methodHandler",
                    default="nullptr",
                ),
                Param(
                    type=prop_collector_type,
                    name="propCollector",
                    default="nullptr",
                ),
                Param(
                    type=factory_type,
                    name="factory",
                    default="nullptr",
                ),
            ])

            reg_body: list[CppNode] = [
                Raw(text=f"int idx = {ud.class_index_getter};"),
            ]
            if ud.categories:
                reg_body.append(
                    If(
                        cond=f"size_t(idx) >= {ud.class_table_name}.size()",
                        then_body=[
                            Raw(
                                text=f'{ud.class_table_name}.resize(idx + 1, {ud.class_info_name}{{"{ud.name}", "unknown", NodeCategory::Unknown, nullptr, nullptr, nullptr, false}});'
                            )
                        ],
                    )
                )
                reg_body.append(Raw(text="bool canHoldComments = (category == NodeCategory::Stat);"))
                reg_body.append(
                    Raw(
                        text=f"{ud.class_table_name}[idx] = {ud.class_info_name}{{kind, categoryToString(category), category, methodHandler, propCollector, factory, canHoldComments}};"
                    )
                )
            else:
                reg_body.append(
                    If(
                        cond=f"size_t(idx) >= {ud.class_table_name}.size()",
                        then_body=[
                            Raw(
                                text=f'{ud.class_table_name}.resize(idx + 1, {ud.class_info_name}{{"{ud.name}", "generic", nullptr, nullptr, nullptr}});'
                            )
                        ],
                    )
                )
                reg_body.append(
                    Raw(
                        text=f'{ud.class_table_name}[idx] = {ud.class_info_name}{{kind, "generic", methodHandler, propCollector, factory}};'
                    )
                )

            ns_items.append(
                Function(
                    name=ud.register_func,
                    return_type=RawType(text="void"),
                    template_params=["typename T"],
                    is_static=True,
                    multiline_params=True,
                    params=reg_params,
                    body=reg_body,
                )
            )

        factory_cases = []
        for node in ud.nodes:
            if node.factory and node.factory.strategy == "alloc":
                args_str = ", ".join(node.factory.args)
                factory_cases.append(
                    SwitchCase(
                        label=f"ReflectAtom::{node.name}",
                        body=[Raw(text=f"return alloc.alloc<Luau::{node.name}>({args_str});")],
                    )
                )

        ns_items.append(
            Function(
                name=f"createDefault{ud.name}",
                return_type=RawType(text=base_ptr),
                params=[
                    Param(type=RawType(text="ReflectAtom"), name="atom"),
                    Param(type=RawType(text="Luau::Allocator&"), name="alloc"),
                ],
                body=[
                    Switch(expr="atom", cases=factory_cases, default_body=[Raw(text="return nullptr;")])
                ],
            )
        )

        for node in ud.nodes:
            class_name = node.name
            class_ptr = f"{const_prefix}Luau::{class_name}*"

            fields: list[FieldDef] = list(ud.base_fields)
            if node.base == "AstStat":
                fields.extend(ud.stat_fields)
            fields.extend(node.fields)

            args_str = ", ".join(node.factory.args) if node.factory and node.factory.args else ""
            ns_items.append(
                Function(
                    name=f"createDefault{class_name}",
                    return_type=RawType(text=base_ptr),
                    is_static=True,
                    params=[Param(type=RawType(text="Luau::Allocator&"), name="alloc")],
                    body=[Raw(text=f"return alloc.alloc<Luau::{class_name}>({args_str});")],
                )
            )

            if not fields:
                handler_body: list[CppNode] = [Raw(text="return false;")]
            else:
                method_cases = []
                for f in fields:
                    read_target = f.read_expr("n->")
                    if read_target:
                        method_cases.append(
                            SwitchCase(
                                label=f"ReflectAtom::{f.atom_name}",
                                body=[
                                    Raw(text=f"pushReflectValue(L, handle.doc, {read_target});"),
                                    Raw(text="return true;"),
                                ],
                            )
                        )
                    write_target = f.write_expr("n->")
                    if write_target:
                        method_cases.append(
                            SwitchCase(
                                label=f"ReflectAtom::{f.set_atom_name}",
                                body=[
                                    Raw(text=f"readReflectValue(L, handle.doc, 2, {write_target});"),
                                    Raw(text="lua_pushvalue(L, 1);"),
                                    Raw(text="return true;"),
                                ],
                            )
                        )
                handler_body = [
                    Raw(text=f"auto* n = static_cast<{class_ptr}>(handle.node);"),
                    Switch(expr="atom", cases=method_cases, default_body=[Raw(text="return false;")]),
                ]

            ns_items.append(
                Function(
                    name=f"handle{class_name}Methods",
                    return_type=RawType(text="bool"),
                    is_static=True,
                    params=[
                        Param(type=RawType(text="lua_State*"), name="L"),
                        Param(type=RawType(text=f"{handle_type}&"), name="handle"),
                        Param(type=RawType(text="ReflectAtom"), name="atom"),
                    ],
                    body=handler_body,
                )
            )

            collector_body: list[CppNode] = []
            if fields:
                collector_body.append(Raw(text=f"auto* n = static_cast<{class_ptr}>(handle.node);"))
                collector_body.append(Raw(text="(void)n;"))
                for f in fields:
                    read_target = f.read_expr("n->")
                    if read_target:
                        collector_body.append(Raw(text=f"pushReflectValue(L, handle.doc, {read_target});"))
                        collector_body.append(Raw(text=f'lua_setfield(L, -2, "{f.name}");'))

            ns_items.append(
                Function(
                    name=f"collect{class_name}Props",
                    return_type=RawType(text="void"),
                    is_static=True,
                    params=[
                        Param(type=RawType(text="lua_State*"), name="L"),
                        Param(type=RawType(text=f"{handle_type}&"), name="handle"),
                    ],
                    body=collector_body,
                )
            )

        reg_calls = []
        for node in ud.nodes:
            cat_arg = f"NodeCategory::{node.category}, " if node.category else ""
            reg_calls.append(
                f'{ud.register_func}<Luau::{node.name}>("{node.name}", {cat_arg}'
                f"handle{node.name}Methods, collect{node.name}Props, createDefault{node.name});"
            )

        ns_items.append(
            Function(
                name=f"register{ud.name}Classes",
                return_type=RawType(text="void"),
                is_static=True,
                body=[Raw(text=c) for c in reg_calls],
            )
        )

        tu = TranslationUnit(
            includes=ud.headers,
            body=[Namespace(name="Luau", body=ns_items)],
        )
        return tu.render()

    def _generate_union_inl(self, ud: UserdataDef) -> str:
        ns_items: list[CppNode] = []

        aux_factory_cases = []
        for node in ud.nodes:
            if node.factory and node.factory.strategy == "aux_union" and node.factory.inner_type:
                args_str = ", ".join(node.factory.args)
                body: list[CppNode] = [
                    Raw(text=f"out = AstAuxData(doc, {node.factory.inner_type}{{{args_str}}});"),
                    Raw(text="return true;"),
                ]
            elif node.factory and node.factory.custom_expr:
                body = [
                    Raw(text=f"out = {node.factory.custom_expr};"),
                    Raw(text="return true;"),
                ]
            else:
                body = [Raw(text="return false;")]
            aux_factory_cases.append(SwitchCase(label=f"ReflectAtom::{node.name}", body=body))

        ns_items.append(
            Function(
                name="createDefaultAstAux",
                return_type=RawType(text="bool"),
                params=[
                    Param(type=RawType(text="ReflectAtom"), name="atom"),
                    Param(type=RawType(text="const std::shared_ptr<AstDocumentState>&"), name="doc"),
                    Param(type=RawType(text="AstAuxData&"), name="out"),
                ],
                body=[
                    Switch(expr="atom", cases=aux_factory_cases, default_body=[Raw(text="return false;")])
                ],
            )
        )

        dispatch_cases = []
        for node in ud.nodes:
            enum_name = node.enum_name or f"Aux_{node.name}"
            case_body: list[CppNode] = []
            if node.union_member:
                case_body.append(Raw(text=f"auto& n = handle.{node.union_member};"))
                case_body.append(Raw(text="(void)n;"))

            atom_cases = []
            for f in node.fields:
                read_target = f.read_expr()
                if read_target:
                    atom_cases.append(
                        SwitchCase(
                            label=f"ReflectAtom::{f.atom_name}",
                            body=[
                                Raw(text=f"pushReflectValue(L, handle.doc, {read_target});"),
                                Raw(text="return true;"),
                            ],
                        )
                    )
                write_target = f.write_expr()
                if write_target:
                    atom_cases.append(
                        SwitchCase(
                            label=f"ReflectAtom::{f.set_atom_name}",
                            body=[
                                Raw(text=f"readReflectValue(L, handle.doc, 2, {write_target});"),
                                Raw(text="lua_pushvalue(L, 1);"),
                                Raw(text="return true;"),
                            ],
                        )
                    )

            case_body.append(Switch(expr="atom", cases=atom_cases, default_body=[Raw(text="return false;")]))
            dispatch_cases.append(SwitchCase(label=enum_name, body=[Block(body=case_body)]))

        ns_items.append(
            Function(
                name="dispatchAux",
                return_type=RawType(text="bool"),
                is_static=True,
                params=[
                    Param(type=RawType(text="lua_State*"), name="L"),
                    Param(type=RawType(text="AstAuxData&"), name="handle"),
                    Param(type=RawType(text="ReflectAtom"), name="atom"),
                ],
                body=[
                    Switch(expr="handle.kind", cases=dispatch_cases, default_body=[Raw(text="return false;")])
                ],
            )
        )

        collect_cases = []
        for node in ud.nodes:
            enum_name = node.enum_name or f"Aux_{node.name}"
            case_body = []
            if node.union_member:
                case_body.append(Raw(text=f"auto& n = handle.{node.union_member};"))
                case_body.append(Raw(text="(void)n;"))
            for f in node.fields:
                read_target = f.read_expr()
                if read_target:
                    case_body.append(Raw(text=f"pushReflectValue(L, handle.doc, {read_target});"))
                    case_body.append(Raw(text=f'lua_setfield(L, -2, "{f.name}");'))
            case_body.append(Raw(text="break;"))
            collect_cases.append(SwitchCase(label=enum_name, body=[Block(body=case_body)]))

        ns_items.append(
            Function(
                name="collectAuxProps",
                return_type=RawType(text="void"),
                is_static=True,
                params=[
                    Param(type=RawType(text="lua_State*"), name="L"),
                    Param(type=RawType(text="AstAuxData&"), name="handle"),
                ],
                body=[
                    Switch(expr="handle.kind", cases=collect_cases, default_body=[Raw(text="break;")])
                ],
            )
        )

        tu = TranslationUnit(
            includes=ud.headers,
            body=[Namespace(name="Luau", body=ns_items)],
        )
        return tu.render()

    def generate_atoms_h(self) -> str:
        atoms = self.schema.all_atoms()
        atom_map = self.schema.atom_string_map()

        ns_items: list[CppNode] = []

        variants = [
            EnumVariant(name="Unknown", value=-1),
            *(EnumVariant(name=v) for v, _ in atoms),
            EnumVariant(name="Count"),
        ]
        ns_items.append(
            Enum(name="ReflectAtom", is_class=True, underlying_type="int16_t", variants=variants)
        )

        atom_cases = [
            SwitchCase(label=f"ReflectAtom::{variant}", body=[Raw(text=f'return "{string_val}";')])
            for variant, string_val in atoms
        ]
        ns_items.append(
            Function(
                name="getAtomString",
                return_type=RawType(text="const char*"),
                is_inline=True,
                params=[Param(type=RawType(text="ReflectAtom"), name="atom")],
                body=[
                    Switch(expr="atom", cases=atom_cases, default_body=[Raw(text='return "";')])
                ],
            )
        )

        map_body: list[CppNode] = [
            Raw(text="DenseHashMap2<std::string_view, ReflectAtom> map;"),
            *(Raw(text=f'map["{string_val}"] = ReflectAtom::{variant};') for string_val, variant in atom_map),
            Raw(text="return map;"),
        ]
        ns_items.append(
            Function(
                name="resolveGlobalReflectAtom",
                return_type=RawType(text="ReflectAtom"),
                is_inline=True,
                params=[Param(type=RawType(text="std::string_view"), name="key")],
                body=[
                    LambdaInitVar(
                        type=RawType(text="DenseHashMap2<std::string_view, ReflectAtom>"),
                        name="s_atomMap",
                        body=map_body,
                    ),
                    Raw(text=""),
                    If(
                        cond="const ReflectAtom* atom = s_atomMap.find(key)",
                        then_body=[Raw(text="return *atom;")],
                    ),
                    Raw(text=""),
                    Raw(text="return ReflectAtom::Unknown;"),
                ],
            )
        )

        ns_items.append(
            Function(
                name="resolveReflectAtom",
                return_type=RawType(text="ReflectAtom"),
                is_inline=True,
                params=[
                    Param(type=RawType(text="int"), name="atomId"),
                    Param(type=RawType(text="std::string_view"), name="key"),
                ],
                body=[
                    If(
                        cond="atomId >= 0",
                        then_body=[
                            If(
                                cond="atomId < int(ReflectAtom::Count)",
                                then_body=[Raw(text="return ReflectAtom(atomId);")],
                            ),
                            Raw(text="return ReflectAtom::Unknown;"),
                        ],
                    ),
                    Raw(text="return resolveGlobalReflectAtom(key);"),
                ],
            )
        )

        ns_items.append(
            Function(
                name="resolveReflectAtom",
                return_type=RawType(text="ReflectAtom"),
                is_inline=True,
                params=[
                    Param(type=RawType(text="int"), name="atomId"),
                    Param(type=RawType(text="const char*"), name="str"),
                    Param(type=RawType(text="size_t"), name="len"),
                ],
                body=[
                    If(
                        cond="atomId >= 0",
                        then_body=[
                            If(
                                cond="atomId < int(ReflectAtom::Count)",
                                then_body=[Raw(text="return ReflectAtom(atomId);")],
                            ),
                            Raw(text="return ReflectAtom::Unknown;"),
                        ],
                    ),
                    Raw(text="return resolveGlobalReflectAtom(std::string_view(str, len));"),
                ],
            )
        )

        tu = TranslationUnit(
            includes=[
                '"Luau/DenseHash2.h"',
                "<cstdint>",
                "<string_view>",
            ],
            body=[Namespace(name="Luau", body=ns_items)],
        )
        return tu.render()


def main() -> int:
    base_dir = Path(__file__).resolve().parent
    src_dir = base_dir / "src"
    src_dir.mkdir(parents=True, exist_ok=True)
    inc_dir = base_dir / "include" / "Luau"
    inc_dir.mkdir(parents=True, exist_ok=True)

    generator = CppGenerator(GLOBAL_SCHEMA)

    for ud in GLOBAL_SCHEMA.userdatas:
        if ud.output_file:
            inl_path = src_dir / ud.output_file
            content = generator.generate_inl(ud)
            inl_path.write_text(content, encoding="utf-8")
            print(f"Generated: {inl_path}")

    atoms_path = inc_dir / "ReflectAtoms.gen.h"
    atoms_content = generator.generate_atoms_h()
    atoms_path.write_text(atoms_content, encoding="utf-8")
    print(f"Generated: {atoms_path}")

    print("\nSummary:")
    for ud in GLOBAL_SCHEMA.userdatas:
        print(f"  {ud.name:<10} : {len(ud.nodes)} nodes")
    print(f"  ReflectAtom: {len(GLOBAL_SCHEMA.all_atoms())} atoms")
    print("Code generation successful.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
