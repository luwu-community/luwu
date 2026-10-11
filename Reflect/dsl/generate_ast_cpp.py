from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, Literal, Optional, Union
from pydantic import BaseModel, field_validator

from cppir import *


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


class FieldType(BaseModel):
    kind: str
    arg: Optional[str] = None


STRUCTURAL_KINDS = {"node", "nodes", "opt_nodes", "local", "locals", "type_or_packs", "item", "item_ptr", "item_list", "class_members"}
WIDE_INTEGERS = {"size_t", "int64_t", "uint64_t"}


class FieldDef(BaseModel):
    name: str
    type: FieldType
    accessor: FieldAccessor = FieldAccessor()
    member: Optional[str] = None
    atom: Optional[str] = None
    set_atom: Optional[str] = None
    doc: Optional[str] = None
    label: Optional[str] = None
    nullable: bool = False
    companions: list[str] = []
    companion_of: Optional[str] = None

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

    @property
    def is_plain_member(self) -> bool:
        return not isinstance(self.accessor.read, str)

    def read_expr(self, prefix: str = "") -> Optional[str]:
        target = f"{prefix}{self.member_expr}" if prefix else self.member_expr
        return self.accessor.read_expr(target)

    def write_expr(self, prefix: str = "") -> Optional[str]:
        target = f"{prefix}{self.member_expr}" if prefix else self.member_expr
        return self.accessor.write_expr(target)


class F:
    @staticmethod
    def _field(kind: str, name: str, arg: Optional[str] = None, **kwargs: Any) -> FieldDef:
        return FieldDef(name=name, type=FieldType(kind=kind, arg=arg), **kwargs)

    @staticmethod
    def boolean(name: str, **kwargs: Any) -> FieldDef:
        return F._field("boolean", name, **kwargs)

    @staticmethod
    def number(name: str, **kwargs: Any) -> FieldDef:
        return F._field("number", name, **kwargs)

    @staticmethod
    def integer(name: str, cpp: str, **kwargs: Any) -> FieldDef:
        return F._field("integer", name, cpp, **kwargs)

    @staticmethod
    def char(name: str, **kwargs: Any) -> FieldDef:
        return F._field("char", name, **kwargs)

    @staticmethod
    def string(name: str, **kwargs: Any) -> FieldDef:
        return F._field("string", name, **kwargs)

    @staticmethod
    def strings(name: str, **kwargs: Any) -> FieldDef:
        return F._field("strings", name, **kwargs)

    @staticmethod
    def name(name: str, **kwargs: Any) -> FieldDef:
        return F._field("name", name, **kwargs)

    @staticmethod
    def opt_name(name: str, **kwargs: Any) -> FieldDef:
        return F._field("opt_name", name, **kwargs)

    @staticmethod
    def enum(name: str, cpp: str, **kwargs: Any) -> FieldDef:
        return F._field("enum", name, cpp, **kwargs)

    @staticmethod
    def location(name: str, **kwargs: Any) -> FieldDef:
        return F._field("location", name, **kwargs)

    @staticmethod
    def opt_location(name: str, **kwargs: Any) -> FieldDef:
        return F._field("opt_location", name, **kwargs)

    @staticmethod
    def arg_names(name: str, optional: bool, **kwargs: Any) -> FieldDef:
        return F._field("arg_names", name, "optional" if optional else "required", **kwargs)

    @staticmethod
    def arg_name_locations(name: str, optional: bool, **kwargs: Any) -> FieldDef:
        return F._field("arg_name_locations", name, "optional" if optional else "required", **kwargs)

    @staticmethod
    def position(name: str, **kwargs: Any) -> FieldDef:
        return F._field("position", name, **kwargs)

    @staticmethod
    def positions(name: str, **kwargs: Any) -> FieldDef:
        return F._field("positions", name, **kwargs)

    @staticmethod
    def node(name: str, cls: str, **kwargs: Any) -> FieldDef:
        return F._field("node", name, cls, **kwargs)

    @staticmethod
    def nodes(name: str, cls: str, **kwargs: Any) -> FieldDef:
        return F._field("nodes", name, cls, **kwargs)

    @staticmethod
    def opt_nodes(name: str, cls: str, **kwargs: Any) -> FieldDef:
        return F._field("opt_nodes", name, cls, nullable=True, **kwargs)

    @staticmethod
    def local(name: str, **kwargs: Any) -> FieldDef:
        return F._field("local", name, **kwargs)

    @staticmethod
    def locals(name: str, **kwargs: Any) -> FieldDef:
        return F._field("locals", name, **kwargs)

    @staticmethod
    def type_or_packs(name: str, **kwargs: Any) -> FieldDef:
        return F._field("type_or_packs", name, **kwargs)

    @staticmethod
    def item(name: str, item: str, **kwargs: Any) -> FieldDef:
        return F._field("item", name, item, **kwargs)

    @staticmethod
    def item_ptr(name: str, item: str, **kwargs: Any) -> FieldDef:
        return F._field("item_ptr", name, item, **kwargs)

    @staticmethod
    def item_list(name: str, item: str, **kwargs: Any) -> FieldDef:
        return F._field("item_list", name, item, **kwargs)

    @staticmethod
    def item_ptrs(name: str, item: str, **kwargs: Any) -> FieldDef:
        return F._field("item_ptrs", name, item, **kwargs)

    @staticmethod
    def class_members(name: str, **kwargs: Any) -> FieldDef:
        return F._field("class_members", name, **kwargs)

    @staticmethod
    def cst(name: str, **kwargs: Any) -> FieldDef:
        return F._field("cst", name, **kwargs)

    @staticmethod
    def opaque(name: str, cpp: str, **kwargs: Any) -> FieldDef:
        return F._field("opaque", name, cpp, **kwargs)


class FactoryDef(BaseModel):
    strategy: Literal["alloc", "empty"] = "alloc"
    args: list[str] = []
    requires_fflag: Optional[str] = None


class NodeDef(BaseModel):
    name: str
    category: Optional[str] = None
    base: Optional[str] = None
    factory: Optional[FactoryDef] = None
    fields: list[FieldDef] = []
    doc: Optional[str] = None
    validate_extra: list[str] = []

    cpp: Optional[str] = None
    display: Optional[str] = None

    @field_validator("fields", mode="before")
    @classmethod
    def coerce_fields(cls, v: Any) -> list[Any]:
        res = []
        if isinstance(v, list):
            for item in v:
                if isinstance(item, dict):
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
    storage: Literal["pointer", "plain"] = "pointer"
    family: str = "Node"
    category: Optional[str] = None
    is_const: bool = False
    output_file: str = ""
    register_func: Optional[str] = None
    class_table_name: Optional[str] = None
    class_index_getter: str = "T::ClassIndex()"
    mutable: bool = False
    fflags: list[str] = []
    headers: list[str] = []
    categories: list[str] = []
    base_fields: list[FieldDef] = []
    stat_fields: list[FieldDef] = []
    nodes: list[NodeDef] = []


BUILTIN_ATOMS: list[tuple[str, str]] = [
    # Special & Document & Allocator
    ("Id", "id"),
    ("Match", "match"),
    ("Root", "root"),
    ("Source", "source"),
    ("Prettyprint", "prettyprint"),
    ("Walk", "walk"),
    ("Errors", "errors"),
    ("Comments", "comments"),
    ("LineOffsets", "lineOffsets"),
    ("Hotcomments", "hotcomments"),
    ("Lines", "lines"),
    ("Properties", "properties"),
    ("Allocator", "allocator"),
    ("Parse", "parse"),
    ("Parseexpr", "parseexpr"),
    ("Defaultnode", "defaultnode"),
    # Node methods
    ("Kind", "kind"),
    ("Category", "category"),
    ("Children", "children"),
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
    tag="TagItem",
    storage="pointer",
    is_const=False,
    output_file="AstNodes.gen.inl",
    register_func="registerNodeClass",
    class_table_name="s_nodeClassTable",
    class_index_getter="T::ClassIndex()",
    mutable=True,
    fflags=["LuwuDefaultArguments"],
    headers=[
        "<vector>",
        '"Luau/Ast.h"',
        '"Luau/Cst.h"',
        '"Luau/ReflectCommon.h"',
        '"Luau/ReflectAstHandler.h"',
    ],
    categories=["Stat", "Expr", "Type", "TypePack", "Generic", "Attr"],
    base_fields=[
        F.location("origlocation", atom="OrigLocation", accessor=FieldAccessor(read="n->location", write=False)),
        F.cst("cst", atom="Cst", accessor=FieldAccessor(read="getNodeCst(handle, n)", write=False)),
        F.boolean("luwuOnly"),
    ],
    stat_fields=[F.boolean("hasSemicolon", atom="HasSemicolon", accessor=FieldAccessor())],
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
            fields=[F.boolean("hasEnd"), F.nodes("body", "AstStat")],
        ),
        NodeDef(
            name="AstStatIf",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "nullptr",
                    "nullptr",
                    "nullptr",
                    "std::nullopt",
                    "std::nullopt",
                    "Luau::Location()",
                ],
            ),
            fields=[
                F.node("condition", "AstExpr"),
                F.node("thenbody", "AstStatBlock", atom="ThenBody"),
                F.node("elsebody", "AstStat", atom="ElseBody", nullable=True),
                F.location("ifLocation", accessor=FieldAccessor(write=False)),
                F.opt_location("thenLocation", accessor=FieldAccessor(write=False)),
                F.opt_location("elseLocation", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="AstStatWhile",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "nullptr",
                    "nullptr",
                    "false",
                    "Luau::Location()",
                    "Luau::Location()",
                ],
            ),
            fields=[
                F.boolean("hasDo"), F.node("condition", "AstExpr"), F.node("body", "AstStatBlock"),
                F.location("whileLocation", accessor=FieldAccessor(write=False)),
                F.location("doLocation", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="AstStatRepeat",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "nullptr",
                    "nullptr",
                    "false",
                    "Luau::Location()",
                    "Luau::Location()",
                ],
            ),
            fields=[
                F.node("condition", "AstExpr"), F.node("body", "AstStatBlock"),
                F.location("repeatLocation", accessor=FieldAccessor(write=False)),
                F.location("untilLocation", accessor=FieldAccessor(write=False)),
            ],
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
                args=[
                    "Luau::Location()",
                    "Luau::AstArray<Luau::AstExpr*>{nullptr, 0}",
                    "Luau::Location()",
                ],
            ),
            fields=[
                F.nodes("list", "AstExpr"),
                F.location("returnLocation", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="AstStatExpr",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "nullptr"]),
            fields=[F.node("expr", "AstExpr")],
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
                F.boolean("isConst"),
                F.boolean("exported", member="isExported", atom="Exported"),
                F.locals("vars"),
                F.nodes("values", "AstExpr"),
                F.opt_location("keywordLocation", accessor=FieldAccessor(write=False)),
                F.opt_location("equalsSignLocation", accessor=FieldAccessor(write=False)),
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
                    "Luau::Location()",
                ],
            ),
            fields=[
                F.boolean("hasDo"),
                F.local("var"),
                F.node("from", "AstExpr"),
                F.node("to", "AstExpr"),
                F.node("step", "AstExpr", nullable=True),
                F.node("body", "AstStatBlock"),
                F.location("forLocation", accessor=FieldAccessor(write=False)),
                F.location("doLocation", accessor=FieldAccessor(write=False)),
            ],
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
                    "Luau::Location()",
                ],
            ),
            fields=[
                F.boolean("hasIn"),
                F.boolean("hasDo"),
                F.locals("vars"),
                F.nodes("values", "AstExpr"),
                F.node("body", "AstStatBlock"),
                F.location("forLocation", accessor=FieldAccessor(write=False)),
                F.location("inLocation", accessor=FieldAccessor(write=False)),
                F.location("doLocation", accessor=FieldAccessor(write=False)),
            ],
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
            fields=[F.nodes("vars", "AstExpr"), F.nodes("values", "AstExpr")],
        ),
        NodeDef(
            name="AstStatCompoundAssign",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstExprBinary::Op::Add", "nullptr", "nullptr"],
            ),
            fields=[F.enum("op", "AstExprBinary::Op"), F.node("var", "AstExpr"), F.node("value", "AstExpr")],
        ),
        NodeDef(
            name="AstStatFunction",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "nullptr",
                    "nullptr",
                    "Luau::Location()",
                ],
            ),
            fields=[
                F.node("name", "AstExpr"), F.node("func", "AstExprFunction"),
                F.location("functionLocation", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="AstStatLocalFunction",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "nullptr",
                    "nullptr",
                    "false",
                    "Luau::Position::missing()",
                    "Luau::Location()",
                    "Luau::Location()",
                ],
            ),
            fields=[
                F.boolean("isConst"), F.local("name"), F.node("func", "AstExprFunction"),
                F.position("constKeywordBegin", accessor=FieldAccessor(write=False)),
                F.location("keywordLocation", accessor=FieldAccessor(write=False)),
                F.location("functionLocation", accessor=FieldAccessor(write=False)),
            ],
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
                    "Luau::Location()",
                ],
            ),
            fields=[
                F.name("name"),
                F.boolean("exported"),
                F.node("type", "AstType"),
                F.nodes("generics", "AstGenericType"),
                F.nodes("genericPacks", "AstGenericTypePack"),
                F.location("nameLocation", accessor=FieldAccessor(write=False)),
                F.location("typeLocation", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="AstStatTypeFunction",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstName()", "Luau::Location()", "nullptr", "false", "false"],
            ),
            fields=[
                F.name("name"),
                F.boolean("exported"),
                F.boolean("hasErrors"),
                F.node("body", "AstExprFunction"),
                F.location("nameLocation", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="AstStatDeclareGlobal",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "Luau::AstName()",
                    "Luau::Location()",
                    "nullptr",
                    "Luau::Location()",
                ],
            ),
            fields=[
                F.name("name"), F.node("type", "AstType"),
                F.location("nameLocation", accessor=FieldAccessor(write=False)),
                F.location("declareLocation", accessor=FieldAccessor(write=False)),
            ],
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
                    "Luau::Location()",
                    "Luau::Location()",
                ],
            ),
            fields=[
                F.name("name"),
                F.boolean("vararg"),
                F.nodes("generics", "AstGenericType"),
                F.nodes("genericPacks", "AstGenericTypePack"),
                F.item("params", "AstTypeList"),
                F.node("returnTypes", "AstTypePack", member="retTypes", atom="ReturnTypes"),
                F.nodes("attributes", "AstAttr"),
                F.arg_names("paramNames", optional=False),
                F.arg_name_locations("paramNameLocations", optional=False, member="paramNames", accessor=FieldAccessor(write=False)),
                F.location("nameLocation", accessor=FieldAccessor(write=False)),
                F.location("varargLocation", accessor=FieldAccessor(write=False)),
                F.location("declareLocation", accessor=FieldAccessor(write=False)),
                F.location("functionLocation", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="AstStatClass",
            category="Stat",
            base="AstStat",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "nullptr",
                    "Luau::AstArray<Luau::AstClassMember>{nullptr, 0}",
                    "false",
                    "Luau::Location()",
                ],
                requires_fflag="LuwuClasses",
            ),
            fields=[
                F.boolean("exported"),
                F.local("name"),
                F.class_members("members"),
                F.nodes("generics", "AstGenericType"),
                F.nodes("genericPacks", "AstGenericTypePack"),
                F.item_ptr("primaryConstructor", "AstClassPrimaryConstructor", nullable=True),
                F.boolean("hasEnd"),
                F.location("keywordLocation", accessor=FieldAccessor(write=False)),
                F.boolean(
                    "explicitVisibility",
                    accessor=FieldAccessor(
                        read="hasExplicitVisibility(*handle.doc->arena, n)",
                        write="handle.doc->arena->explicitVisibility[n]",
                    ),
                ),
            ],
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
                    "Luau::Location()",
                    "Luau::Location()",
                    "std::nullopt",
                    "nullptr",
                ],
            ),
            fields=[
                F.name("name"),
                F.opt_name("superName"),
                F.item_list("props", "AstDeclaredExternTypeProperty"),
                F.item_ptr("indexer", "AstTableIndexer", nullable=True),
                F.nodes("generics", "AstGenericType"),
                F.nodes("genericPacks", "AstGenericTypePack"),
                F.location("declareLocation", accessor=FieldAccessor(write=False)),
                F.location("classLocation", accessor=FieldAccessor(write=False)),
                F.opt_location("extendsLocation", accessor=FieldAccessor(write=False)),
            ],
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
                F.integer("messageIndex", "unsigned int", accessor=FieldAccessor(write=False)),
                F.nodes("expressions", "AstExpr", accessor=FieldAccessor(write=False)),
                F.nodes("statements", "AstStat", accessor=FieldAccessor(write=False)),
            ],
        ),
        # Expressions
        NodeDef(
            name="AstExprGroup",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "nullptr"]),
            fields=[F.node("expr", "AstExpr")],
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
            fields=[F.boolean("value")],
        ),
        NodeDef(
            name="AstExprConstantNumber",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "0.0"]),
            fields=[
                F.number("value"),
                F.enum("parseResult", "ConstantNumberParseResult"),
            ],
        ),
        NodeDef(
            name="AstExprConstantInteger",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "int64_t(0)"]),
            fields=[
                F.integer("value", "int64_t"),
                F.enum("parseResult", "ConstantNumberParseResult"),
            ],
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
            fields=[F.string("value"), F.enum("quoteStyle", "AstExprConstantString::QuoteStyle")],
        ),
        NodeDef(
            name="AstExprLocal",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "nullptr", "false"]),
            fields=[F.boolean("upvalue"), F.local("local")],
        ),
        NodeDef(
            name="AstExprGlobal",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "Luau::AstName()"]),
            fields=[F.name("name")],
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
            fields=[
                F.boolean("self"),
                F.node("func", "AstExpr"),
                F.nodes("args", "AstExpr"),
                F.type_or_packs("typeArguments"),
                F.location("argLocation", accessor=FieldAccessor(write=False)),
            ],
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
            fields=[
                F.name("index"), F.char("op"), F.node("expr", "AstExpr"),
                F.location("indexLocation", accessor=FieldAccessor(write=False)),
                F.position("opPosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="AstExprIndexExpr",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "nullptr", "nullptr"]),
            fields=[F.node("expr", "AstExpr"), F.node("index", "AstExpr")],
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
                F.boolean("vararg"),
                F.name("debugname", atom="DebugName", nullable=True),
                F.locals("args", companions=["argsDefaults"]),
                F.node("body", "AstStatBlock"),
                F.nodes("generics", "AstGenericType"),
                F.nodes("genericPacks", "AstGenericTypePack"),
                F.node("returnAnnotation", "AstTypePack", nullable=True),
                F.nodes("attributes", "AstAttr"),
                F.opt_nodes("argsDefaults", "AstExpr", companion_of="args"),
                F.node("varargAnnotation", "AstTypePack", nullable=True),
                F.local("self", nullable=True),
                F.integer("functionDepth", "size_t"),
                F.location("varargLocation", accessor=FieldAccessor(write=False)),
                F.opt_location("argLocation", accessor=FieldAccessor(write=False)),
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
            fields=[F.item_list("items", "AstTableItem")],
        ),
        NodeDef(
            name="AstExprUnary",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstExprUnary::Op::Not", "nullptr"],
            ),
            fields=[F.enum("op", "AstExprUnary::Op"), F.node("expr", "AstExpr")],
        ),
        NodeDef(
            name="AstExprBinary",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstExprBinary::Op::Add", "nullptr", "nullptr"],
            ),
            fields=[F.enum("op", "AstExprBinary::Op"), F.node("left", "AstExpr"), F.node("right", "AstExpr")],
        ),
        NodeDef(
            name="AstExprTypeAssertion",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "nullptr", "nullptr"]),
            fields=[F.node("expr", "AstExpr"), F.node("annotation", "AstType")],
        ),
        NodeDef(
            name="AstExprIfElse",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=[
                    "Luau::Location()",
                    "nullptr",
                    "true",
                    "nullptr",
                    "true",
                    "nullptr",
                    "Luau::Location()",
                    "std::nullopt",
                    "std::nullopt",
                ],
            ),
            fields=[
                F.boolean("hasElse"),
                F.node("condition", "AstExpr"),
                F.node("trueExpr", "AstExpr"),
                F.node("falseExpr", "AstExpr"),
                F.boolean("hasThen"),
                F.location("ifLocation", accessor=FieldAccessor(write=False)),
                F.opt_location("thenLocation", accessor=FieldAccessor(write=False)),
                F.opt_location("elseLocation", accessor=FieldAccessor(write=False)),
            ],
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
            fields=[F.strings("strings"), F.nodes("expressions", "AstExpr")],
        ),
        NodeDef(
            name="AstExprInstantiate",
            category="Expr",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "nullptr", "Luau::AstArray<Luau::AstTypeOrPack>{nullptr, 0}"],
            ),
            fields=[F.node("expr", "AstExpr"), F.type_or_packs("typeArguments")],
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
                F.integer("messageIndex", "unsigned int", accessor=FieldAccessor(write=False)),
                F.nodes("expressions", "AstExpr", accessor=FieldAccessor(write=False)),
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
            fields=[
                F.name("name"),
                F.opt_name("prefix"),
                F.boolean("hasParameterList"),
                F.type_or_packs("parameters"),
                F.local("prefixLocal", nullable=True),
                F.opt_location("prefixLocation", accessor=FieldAccessor(write=False)),
                F.location("nameLocation", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="AstTypeTable",
            category="Type",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstArray<Luau::AstTableProp>{nullptr, 0}", "nullptr"],
            ),
            fields=[
                F.item_list("props", "AstTableProp"),
                F.item_ptr("indexer", "AstTableIndexer", nullable=True),
            ],
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
            fields=[
                F.nodes("generics", "AstGenericType"),
                F.nodes("genericPacks", "AstGenericTypePack"),
                F.item("argTypes", "AstTypeList"),
                F.node("returnTypes", "AstTypePack"),
                F.nodes("attributes", "AstAttr"),
                F.arg_names("argNames", optional=True),
                F.arg_name_locations("argNameLocations", optional=True, member="argNames", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="AstTypeTypeof",
            category="Type",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "nullptr"]),
            fields=[F.node("expr", "AstExpr")],
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
            fields=[F.nodes("types", "AstType")],
        ),
        NodeDef(
            name="AstTypeIntersection",
            category="Type",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstArray<Luau::AstType*>{nullptr, 0}"],
            ),
            fields=[F.nodes("types", "AstType")],
        ),
        NodeDef(
            name="AstTypeSingletonBool",
            category="Type",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "false"]),
            fields=[F.boolean("value")],
        ),
        NodeDef(
            name="AstTypeSingletonString",
            category="Type",
            base="AstNode",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Location()", "Luau::AstArray<char>{nullptr, 0}"],
            ),
            fields=[F.string("value")],
        ),
        NodeDef(
            name="AstTypeGroup",
            category="Type",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "nullptr"]),
            fields=[F.node("type", "AstType")],
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
                F.boolean("isMissing", accessor=FieldAccessor(write=False)),
                F.integer("messageIndex", "unsigned int", accessor=FieldAccessor(write=False)),
                F.nodes("types", "AstType", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="AstTypePackExplicit",
            category="TypePack",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "Luau::AstTypeList{}"]),
            fields=[F.item("typeList", "AstTypeList")],
        ),
        NodeDef(
            name="AstTypePackVariadic",
            category="TypePack",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "nullptr"]),
            fields=[F.node("variadicType", "AstType")],
        ),
        NodeDef(
            name="AstTypePackGeneric",
            category="TypePack",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "Luau::AstName()"]),
            fields=[F.name("name", member="genericName")],
        ),
        NodeDef(
            name="AstGenericType",
            category="Generic",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "Luau::AstName()", "nullptr"]),
            fields=[F.name("name"), F.node("type", "AstType", member="defaultValue", nullable=True)],
        ),
        NodeDef(
            name="AstGenericTypePack",
            category="Generic",
            base="AstNode",
            factory=FactoryDef(strategy="alloc", args=["Luau::Location()", "Luau::AstName()", "nullptr"]),
            fields=[F.name("name"), F.node("type", "AstTypePack", member="defaultValue", nullable=True)],
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
            fields=[F.enum("type", "AstAttr::Type"), F.name("name"), F.nodes("args", "AstExpr")],
        ),
    ],
)


CST_NODES_USERDATA = UserdataDef(
    name="CstNode",
    tag="TagItem",
    storage="pointer",
    family="Cst",
    category="cst",
    is_const=True,
    output_file="CstNodes.gen.inl",
    register_func="registerCstNodeClass",
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
            factory=FactoryDef(strategy="alloc", args=["false"], requires_fflag="LuauCstAttr"),
            fields=[F.boolean("hasAt", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstParametrizedAttr",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Position::missing()", "Luau::Position::missing()", "Luau::AstArray<Luau::Position>{nullptr, 0}"],
                requires_fflag="LuauCstAttr",
            ),
            fields=[
                F.position("openParenPosition", accessor=FieldAccessor(write=False)),
                F.position("closeParenPosition", accessor=FieldAccessor(write=False)),
                F.positions("argsCommaPositions", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstExprGroup",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[F.position("closePosition", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstExprConstantNumber",
            factory=FactoryDef(strategy="alloc", args=["Luau::AstArray<char>{nullptr, 0}"]),
            fields=[F.string("value", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstExprConstantInteger",
            factory=FactoryDef(strategy="alloc", args=["Luau::AstArray<char>{nullptr, 0}"]),
            fields=[F.string("value", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstExprConstantString",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::AstArray<char>{nullptr, 0}", "Luau::CstExprConstantString::QuoteStyle::QuotedSingle", "0"],
            ),
            fields=[
                F.enum("quoteStyle", "CstExprConstantString::QuoteStyle", accessor=FieldAccessor(write=False)),
                F.integer("blockDepth", "unsigned int", accessor=FieldAccessor(write=False)),
                F.string("sourceString", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstExprCall",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Position::missing()", "Luau::Position::missing()", "Luau::AstArray<Luau::Position>{nullptr, 0}"],
            ),
            fields=[
                F.position("openParens", accessor=FieldAccessor(write=False)),
                F.position("closeParens", accessor=FieldAccessor(write=False)),
                F.positions("commaPositions", accessor=FieldAccessor(write=False)),
                F.item_ptr("explicitTypes", "CstTypeInstantiation", accessor=FieldAccessor(write=False), nullable=True),
            ],
        ),
        NodeDef(
            name="CstExprIndexExpr",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Position::missing()", "Luau::Position::missing()"],
            ),
            fields=[
                F.position("openBracketPosition", accessor=FieldAccessor(write=False)),
                F.position("closeBracketPosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstExprFunction",
            factory=FactoryDef(strategy="empty", args=[]),
            fields=[
                F.position("functionKeywordPosition", accessor=FieldAccessor(write=False)),
                F.position("openGenericsPosition", accessor=FieldAccessor(write=False)),
                F.positions("genericsCommaPositions", accessor=FieldAccessor(write=False)),
                F.position("closeGenericsPosition", accessor=FieldAccessor(write=False)),
                F.positions("argsAnnotationColonPositions", accessor=FieldAccessor(write=False)),
                F.positions("argsCommaPositions", accessor=FieldAccessor(write=False)),
                F.position("varargAnnotationColonPosition", accessor=FieldAccessor(write=False)),
                F.position("returnSpecifierPosition", accessor=FieldAccessor(write=False)),
                F.item_ptrs("attrLists", "CstAttrList", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstExprTable",
            factory=FactoryDef(strategy="alloc", args=["Luau::AstArray<Luau::CstExprTable::Item>{nullptr, 0}"]),
            fields=[F.item_list("items", "CstTableItem", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstExprOp",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[F.position("opPosition", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstExprTypeAssertion",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[F.position("opPosition", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstExprIfElse",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Position::missing()", "Luau::Position::missing()", "false"],
            ),
            fields=[
                F.boolean("isElseIf", accessor=FieldAccessor(write=False)),
                F.position("thenPosition", accessor=FieldAccessor(write=False)),
                F.position("elsePosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstExprInterpString",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::AstArray<Luau::AstArray<char>>{nullptr, 0}", "Luau::AstArray<Luau::Position>{nullptr, 0}"],
            ),
            fields=[
                F.positions("commaPositions", member="stringPositions", accessor=FieldAccessor(write=False)),
                F.strings("sourceStrings", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstExprExplicitTypeInstantiation",
            factory=FactoryDef(strategy="empty", args=["Luau::CstTypeInstantiation{}"]),
            fields=[F.item("instantiation", "CstTypeInstantiation", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstStatDo",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()", "Luau::Position::missing()"]),
            fields=[
                F.position("statsStartPosition", accessor=FieldAccessor(write=False)),
                F.position("endPosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstStatRepeat",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[F.position("untilPosition", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstStatReturn",
            factory=FactoryDef(strategy="alloc", args=["Luau::AstArray<Luau::Position>{nullptr, 0}"]),
            fields=[F.positions("commaPositions", accessor=FieldAccessor(write=False))],
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
                F.positions("varsAnnotationColonPositions", accessor=FieldAccessor(write=False)),
                F.positions("varsCommaPositions", accessor=FieldAccessor(write=False)),
                F.positions("valuesCommaPositions", accessor=FieldAccessor(write=False)),
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
                F.position("annotationColonPosition", accessor=FieldAccessor(write=False)),
                F.position("equalsPosition", accessor=FieldAccessor(write=False)),
                F.position("endCommaPosition", accessor=FieldAccessor(write=False)),
                F.position("stepCommaPosition", accessor=FieldAccessor(write=False)),
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
                F.positions("varsAnnotationColonPositions", accessor=FieldAccessor(write=False)),
                F.positions("varsCommaPositions", accessor=FieldAccessor(write=False)),
                F.positions("valuesCommaPositions", accessor=FieldAccessor(write=False)),
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
                F.positions("varsCommaPositions", accessor=FieldAccessor(write=False)),
                F.position("equalsPosition", accessor=FieldAccessor(write=False)),
                F.positions("valuesCommaPositions", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstStatCompoundAssign",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[F.position("opPosition", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstStatFunction",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[
                F.position("functionKeywordPosition", accessor=FieldAccessor(write=False)),
                F.item_ptrs("attrLists", "CstAttrList", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstStatLocalFunction",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()", "Luau::Position::missing()"]),
            fields=[
                F.position("localKeywordPosition", accessor=FieldAccessor(write=False)),
                F.position("functionKeywordPosition", accessor=FieldAccessor(write=False)),
                F.item_ptrs("attrLists", "CstAttrList", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstGenericType",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[F.position("defaultEqualsPosition", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstGenericTypePack",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()", "Luau::Position::missing()"]),
            fields=[
                F.position("ellipsisPosition", accessor=FieldAccessor(write=False)),
                F.position("defaultEqualsPosition", accessor=FieldAccessor(write=False)),
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
                F.position("typeKeywordPosition", accessor=FieldAccessor(write=False)),
                F.position("genericsOpenPosition", accessor=FieldAccessor(write=False)),
                F.positions("genericsCommaPositions", accessor=FieldAccessor(write=False)),
                F.position("genericsClosePosition", accessor=FieldAccessor(write=False)),
                F.position("equalsPosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstStatTypeFunction",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()", "Luau::Position::missing()"]),
            fields=[
                F.position("typeKeywordPosition", accessor=FieldAccessor(write=False)),
                F.position("functionKeywordPosition", accessor=FieldAccessor(write=False)),
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
                F.position("prefixPointPosition", accessor=FieldAccessor(write=False)),
                F.position("openParametersPosition", accessor=FieldAccessor(write=False)),
                F.positions("parametersCommaPositions", accessor=FieldAccessor(write=False)),
                F.position("closeParametersPosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstTypeTable",
            factory=FactoryDef(strategy="alloc", args=["Luau::AstArray<Luau::CstTypeTable::Item>{nullptr, 0}", "false"]),
            fields=[
                F.boolean("isArray", accessor=FieldAccessor(write=False)),
                F.item_list("items", "CstTypeTableItem", accessor=FieldAccessor(write=False)),
            ],
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
                F.position("openGenericsPosition", accessor=FieldAccessor(write=False)),
                F.positions("genericsCommaPositions", accessor=FieldAccessor(write=False)),
                F.position("closeGenericsPosition", accessor=FieldAccessor(write=False)),
                F.position("openArgsPosition", accessor=FieldAccessor(write=False)),
                F.positions("argumentNameColonPositions", accessor=FieldAccessor(write=False)),
                F.positions("argumentsCommaPositions", accessor=FieldAccessor(write=False)),
                F.position("closeArgsPosition", accessor=FieldAccessor(write=False)),
                F.position("returnArrowPosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstTypeTypeof",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()", "Luau::Position::missing()"]),
            fields=[
                F.position("openPosition", accessor=FieldAccessor(write=False)),
                F.position("closePosition", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstTypeUnion",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()", "Luau::AstArray<Luau::Position>{nullptr, 0}"]),
            fields=[
                F.position("leadingPosition", accessor=FieldAccessor(write=False)),
                F.positions("separatorPositions", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstTypeIntersection",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()", "Luau::AstArray<Luau::Position>{nullptr, 0}"]),
            fields=[
                F.position("leadingPosition", accessor=FieldAccessor(write=False)),
                F.positions("separatorPositions", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstTypeSingletonString",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::AstArray<char>{nullptr, 0}", "Luau::CstExprConstantString::QuoteStyle::QuotedSingle", "0"],
            ),
            fields=[
                F.enum("quoteStyle", "CstExprConstantString::QuoteStyle", accessor=FieldAccessor(write=False)),
                F.integer("blockDepth", "unsigned int", accessor=FieldAccessor(write=False)),
                F.string("sourceString", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstTypeGroup",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[F.position("closePosition", accessor=FieldAccessor(write=False))],
        ),
        NodeDef(
            name="CstTypePackExplicit",
            factory=FactoryDef(
                strategy="alloc",
                args=["Luau::Position::missing()", "Luau::Position::missing()", "Luau::AstArray<Luau::Position>{nullptr, 0}"],
            ),
            fields=[
                F.position("openParenthesesPosition", accessor=FieldAccessor(write=False)),
                F.position("closeParenthesesPosition", accessor=FieldAccessor(write=False)),
                F.positions("commaPositions", accessor=FieldAccessor(write=False)),
            ],
        ),
        NodeDef(
            name="CstTypePackGeneric",
            factory=FactoryDef(strategy="alloc", args=["Luau::Position::missing()"]),
            fields=[F.position("ellipsisPosition", accessor=FieldAccessor(write=False))],
        ),
    ],
)


PLAIN_ITEMS: list[NodeDef] = [
    NodeDef(
        name="AstTableProp",
        category="aux",
        cpp="Luau::AstTableProp",
        display="name",
        factory=FactoryDef(
            args=[
                "Luau::AstName()",
                "Luau::Location()",
                "nullptr",
                "Luau::AstTableAccess::ReadWrite",
                "std::nullopt",
            ],
        ),
        fields=[
            F.name("name", member="n.name"),
            F.location("origlocation", accessor=FieldAccessor(write=False), member="n.location"),
            F.node("type", "AstType", member="n.type"),
            F.enum("access", "AstTableAccess", member="n.access"),
            F.opt_location("accessLocation", accessor=FieldAccessor(write=False), member="n.accessLocation"),
        ],
    ),
    NodeDef(
        name="AstTableIndexer",
        category="aux",
        cpp="Luau::AstTableIndexer",
        factory=FactoryDef(
            args=[
                "nullptr",
                "nullptr",
                "Luau::Location()",
                "Luau::AstTableAccess::ReadWrite",
                "std::nullopt",
            ],
        ),
        fields=[
            F.location("origlocation", accessor=FieldAccessor(write=False), member="n.location"),
            F.node("indexType", "AstType", member="n.indexType", label="index type"),
            F.node("resultType", "AstType", member="n.resultType", label="result type"),
            F.enum("access", "AstTableAccess", member="n.access"),
            F.opt_location("accessLocation", accessor=FieldAccessor(write=False), member="n.accessLocation"),
        ],
    ),
    NodeDef(
        name="AstDeclaredExternTypeProperty",
        category="aux",
        cpp="Luau::AstDeclaredExternTypeProperty",
        display="name",
        factory=FactoryDef(
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
            F.name("name", member="n.name"),
            F.location("origlocation", accessor=FieldAccessor(write=False), member="n.location"),
            F.node("type", "AstType", member="n.ty"),
            F.boolean("isMethod", member="n.isMethod"),
            F.enum("access", "AstTableAccess", member="n.access"),
            F.location("nameLocation", accessor=FieldAccessor(write=False), member="n.nameLocation"),
        ],
    ),
    NodeDef(
        name="AstClassProperty",
        category="aux",
        cpp="Luau::AstClassProperty",
        display="name",
        factory=FactoryDef(
            args=[
                "std::nullopt",
                "Luau::AstClassMemberVisibility::Public",
                "Luau::AstName()",
                "Luau::Location()",
                "std::nullopt",
                "nullptr",
            ],
        ),
        fields=[
            F.name("name", member="n.name"),
            F.node("type", "AstType", member="n.ty", nullable=True),
            F.node("defaultValue", "AstExpr", member="n.defaultValue", nullable=True),
            F.enum("visibility", "AstClassMemberVisibility", member="n.visibility"),
            F.boolean("isConst", member="n.isConst"),
            F.boolean("hasSemicolon", member="n.hasSemicolon"),
            F.opt_location("qualifierLocation", accessor=FieldAccessor(write=False), member="n.qualifierLocation"),
            F.location("nameLocation", accessor=FieldAccessor(write=False), member="n.nameLocation"),
            F.opt_location("typeColonLocation", accessor=FieldAccessor(write=False), member="n.typeColonLocation"),
            F.opt_location("constLocation", accessor=FieldAccessor(write=False), member="n.constLocation"),
            F.opt_location("equalsLocation", accessor=FieldAccessor(write=False), member="n.equalsLocation"),
        ],
    ),
    NodeDef(
        name="AstClassMethod",
        category="aux",
        cpp="Luau::AstClassMethod",
        display="name",
        factory=FactoryDef(
            args=[
                "std::nullopt",
                "Luau::AstClassMemberVisibility::Public",
                "Luau::Location()",
                "Luau::AstName()",
                "Luau::Location()",
                "nullptr",
            ],
        ),
        fields=[
            F.name("name", member="n.functionName"),
            F.node("func", "AstExprFunction", member="n.function", label="function"),
            F.enum("visibility", "AstClassMemberVisibility", member="n.visibility"),
            F.boolean("hasSemicolon", member="n.hasSemicolon"),
            F.opt_location("qualifierLocation", accessor=FieldAccessor(write=False), member="n.qualifierLocation"),
            F.location("keywordLocation", accessor=FieldAccessor(write=False), member="n.keywordLocation"),
            F.location("nameLocation", accessor=FieldAccessor(write=False), member="n.nameLocation"),
        ],
    ),
    NodeDef(
        name="AstClassPrimaryConstructor",
        category="aux",
        cpp="Luau::AstClassPrimaryConstructor",
        factory=FactoryDef(
            args=[
                "std::nullopt",
                "Luau::AstClassMemberVisibility::Public",
                "Luau::AstArray<Luau::AstLocal*>{nullptr, 0}",
                "Luau::AstArray<Luau::AstExpr*>{nullptr, 0}",
                "Luau::AstArray<Luau::AstClassPrimaryConstructorParamQualifiers>{nullptr, 0}",
                "Luau::Location()",
            ],
        ),
        fields=[
            F.enum("visibility", "AstClassMemberVisibility", member="n.visibility"),
            F.locals("args", member="n.args", companions=["argsDefaults", "argsQualifiers"]),
            F.opt_nodes("argsDefaults", "AstExpr", member="n.argsDefaults", companion_of="args"),
            F.item_list(
                "argsQualifiers",
                "AstClassPrimaryConstructorParamQualifiers",
                member="n.argsQualifiers",
                companion_of="args",
            ),
            F.opt_location("qualifierLocation", accessor=FieldAccessor(write=False), member="n.qualifierLocation"),
            F.location("argLocation", accessor=FieldAccessor(write=False), member="n.argLocation"),
            F.boolean(
                "explicitVisibility",
                accessor=FieldAccessor(
                    read="hasExplicitVisibility(*handle.doc->arena, &n)",
                    write="handle.doc->arena->explicitVisibility[&n]",
                ),
            ),
        ],
    ),
    NodeDef(
        name="AstClassPrimaryConstructorParamQualifiers",
        category="aux",
        cpp="Luau::AstClassPrimaryConstructorParamQualifiers",
        factory=FactoryDef(
            args=[
                "std::nullopt",
                "Luau::AstClassMemberVisibility::Public",
                "std::nullopt",
                "false",
            ],
        ),
        fields=[
            F.enum("visibility", "AstClassMemberVisibility", member="n.visibility"),
            F.boolean("isConst", member="n.isConst"),
            F.opt_location("qualifierLocation", accessor=FieldAccessor(write=False), member="n.qualifierLocation"),
            F.opt_location("constLocation", accessor=FieldAccessor(write=False), member="n.constLocation"),
            F.boolean(
                "explicitVisibility",
                accessor=FieldAccessor(
                    read="hasExplicitVisibility(*handle.doc->arena, &n)",
                    write="handle.doc->arena->explicitVisibility[&n]",
                ),
            ),
        ],
    ),
    NodeDef(
        name="AstComment",
        category="aux",
        cpp="ReflectComment",
        display="text",
        factory=FactoryDef(
            args=["Luau::Lexeme::Type::Comment", 'copyToArena(*doc->allocator(), "--")', "Luau::Location()"],
        ),
        fields=[
            F.enum("type", "Lexeme::Type", member="n.type"),
            F.string("text", member="n.text"),
            F.location("origlocation", accessor=FieldAccessor(write=False), member="n.location"),
        ],
    ),
    NodeDef(
        name="AstTableItem",
        category="aux",
        cpp="Luau::AstExprTable::Item",
        factory=FactoryDef(
            args=[
                "Luau::AstExprTable::Item::Kind::List",
                "nullptr",
                "nullptr",
            ],
        ),
        fields=[
            F.node("key", "AstExpr", member="n.key", nullable=True),
            F.node("value", "AstExpr", member="n.value"),
            F.enum("itemKind", "AstExprTable::Item::Kind", member="n.kind"),
        ],
        validate_extra=[
            'if (item.kind != Luau::AstExprTable::Item::Kind::List && !item.key) v.fail({field}, {index}, "has no key");',
            'if (v.forPrint && item.kind == Luau::AstExprTable::Item::Kind::Record && !item.key->is<Luau::AstExprConstantString>()) v.fail({field}, {index}, "is a record item whose key is not a string constant");',
        ],
    ),
    NodeDef(
        name="AstTypeList",
        category="aux",
        cpp="Luau::AstTypeList",
        factory=FactoryDef(
            args=[
                "Luau::AstArray<Luau::AstType*>{nullptr, 0}",
                "nullptr",
            ],
        ),
        fields=[
            F.nodes("types", "AstType", member="n.types"),
            F.node("tailType", "AstTypePack", member="n.tailType", nullable=True),
        ],
    ),
    NodeDef(
        name="CstTableItem",
        category="aux",
        cpp="Luau::CstExprTable::Item",
        factory=FactoryDef(
            args=[
                "Luau::Position::missing()",
                "Luau::Position::missing()",
                "Luau::Position::missing()",
                "Luau::CstExprTable::Separator::Missing",
                "Luau::Position::missing()",
            ],
        ),
        fields=[
            F.position("indexerOpenPosition", accessor=FieldAccessor(write=False), member="n.indexerOpenPosition"),
            F.position("indexerClosePosition", accessor=FieldAccessor(write=False), member="n.indexerClosePosition"),
            F.position("equalsPosition", accessor=FieldAccessor(write=False), member="n.equalsPosition"),
            F.position("separatorPosition", accessor=FieldAccessor(write=False), member="n.separatorPosition"),
            F.enum("separator", "CstExprTable::Separator", accessor=FieldAccessor(write=False), member="n.separator"),
        ],
    ),
    NodeDef(
        name="CstAttrList",
        category="aux",
        cpp="Luau::CstAttrList",
        factory=FactoryDef(
            args=[
                "Luau::Position::missing()",
                "Luau::Position::missing()",
                "Luau::AstArray<Luau::Position>{nullptr, 0}",
            ],
            requires_fflag="LuauCstAttr",
        ),
        fields=[
            F.position("atBracketPosition", accessor=FieldAccessor(write=False), member="n.atBracketPosition"),
            F.position("closeBracketPosition", accessor=FieldAccessor(write=False), member="n.closeBracketPosition"),
            F.positions("commaPositions", accessor=FieldAccessor(write=False), member="n.commaPositions"),
        ],
    ),
    NodeDef(
        name="CstTypeInstantiation",
        category="aux",
        cpp="Luau::CstTypeInstantiation",
        factory=FactoryDef(args=[]),
        fields=[
            F.position("leftArrow1Position", accessor=FieldAccessor(write=False), member="n.leftArrow1Position"),
            F.position("leftArrow2Position", accessor=FieldAccessor(write=False), member="n.leftArrow2Position"),
            F.positions("commaPositions", accessor=FieldAccessor(write=False), member="n.commaPositions"),
            F.position("rightArrow1Position", accessor=FieldAccessor(write=False), member="n.rightArrow1Position"),
            F.position("rightArrow2Position", accessor=FieldAccessor(write=False), member="n.rightArrow2Position"),
        ],
    ),
    NodeDef(
        name="CstTypeTableItem",
        category="aux",
        cpp="Luau::CstTypeTable::Item",
        factory=FactoryDef(
            args=[
                "Luau::CstTypeTable::Item::Kind::Property",
                "Luau::Position::missing()",
                "Luau::Position::missing()",
                "Luau::Position::missing()",
                "Luau::CstExprTable::Separator::Missing",
                "Luau::Position::missing()",
            ],
        ),
        fields=[
            F.enum("itemKind", "CstTypeTable::Item::Kind", accessor=FieldAccessor(write=False), member="n.kind"),
            F.position("indexerOpenPosition", accessor=FieldAccessor(write=False), member="n.indexerOpenPosition"),
            F.position("indexerClosePosition", accessor=FieldAccessor(write=False), member="n.indexerClosePosition"),
            F.position("colonPosition", accessor=FieldAccessor(write=False), member="n.colonPosition"),
            F.enum("separator", "CstExprTable::Separator", accessor=FieldAccessor(write=False), member="n.separator"),
            F.position("separatorPosition", accessor=FieldAccessor(write=False), member="n.separatorPosition"),
            F.cst(
                "stringInfo",
                accessor=FieldAccessor(read="static_cast<const Luau::CstNode*>(n.stringInfo)", write=False),
            ),
            F.position("stringPosition", accessor=FieldAccessor(write=False), member="n.stringPosition"),
        ],
    ),
    NodeDef(
        name="AstLocal",
        category="local",
        cpp="Luau::AstLocal",
        display="name",
        factory=FactoryDef(
            args=["Luau::AstName()", "Luau::Location()", "nullptr", "0", "0", "nullptr", "false"],
        ),
        fields=[
            F.name("name", member="n.name"),
            F.location("origlocation", member="n.location", accessor=FieldAccessor(write=False)),
            F.local("shadow", member="n.shadow", nullable=True),
            F.integer("depth", "size_t", member="n.functionDepth"),
            F.boolean("isConst", member="n.isConst"),
            F.boolean("exported", member="n.isExported"),
            F.node("annotation", "AstType", member="n.annotation", nullable=True),
            F.integer("loopDepth", "size_t", member="n.loopDepth"),
        ],
    ),
]


def plain_userdata(node: NodeDef) -> UserdataDef:
    return UserdataDef(
        name=node.name,
        tag="TagItem",
        storage="plain",
        family="Plain",
        category=node.category,
        mutable=True,
        output_file=f"{node.name}.gen.inl",
        headers=[
            '"Luau/Ast.h"',
            '"Luau/Cst.h"',
            '"Luau/ReflectCommon.h"',
            '"Luau/ReflectAstHandler.h"',
        ],
        nodes=[node],
    )


GLOBAL_SCHEMA = ReflectSchema(
    userdatas=[AST_NODES_USERDATA, CST_NODES_USERDATA, *[plain_userdata(node) for node in PLAIN_ITEMS]]
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
        elif ud.storage == "plain":
            return self._generate_plain_inl(ud)
        raise ValueError(f"Unsupported storage type: {ud.storage}")

    def _plain_items(self) -> list[NodeDef]:
        return [ud.nodes[0] for ud in self.schema.userdatas if ud.storage == "plain"]

    def _item(self, name: str) -> NodeDef:
        for node in self._plain_items():
            if node.name == name:
                return node
        raise ValueError(f"Unknown item type: {name}")

    def _item_cpp(self, name: str) -> str:
        node = self._item(name)
        if not node.cpp:
            raise ValueError(f"Item type has no C++ type: {name}")
        return node.cpp

    def _cpp_type(self, t: FieldType) -> str:
        fixed = {
            "boolean": "bool",
            "number": "double",
            "char": "char",
            "string": "Luau::AstArray<char>",
            "strings": "Luau::AstArray<Luau::AstArray<char>>",
            "name": "Luau::AstName",
            "opt_name": "std::optional<Luau::AstName>",
            "location": "Luau::Location",
            "opt_location": "std::optional<Luau::Location>",
            "position": "Luau::Position",
            "positions": "Luau::AstArray<Luau::Position>",
            "local": "Luau::AstLocal*",
            "locals": "Luau::AstArray<Luau::AstLocal*>",
            "type_or_packs": "Luau::AstArray<Luau::AstTypeOrPack>",
            "class_members": "Luau::AstArray<Luau::AstClassMember>",
            "cst": "const Luau::CstNode*",
        }
        if t.kind in fixed:
            return fixed[t.kind]
        if t.kind in ("integer", "opaque") and t.arg:
            return t.arg
        if t.kind == "enum":
            return f"Luau::{t.arg}"
        if t.kind == "node":
            return f"Luau::{t.arg}*"
        if t.kind in ("nodes", "opt_nodes"):
            return f"Luau::AstArray<Luau::{t.arg}*>"
        if t.kind == "item" and t.arg:
            return self._item_cpp(t.arg)
        if t.kind == "item_ptr" and t.arg:
            return f"{self._item_cpp(t.arg)}*"
        if t.kind == "item_list" and t.arg:
            return f"Luau::AstArray<{self._item_cpp(t.arg)}>"
        if t.kind == "item_ptrs" and t.arg:
            return f"Luau::AstArray<{self._item_cpp(t.arg)}*>"
        if t.kind in ("arg_names", "arg_name_locations"):
            return f"Luau::AstArray<{self._arg_name_cpp(t)}>"
        raise ValueError(f"Unknown field type: {t.kind}")

    def _arg_name_cpp(self, t: FieldType) -> str:
        return "std::optional<Luau::AstArgumentName>" if t.arg == "optional" else "Luau::AstArgumentName"

    def _type_assert(self, owner: str, f: FieldDef, expr: str) -> CppNode:
        return Raw(
            text=f"static_assert(std::is_same_v<std::remove_const_t<decltype({expr})>, {self._cpp_type(f.type)}>, "
            f'"schema type of {owner}.{f.name} does not match the C++ member");'
        )

    def _push(self, t: FieldType, e: str) -> list[CppNode]:
        k = t.kind
        if k == "boolean":
            text = f"lua_pushboolean(L, {e});"
        elif k == "number":
            text = f"lua_pushnumber(L, {e});"
        elif k == "integer":
            text = f"lua_pushinteger64(L, int64_t({e}));" if t.arg in WIDE_INTEGERS else f"lua_pushinteger(L, int({e}));"
        elif k == "char":
            text = f"lua_pushlstring(L, &{e}, 1);"
        elif k == "string":
            text = f'lua_pushlstring(L, {e}.data ? {e}.data : "", {e}.size);'
        elif k == "strings":
            text = (
                f"pushArray(L, {e}.size, [&](size_t i) {{ "
                f'lua_pushlstring(L, {e}.data[i].data ? {e}.data[i].data : "", {e}.data[i].size); }});'
            )
        elif k == "name":
            text = f"{e}.value ? lua_pushstring(L, {e}.value) : lua_pushnil(L);"
        elif k == "opt_name":
            text = f"({e} && {e}->value) ? lua_pushstring(L, {e}->value) : lua_pushnil(L);"
        elif k == "enum":
            text = f"lua_pushstring(L, toString({e}).c_str());"
        elif k == "location":
            text = f"pushLocation(L, handle.doc, {e});"
        elif k == "opt_location":
            text = f"{e} ? pushLocation(L, handle.doc, *{e}) : lua_pushnil(L);"
        elif k == "arg_names":
            text = f"pushArgumentNames(L, {e});"
        elif k == "arg_name_locations":
            text = f"pushArgumentNameLocations(L, handle.doc, {e});"
        elif k == "position":
            text = f"pushPosition(L, handle.doc, {e});"
        elif k == "positions":
            text = f"pushPositionArray(L, handle.doc, {e});"
        elif k == "node":
            text = f"pushAstNode(L, handle.doc, {e});"
        elif k == "nodes":
            text = f"pushNodeArray(L, handle.doc, {e});"
        elif k == "opt_nodes":
            text = (
                f"pushArray(L, {e}.size, [&](size_t i) {{ "
                f"{e}.data[i] ? pushAstNode(L, handle.doc, {e}.data[i]) : lua_pushboolean(L, false); }});"
            )
        elif k == "local":
            text = f"pushItem(L, handle.doc, {e});"
        elif k == "locals":
            text = f"pushLocalArray(L, handle.doc, {e});"
        elif k == "type_or_packs":
            text = f"pushTypeOrPackArray(L, handle.doc, {e});"
        elif k == "cst":
            text = f"pushCstNode(L, handle.doc, {e});"
        elif k == "item":
            text = f"pushItem(L, handle.doc, &{e});"
        elif k == "item_ptr":
            text = f"pushItem(L, handle.doc, {e});"
        elif k == "item_list":
            text = f"pushArray(L, {e}.size, [&](size_t i) {{ pushItem(L, handle.doc, &{e}.data[i]); }});"
        elif k == "item_ptrs":
            text = (
                f"pushArray(L, {e}.size, [&](size_t i) {{ "
                f"{e}.data[i] ? pushItem(L, handle.doc, {e}.data[i]) : lua_pushboolean(L, false); }});"
            )
        elif k == "class_members":
            text = f"pushClassMembers(L, handle.doc, {e});"
        else:
            raise ValueError(f"Field type cannot be read: {k}")
        return [Raw(text=text)]

    def _read(self, t: FieldType, target: str) -> list[CppNode]:
        k = t.kind
        if k == "boolean":
            text = f"{target} = lua_toboolean(L, 2) != 0;"
        elif k == "number":
            text = f"{target} = luaL_checknumber(L, 2);"
        elif k == "integer":
            if t.arg in WIDE_INTEGERS:
                text = f"{target} = lua_isnumber(L, 2) ? {t.arg}(lua_tointeger(L, 2)) : {t.arg}(luaL_checkinteger64(L, 2));"
            else:
                text = f"{target} = static_cast<{t.arg}>(luaL_checkinteger(L, 2));"
        elif k == "char":
            text = f"{target} = readChar(L, 2);"
        elif k == "string":
            text = f"{target} = readString(L, handle.doc, 2);"
        elif k == "strings":
            text = (
                f"{target} = readArray<Luau::AstArray<char>>(L, handle.doc, 2, "
                f"[&](int) {{ return readString(L, handle.doc, -1); }});"
            )
        elif k == "name":
            text = f"{target} = readName(L, handle.doc, 2);"
        elif k == "opt_name":
            text = (
                f"{target} = lua_isnil(L, 2) ? std::nullopt : "
                f"std::optional<Luau::AstName>(readName(L, handle.doc, 2));"
            )
        elif k == "enum":
            text = f"{target} = readEnum<Luau::{t.arg}>(L, 2);"
        elif k == "node":
            text = f"{target} = readNode<Luau::{t.arg}>(L, handle.doc, 2);"
        elif k == "nodes":
            text = f"{target} = readNodeArray<Luau::{t.arg}>(L, handle.doc, 2);"
        elif k == "opt_nodes":
            text = f"{target} = readOptionalNodeArray<Luau::{t.arg}>(L, handle.doc, 2);"
        elif k == "local":
            text = f"{target} = readLocal(L, handle.doc, 2);"
        elif k == "locals":
            text = f"{target} = readLocalArray(L, handle.doc, 2);"
        elif k == "type_or_packs":
            text = f"{target} = readTypeOrPackArray(L, handle.doc, 2);"
        elif k == "item":
            text = f"readItemInto(L, handle.doc, 2, {target});"
        elif k == "item_ptr" and t.arg:
            text = f"{target} = readItemPointer<{self._item_cpp(t.arg)}>(L, handle.doc, 2);"
        elif k == "item_list" and t.arg:
            text = f"{target} = readItemArray<{self._item_cpp(t.arg)}>(L, handle.doc, 2);"
        elif k == "arg_names":
            text = f"{target} = readArgumentNames<{self._arg_name_cpp(t)}>(L, handle.doc, 2);"
        elif k == "class_members":
            text = f"{target} = readClassMembers(L, handle.doc, 2);"
        else:
            raise ValueError(f"Field type cannot be written: {k}")
        return [Raw(text=text)]

    def _validate_item(self, node: NodeDef, e: str, field: str, index: str, scoped: bool = False) -> list[CppNode]:
        body: list[CppNode] = []
        for f in node.fields:
            member = f.member_expr
            if member.startswith("n."):
                member = "item." + member[2:]
            if f.type.kind in ("nodes", "locals"):
                body.extend(self._validate(f.type, member, field, nullable=f.nullable, loop_var="j"))
            else:
                body.extend(
                    self._validate(f.type, member, field, index=index, missing=f'"has no {f.label or f.name}"', nullable=f.nullable)
                )
        for stmt in node.validate_extra:
            body.append(Raw(text=stmt.replace("{field}", f'"{field}"').replace("{index}", index)))
        if not body:
            return []
        body = [Raw(text=f"const auto& item = {e};"), *body]
        return [Block(body=body)] if scoped else body

    def _validate(
        self,
        t: FieldType,
        e: str,
        field: str,
        index: str = "-1",
        missing: str = '"is nil"',
        nullable: bool = False,
        loop_var: str = "i",
    ) -> list[CppNode]:
        k = t.kind
        absent = "nullptr" if nullable else missing
        i = loop_var
        loop = f"size_t {i} = 0; {i} < {e}.size; {i}++"
        if k == "node":
            return [Raw(text=f'v.child({e}, "{field}", {index}, {absent});')]
        if k == "local":
            return [Raw(text=f'v.local({e}, "{field}", {index}, {absent});')]
        if k == "name":
            return [] if nullable else [Raw(text=f'v.name({e}, "{field}", {index}, {missing});')]
        if k == "opt_name":
            return [If(cond=e, then_body=[Raw(text=f'v.name(*{e}, "{field}", {index}, {missing});')])]
        if k in ("nodes", "opt_nodes"):
            return [For(header=loop, body=[Raw(text=f'v.child({e}.data[{i}], "{field}", int({i}), {absent});')])]
        if k == "locals":
            return [For(header=loop, body=[Raw(text=f'v.local({e}.data[{i}], "{field}", int({i}), {absent});')])]
        if k == "type_or_packs":
            item = f"{e}.data[{i}]"
            return [
                For(
                    header=loop,
                    body=[
                        Raw(
                            text=f"v.child({item}.type ? static_cast<Luau::AstNode*>({item}.type) : "
                            f'static_cast<Luau::AstNode*>({item}.typePack), "{field}", int({i}), {absent});'
                        )
                    ],
                )
            ]
        if k == "item" and t.arg:
            return self._validate_item(self._item(t.arg), e, field, index, scoped=True)
        if k == "item_ptr" and t.arg:
            inner = self._validate_item(self._item(t.arg), f"*{e}", field, index)
            otherwise: list[CppNode] = [] if nullable else [Raw(text=f'v.fail("{field}", {index}, {missing});')]
            if not inner and not otherwise:
                return []
            return [If(cond=e, then_body=inner, else_body=otherwise)]
        if k == "item_list" and t.arg:
            inner = self._validate_item(self._item(t.arg), f"{e}.data[{i}]", field, f"int({i})")
            return [For(header=loop, body=inner)] if inner else []
        if k == "class_members":
            prop = self._validate_item(self._item("AstClassProperty"), "*prop", field, f"int({i})")
            method = self._validate_item(self._item("AstClassMethod"), "*method", field, f"int({i})")
            return [
                For(
                    header=loop,
                    body=[
                        If(
                            cond=f"const auto* prop = Luau::get_if<Luau::AstClassProperty>(&{e}.data[{i}])",
                            then_body=prop,
                        ),
                        If(
                            cond=f"const auto* method = Luau::get_if<Luau::AstClassMethod>(&{e}.data[{i}])",
                            then_body=method,
                        ),
                    ],
                )
            ]
        return []

    def _method_cases(self, ud: UserdataDef, fields: list[FieldDef], prefix: str) -> list[SwitchCase]:
        cases = []
        for f in fields:
            read_target = f.read_expr(prefix)
            if read_target:
                cases.append(
                    SwitchCase(
                        label=f"ReflectAtom::{f.atom_name}",
                        body=[*self._push(f.type, read_target), Return(expr="true")],
                    )
                )
            write_target = f.write_expr(prefix)
            if write_target:
                body: list[CppNode] = []
                if ud.mutable:
                    structural = "true" if f.type.kind in STRUCTURAL_KINDS else "false"
                    body.append(Raw(text=f"beginReflectWrite(L, handle.doc, {structural});"))
                by_name = {other.name: other for other in fields}
                if f.companions:
                    body.append(Raw(text=f"{self._cpp_type(f.type)} previous = {write_target};"))
                    body.extend(self._read(f.type, write_target))
                    for name in f.companions:
                        companion = by_name[name].write_expr(prefix)
                        body.append(
                            Raw(text=f"{companion} = realignCompanion(handle.doc, previous, {write_target}, {companion});")
                        )
                elif f.companion_of:
                    owner = by_name[f.companion_of].write_expr(prefix)
                    body.extend(self._read(f.type, "auto list"))
                    body.append(
                        Raw(
                            text=f"{write_target} = fitCompanion(L, handle.doc, 2, list, {owner}.size, "
                            f'"{f.name}", "{f.companion_of}");'
                        )
                    )
                else:
                    body.extend(self._read(f.type, write_target))
                body.append(Raw(text="lua_pushvalue(L, 1);"))
                body.append(Return(expr="true"))
                if f.companions or f.companion_of:
                    body = [Block(body=body)]
                cases.append(SwitchCase(label=f"ReflectAtom::{f.set_atom_name}", body=body))
        return cases

    def _type_asserts(self, owner: str, fields: list[FieldDef], prefix: str) -> list[CppNode]:
        return [self._type_assert(owner, f, f.read_expr(prefix) or f"{prefix}{f.member_expr}") for f in fields]

    def _collect_props(self, fields: list[FieldDef], prefix: str) -> list[CppNode]:
        body: list[CppNode] = []
        for f in fields:
            read_target = f.read_expr(prefix)
            if read_target:
                body.extend(self._push(f.type, read_target))
                body.append(Raw(text=f'lua_setfield(L, -2, "{f.name}");'))
        return body

    def _node_functions(
        self,
        ud: UserdataDef,
        node: NodeDef,
        fields: list[FieldDef],
        node_decl: str,
        prefix: str,
        with_validator: bool,
    ) -> list[CppNode]:
        class_name = node.name
        item_ref = Type(name="AstItem").ref()
        items: list[CppNode] = []

        if not fields:
            handler_body: list[CppNode] = [Return(expr="false")]
        else:
            handler_body = [
                Raw(text=node_decl),
                Unused(name="n"),
                *self._type_asserts(class_name, fields, prefix),
                Switch(expr="atom", cases=self._method_cases(ud, fields, prefix), default_body=[Return(expr="false")]),
            ]

        items.append(
            Function(
                name=f"handle{class_name}Methods",
                return_type=BOOL,
                is_static=True,
                params=[
                    LUA_STATE_PTR.param("L"),
                    item_ref.param("handle"),
                    REFLECT_ATOM.param("atom"),
                ],
                body=handler_body,
            )
        )

        collector_body: list[CppNode] = []
        if fields:
            collector_body.append(Raw(text=node_decl))
            collector_body.append(Unused(name="n"))
            collector_body.extend(self._collect_props(fields, prefix))

        items.append(
            Function(
                name=f"collect{class_name}Props",
                return_type=VOID,
                is_static=True,
                params=[
                    LUA_STATE_PTR.param("L"),
                    item_ref.param("handle"),
                ],
                body=collector_body,
            )
        )

        if with_validator:
            validator_body: list[CppNode] = [
                Raw(text=f"auto* n = static_cast<Luau::{class_name}*>(node);"),
                Unused(name="n"),
            ]
            for f in fields:
                if f.is_plain_member:
                    validator_body.extend(self._validate(f.type, f"n->{f.member_expr}", f.name, nullable=f.nullable))
            validator_body.extend(Raw(text=stmt) for stmt in node.validate_extra)

            items.append(
                Function(
                    name=f"validate{class_name}",
                    return_type=VOID,
                    is_static=True,
                    params=[
                        Type(name="AstValidator").ref().param("v"),
                        Type(name="Luau::AstNode").ptr().param("node"),
                    ],
                    body=validator_body,
                )
            )

        return items

    def _factory_cases(self, ud: UserdataDef, required_fflags: list[str]) -> list[SwitchCase]:
        cases = []
        for node in ud.nodes:
            if node.factory and node.factory.strategy in ("alloc", "empty"):
                args_str = ", ".join(node.factory.args)
                case_body: list[CppNode] = []
                flag = node.factory.requires_fflag
                if flag:
                    if flag not in required_fflags:
                        required_fflags.append(flag)
                    case_body.append(
                        If(
                            cond=f"!FFlag::{flag}",
                            then_body=[
                                Raw(text=f'luaL_error(L, "cannot create a default {node.name}: FFlag {flag} is not enabled");')
                            ],
                        )
                    )
                case_body.append(Return(expr=f"alloc.alloc<Luau::{node.name}>({args_str})"))
                cases.append(SwitchCase(label=f"ReflectAtom::{node.name}", body=case_body))
        return cases

    def _default_factory(self, ud: UserdataDef, base_ptr_type: CppType, required_fflags: list[str]) -> CppNode:
        return Function(
            name=f"createDefault{ud.name}",
            return_type=base_ptr_type,
            params=[
                LUA_STATE_PTR.param("L"),
                REFLECT_ATOM.param("atom"),
                ALLOCATOR_REF.param("alloc"),
            ],
            body=[
                Switch(
                    expr="atom",
                    cases=self._factory_cases(ud, required_fflags),
                    default_body=[Return(expr="nullptr")],
                )
            ],
        )

    def _translation_unit(self, ud: UserdataDef, ns_items: list[CppNode], required_fflags: list[str]) -> str:
        flags = [*required_fflags, *ud.fflags]
        body: list[CppNode] = [Raw(text=f"LUAU_FASTFLAG({flag})") for flag in flags]
        if flags:
            body.append(BlankLine())
        body.append(Namespace(name="Luau", body=ns_items))
        return TranslationUnit(includes=ud.headers, body=body).render()

    def _generate_pointer_inl(self, ud: UserdataDef) -> str:
        base_ptr_type = Type(name=f"Luau::{ud.name}").ptr(is_const=ud.is_const)
        item_ref = Type(name="AstItem").ref()
        method_handler_type = FuncPtr(
            return_type=BOOL,
            params=[LUA_STATE_PTR.param("L"), item_ref.param("handle"), REFLECT_ATOM.param("atom")],
        )
        prop_collector_type = FuncPtr(
            return_type=VOID,
            params=[LUA_STATE_PTR.param("L"), item_ref.param("handle")],
        )
        validator_type = FuncPtr(
            return_type=VOID,
            params=[Type(name="AstValidator").ref().param("v"), Type(name="Luau::AstNode").ptr().param("node")],
        )

        ns_items: list[CppNode] = [
            Variable(
                type=TemplateType(name="std::vector", args=[Type(name="AstItemClass")]),
                name=ud.class_table_name,
                is_static=True,
            )
        ]

        reg_params = [CONST_CHAR_PTR.param("kind")]
        if ud.categories:
            reg_params.append(NODE_CATEGORY.param("category"))
        reg_params.extend([
            method_handler_type.param("methodHandler", default="nullptr"),
            prop_collector_type.param("propCollector", default="nullptr"),
        ])
        if ud.mutable:
            reg_params.append(validator_type.param("validator", default="nullptr"))

        table = ud.class_table_name
        fallback_category = "unknown" if ud.categories else ud.category
        fallback = f'AstItemClass{{"{ud.name}", "{fallback_category}", ItemFamily::{ud.family}}}'
        if ud.categories:
            entry = (
                f"AstItemClass{{kind, categoryToString(category), ItemFamily::{ud.family}, category, "
                f"category == NodeCategory::Stat, methodHandler, propCollector"
                f"{', validator' if ud.mutable else ''}}}"
            )
        else:
            entry = (
                f'AstItemClass{{kind, "{ud.category}", ItemFamily::{ud.family}, NodeCategory::Unknown, false, '
                f"methodHandler, propCollector{', validator' if ud.mutable else ''}}}"
            )

        ns_items.append(
            Function(
                name=ud.register_func,
                return_type=VOID,
                template_params=["typename T"],
                is_static=True,
                multiline_params=True,
                params=reg_params,
                body=[
                    Raw(text=f"int idx = {ud.class_index_getter};"),
                    If(cond=f"size_t(idx) >= {table}.size()", then_body=[Raw(text=f"{table}.resize(idx + 1, {fallback});")]),
                    Raw(text=f"{table}[idx] = {entry};"),
                ],
            )
        )

        required_fflags: list[str] = []
        ns_items.append(self._default_factory(ud, base_ptr_type, required_fflags))

        for node in ud.nodes:
            class_ptr = f"{'const ' if ud.is_const else ''}Luau::{node.name}*"

            fields: list[FieldDef] = list(ud.base_fields)
            if node.base == "AstStat":
                fields.extend(ud.stat_fields)
            fields.extend(node.fields)

            ns_items.extend(
                self._node_functions(
                    ud,
                    node,
                    fields,
                    f"auto* n = static_cast<{class_ptr}>(handle.target);",
                    "n->",
                    ud.mutable,
                )
            )

        reg_calls = []
        for node in ud.nodes:
            cat_arg = f"NodeCategory::{node.category}, " if node.category else ""
            validator_arg = f", validate{node.name}" if ud.mutable else ""
            reg_calls.append(
                f'{ud.register_func}<Luau::{node.name}>("{node.name}", {cat_arg}'
                f"handle{node.name}Methods, collect{node.name}Props{validator_arg});"
            )

        ns_items.append(
            Function(
                name=f"register{ud.name}Classes",
                return_type=VOID,
                is_static=True,
                body=[Raw(text=c) for c in reg_calls],
            )
        )

        return self._translation_unit(ud, ns_items, required_fflags)

    def _generate_plain_inl(self, ud: UserdataDef) -> str:
        node = ud.nodes[0]
        cpp = self._item_cpp(node.name)
        doc_state_ref = TemplateType(name="std::shared_ptr", args=[Type(name="AstDocumentState")]).ref(is_const=True)
        item_ref = Type(name="AstItem").ref()
        node_decl = f"auto& n = *static_cast<{cpp}*>(handle.target);"
        required_fflags: list[str] = []

        ns_items: list[CppNode] = self._node_functions(ud, node, list(node.fields), node_decl, "", False)

        create_body: list[CppNode] = []
        args_str = ", ".join(node.factory.args) if node.factory else ""
        flag = node.factory.requires_fflag if node.factory else None
        if flag:
            required_fflags.append(flag)
            create_body.append(
                If(
                    cond=f"!FFlag::{flag}",
                    then_body=[Raw(text=f'luaL_error(L, "cannot create a default {node.name}: FFlag {flag} is not enabled");')],
                )
            )
        create_body.append(Raw(text=f"pushItem(L, doc, doc->allocator()->alloc<{cpp}>({cpp}{{{args_str}}}));"))
        ns_items.append(
            Function(
                name=f"create{node.name}",
                return_type=VOID,
                is_static=True,
                params=[LUA_STATE_PTR.param("L"), doc_state_ref.param("doc")],
                body=create_body,
            )
        )

        label = "nullptr"
        if node.display:
            shown = next(f for f in node.fields if f.name == node.display)
            expr = shown.read_expr("")
            if shown.type.kind == "name":
                label_body: list[CppNode] = [
                    Raw(text=node_decl),
                    If(
                        cond=f"{expr}.value",
                        then_body=[Raw(text=f'lua_pushfstring(L, "{node.name}(%s)", {expr}.value);')],
                        else_body=[Raw(text=f'lua_pushstring(L, "{node.name}");')],
                    ),
                ]
            elif shown.type.kind == "string":
                label_body = [
                    Raw(text=node_decl),
                    Raw(text=f'std::string text({expr}.data ? {expr}.data : "", {expr}.size);'),
                    Raw(text=f'lua_pushfstring(L, "{node.name}(%s)", text.c_str());'),
                ]
            else:
                raise ValueError(f"Field cannot be displayed: {node.name}.{node.display}")
            label = f"label{node.name}"
            ns_items.append(
                Function(
                    name=label,
                    return_type=VOID,
                    is_static=True,
                    params=[LUA_STATE_PTR.param("L"), item_ref.param("handle")],
                    body=label_body,
                )
            )

        ns_items.append(
            Raw(
                text=f'const AstItemClass g_{node.name}Class = {{"{node.name}", "{ud.category}", ItemFamily::{ud.family}, '
                f"NodeCategory::Unknown, false, handle{node.name}Methods, collect{node.name}Props, nullptr, "
                f"create{node.name}, {label}}};"
            )
        )

        return self._translation_unit(ud, ns_items, required_fflags)

    def generate_items_inl(self) -> str:
        items = self._plain_items()
        body: list[CppNode] = [
            Namespace(
                name="Luau",
                body=[
                    Function(
                        name="plainItemClass",
                        return_type=Type(name="AstItemClass").ptr(is_const=True),
                        params=[REFLECT_ATOM.param("atom")],
                        body=[
                            Switch(
                                expr="atom",
                                cases=[
                                    SwitchCase(label=f"ReflectAtom::{node.name}", body=[Return(expr=f"&g_{node.name}Class")])
                                    for node in items
                                ],
                                default_body=[Return(expr="nullptr")],
                            )
                        ],
                    )
                ],
            )
        ]
        return TranslationUnit(includes=[f'"{node.name}.gen.inl"' for node in items], body=body).render()

    def generate_items_h(self) -> str:
        ns_items: list[CppNode] = [
            Raw(text="struct AstItemClass;\nstruct ReflectComment;"),
            Raw(text="template<typename T>\nconst AstItemClass* itemClass() = delete;"),
        ]
        for node in self._plain_items():
            ns_items.append(
                Raw(
                    text=f"extern const AstItemClass g_{node.name}Class;\n"
                    f"template<>\n"
                    f"inline const AstItemClass* itemClass<{self._item_cpp(node.name)}>()\n"
                    f"{{\n"
                    f"    return &g_{node.name}Class;\n"
                    f"}}"
                )
            )
        return TranslationUnit(
            includes=['"Luau/Ast.h"', '"Luau/Cst.h"'], body=[Namespace(name="Luau", body=ns_items)]
        ).render()

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
            SwitchCase(label=f"ReflectAtom::{variant}", body=[Return(expr=f'"{string_val}"')])
            for variant, string_val in atoms
        ]
        ns_items.append(
            Function(
                name="getAtomString",
                return_type=CONST_CHAR_PTR,
                is_inline=True,
                params=[REFLECT_ATOM.param("atom")],
                body=[
                    Switch(expr="atom", cases=atom_cases, default_body=[Return(expr='""')])
                ],
            )
        )

        map_type = TemplateType(name="DenseHashMap2", args=[STRING_VIEW, REFLECT_ATOM])
        map_body: list[CppNode] = [
            Variable(type=map_type, name="map"),
            *(Raw(text=f'map["{string_val}"] = ReflectAtom::{variant};') for string_val, variant in atom_map),
            Return(expr="map"),
        ]
        ns_items.append(
            Function(
                name="resolveGlobalReflectAtom",
                return_type=REFLECT_ATOM,
                is_inline=True,
                params=[STRING_VIEW.param("key")],
                body=[
                    LambdaInitVar(
                        type=map_type,
                        name="s_atomMap",
                        body=map_body,
                    ),
                    BlankLine(),
                    If(
                        cond="const ReflectAtom* atom = s_atomMap.find(key)",
                        then_body=[Return(expr="*atom")],
                    ),
                    BlankLine(),
                    Return(expr="ReflectAtom::Unknown"),
                ],
            )
        )

        ns_items.append(
            Function(
                name="resolveReflectAtom",
                return_type=REFLECT_ATOM,
                is_inline=True,
                params=[
                    INT.param("atomId"),
                    STRING_VIEW.param("key"),
                ],
                body=[
                    If(
                        cond="atomId >= 0",
                        then_body=[
                            If(
                                cond="atomId < int(ReflectAtom::Count)",
                                then_body=[Return(expr="ReflectAtom(atomId)")],
                            ),
                            Return(expr="ReflectAtom::Unknown"),
                        ],
                    ),
                    Return(expr="resolveGlobalReflectAtom(key)"),
                ],
            )
        )

        ns_items.append(
            Function(
                name="resolveReflectAtom",
                return_type=REFLECT_ATOM,
                is_inline=True,
                params=[
                    INT.param("atomId"),
                    CONST_CHAR_PTR.param("str"),
                    SIZE_T.param("len"),
                ],
                body=[
                    If(
                        cond="atomId >= 0",
                        then_body=[
                            If(
                                cond="atomId < int(ReflectAtom::Count)",
                                then_body=[Return(expr="ReflectAtom(atomId)")],
                            ),
                            Return(expr="ReflectAtom::Unknown"),
                        ],
                    ),
                    Return(expr="resolveGlobalReflectAtom(std::string_view(str, len))"),
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


def find_repo_root() -> Path:
    """Find repository root using git or falling back to .git directory search."""
    if shutil.which("git"):
        try:
            res = subprocess.run(
                ["git", "rev-parse", "--show-toplevel"],
                cwd=Path(__file__).resolve().parent,
                capture_output=True,
                text=True,
                check=True,
            )
            top = res.stdout.strip()
            if top:
                return Path(top)
        except (subprocess.SubprocessError, OSError):
            pass

    for parent in Path(__file__).resolve().parents:
        if (parent / ".git").exists():
            return parent
    return Path(__file__).resolve().parent


def main() -> int:
    repo_root = find_repo_root()
    reflect_dir = repo_root / "Reflect"
    src_dir = reflect_dir / "src"
    src_dir.mkdir(parents=True, exist_ok=True)
    inc_dir = reflect_dir / "include" / "Luau"
    inc_dir.mkdir(parents=True, exist_ok=True)

    generator = CppGenerator(GLOBAL_SCHEMA)

    for ud in GLOBAL_SCHEMA.userdatas:
        if ud.output_file:
            inl_path = src_dir / ud.output_file
            content = generator.generate_inl(ud)
            inl_path.write_text(content, encoding="utf-8")
            print(f"Generated: {inl_path}")

    items_path = src_dir / "AstItems.gen.inl"
    items_path.write_text(generator.generate_items_inl(), encoding="utf-8")
    print(f"Generated: {items_path}")

    items_header_path = inc_dir / "ReflectItems.gen.h"
    items_header_path.write_text(generator.generate_items_h(), encoding="utf-8")
    print(f"Generated: {items_header_path}")

    atoms_path = inc_dir / "ReflectAtoms.gen.h"
    atoms_content = generator.generate_atoms_h()
    atoms_path.write_text(atoms_content, encoding="utf-8")
    print(f"Generated: {atoms_path}")

    print("\nSummary:")
    for ud in GLOBAL_SCHEMA.userdatas:
        if ud.storage == "pointer":
            print(f"  {ud.name:<11}: {len(ud.nodes)} nodes")
    print(f"  Plain items: {len(PLAIN_ITEMS)} types")
    print(f"  ReflectAtom: {len(GLOBAL_SCHEMA.all_atoms())} atoms")
    print("Code generation successful.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
