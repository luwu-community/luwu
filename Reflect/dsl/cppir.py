from __future__ import annotations

from typing import Any, Optional, Union
from pydantic import BaseModel


def _indent(level: int) -> str:
    return "    " * level


def render_items(items: list[CppNode], indent: int = 0) -> str:
    """Render a list of CppNode items into formatted C++ code."""
    lines: list[str] = []
    for item in items:
        assert isinstance(
            item, CppNode
        ), f"Items must be CppNode instances (wrap raw strings in Raw(text=...)), got {type(item).__name__}: {item!r}"
        rendered = item.render(indent)
        if rendered != "":
            lines.append(rendered)
        elif item.is_blank_line:
            lines.append("")
    return "\n".join(lines)


class CppNode(BaseModel):
    """Base node for C++ code generation AST."""

    def render(self, indent: int = 0) -> str:
        """Render node into C++ source code."""
        raise NotImplementedError

    @property
    def is_blank_line(self) -> bool:
        """True if node emits an intentional blank line."""
        return False


class Raw(CppNode):
    """Unparsed literal C++ statement or code snippet."""

    text: str

    def render(self, indent: int = 0) -> str:
        if not self.text:
            return ""
        return f"{_indent(indent)}{self.text}"

    @property
    def is_blank_line(self) -> bool:
        return self.text == ""


class BlankLine(CppNode):
    """Explicit blank line in generated C++ output."""

    def render(self, indent: int = 0) -> str:
        return ""

    @property
    def is_blank_line(self) -> bool:
        return True


class Return(CppNode):
    """C++ `return [expr];` statement."""

    expr: Optional[str] = None

    def render(self, indent: int = 0) -> str:
        if self.expr is not None:
            return f"{_indent(indent)}return {self.expr};"
        return f"{_indent(indent)}return;"


class Break(CppNode):
    """C++ `break;` statement."""

    def render(self, indent: int = 0) -> str:
        return f"{_indent(indent)}break;"


class Unused(CppNode):
    """C++ `(void)<name>;` statement to silence unused-variable warnings."""

    name: str

    def render(self, indent: int = 0) -> str:
        return f"{_indent(indent)}(void){self.name};"


class CppType(BaseModel):
    """Base declarative C++ type with fluent pointer, ref, and param helpers."""

    def format_decl(self, name: str = "") -> str:
        """Format type declaration, optionally binding an identifier name."""
        raise NotImplementedError

    def __str__(self) -> str:
        return self.format_decl()

    def ptr(self, is_const: bool = False) -> Ptr:
        """Derive pointer type (`T*` or `const T*`)."""
        return Ptr(pointee=self, is_const=is_const)

    def ref(self, is_const: bool = False) -> Ref:
        """Derive reference type (`T&` or `const T&`)."""
        return Ref(referent=self, is_const=is_const)

    def param(self, name: str = "", default: Optional[str] = None) -> Param:
        """Create function parameter binding this type to a name and default value."""
        return Param(type=self, name=name, default=default)


class RawType(CppType):
    """Escape-hatch type from a literal string."""

    text: str

    def format_decl(self, name: str = "") -> str:
        if name:
            return f"{self.text} {name}"
        return self.text


class Type(CppType):
    """Named C++ type (e.g. `int`, `void`, `ReflectAtom`)."""

    name: str

    def format_decl(self, name: str = "") -> str:
        if name:
            return f"{self.name} {name}"
        return self.name


class TemplateType(CppType):
    """Parameterized template type (e.g. `std::vector<T>`, `DenseHashMap2<K, V>`)."""

    name: str
    args: list[CppType] = []

    def format_decl(self, name: str = "") -> str:
        args_str = ", ".join(a.format_decl() for a in self.args)
        decl = f"{self.name}<{args_str}>"
        if name:
            return f"{decl} {name}"
        return decl


class Ptr(CppType):
    """Pointer type (`T*` or `const T*`)."""

    pointee: CppType
    is_const: bool = False

    def format_decl(self, name: str = "") -> str:
        const_prefix = "const " if self.is_const else ""
        decl = f"{const_prefix}{self.pointee.format_decl()}*"
        if name:
            return f"{decl} {name}"
        return decl


class Ref(CppType):
    """Reference type (`T&` or `const T&`)."""

    referent: CppType
    is_const: bool = False

    def format_decl(self, name: str = "") -> str:
        const_prefix = "const " if self.is_const else ""
        decl = f"{const_prefix}{self.referent.format_decl()}&"
        if name:
            return f"{decl} {name}"
        return decl


class Param(CppType):
    """Function parameter binding a type to an identifier and optional default value."""

    type: CppType
    name: str = ""
    default: Optional[str] = None

    def format_decl(self, name: str = "") -> str:
        n = name if name else self.name
        decl = self.type.format_decl(n)
        if self.default is not None:
            return f"{decl} = {self.default}"
        return decl

    def render(self) -> str:
        return self.format_decl()


class FuncPtr(CppType):
    """Function pointer type supporting nested return types and parameters."""

    return_type: CppType
    params: list[CppType] = []

    def format_decl(self, name: str = "") -> str:
        param_str = ", ".join(p.format_decl() for p in self.params)
        ptr_part = f"*{name}" if name else "*"

        if isinstance(self.return_type, FuncPtr):
            return self.return_type.format_decl(f"({ptr_part})({param_str})")

        ret = self.return_type.format_decl()
        return f"{ret} ({ptr_part})({param_str})"


VOID = Type(name="void")
BOOL = Type(name="bool")
INT = Type(name="int")
SIZE_T = Type(name="size_t")
STRING_VIEW = Type(name="std::string_view")
CONST_CHAR_PTR = Ptr(pointee=Type(name="char"), is_const=True)
LUA_STATE_PTR = Ptr(pointee=Type(name="lua_State"))
REFLECT_ATOM = Type(name="ReflectAtom")
NODE_CATEGORY = Type(name="NodeCategory")
ALLOCATOR_REF = Ref(referent=Type(name="Luau::Allocator"))


class StructField(BaseModel):
    """Member field in a C++ struct definition."""

    type: CppType
    name: str
    default: Optional[str] = None

    def render(self, indent: int = 0) -> str:
        decl = self.type.format_decl(self.name)
        if self.default is not None:
            return f"{_indent(indent)}{decl} = {self.default};"
        return f"{_indent(indent)}{decl};"


class Struct(CppNode):
    """C++ `struct <name> { ... };` definition."""

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
    """C++ variable declaration or definition."""

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
    """Variable initialized via immediately invoked lambda expression (IIFE)."""

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
            lines.append(render_items(self.body, indent + 1))
        lines.append(f"{_indent(indent)}}}();")
        return "\n".join(lines)


class Block(CppNode):
    """Enclosed `{ ... }` block scope."""

    body: list[CppNode] = []

    def render(self, indent: int = 0) -> str:
        lines = [f"{_indent(indent)}{{"]
        if self.body:
            lines.append(render_items(self.body, indent + 1))
        lines.append(f"{_indent(indent)}}}")
        return "\n".join(lines)


class SwitchCase(BaseModel):
    """`case <label>:` branch in a C++ `switch` statement."""

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
            elif stmt.is_blank_line:
                lines.append("")
            else:
                lines.append(stmt.render(indent + 1))
        return "\n".join(lines)


class Switch(CppNode):
    """C++ `switch (<expr>) { ... }` statement."""

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
                elif stmt.is_blank_line:
                    lines.append("")
                else:
                    lines.append(stmt.render(indent + 1))
        lines.append(f"{_indent(indent)}}}")
        return "\n".join(lines)


class If(CppNode):
    """C++ `if (<cond>) { ... } [else { ... }]` construct."""

    cond: str
    then_body: list[CppNode] = []
    else_body: list[CppNode] = []

    def render(self, indent: int = 0) -> str:
        lines = [
            f"{_indent(indent)}if ({self.cond})",
            f"{_indent(indent)}{{",
        ]
        if self.then_body:
            lines.append(render_items(self.then_body, indent + 1))
        lines.append(f"{_indent(indent)}}}")
        if self.else_body:
            lines.append(f"{_indent(indent)}else")
            lines.append(f"{_indent(indent)}{{")
            lines.append(render_items(self.else_body, indent + 1))
            lines.append(f"{_indent(indent)}}}")
        return "\n".join(lines)


class Function(CppNode):
    """C++ function definition or declaration."""

    name: str
    return_type: CppType = VOID
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
            lines.append(render_items(self.body, indent + 1))
        lines.append(f"{_indent(indent)}}}")
        return "\n".join(lines)


class EnumVariant(BaseModel):
    """Enumerator variant in a C++ enum."""

    name: str
    value: Optional[Union[int, str]] = None

    def render(self, indent: int = 0) -> str:
        if self.value is not None:
            return f"{_indent(indent)}{self.name} = {self.value},"
        return f"{_indent(indent)}{self.name},"


class Enum(CppNode):
    """C++ `enum [class] <name> [: <underlying_type>] { ... };` definition."""

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
    """C++ `namespace <name> { ... }` scope."""

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
    """Top-level C++ translation unit with includes, `#pragma once`, and body nodes."""

    comment: str = "// Auto-generated by Reflect/dsl/generate_ast_cpp.py. DO NOT EDIT!"
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
