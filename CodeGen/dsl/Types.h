// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <string>

namespace Luwu::Dsl
{

enum class TypeKind
{
    Void,
    Ptr,
    I64,
    U64,
    I32,
    U32,
    I16,
    U8,
    Double,
    Tag,
    Any
};

inline TypeKind parseTypeKind(const std::string& name)
{
    std::string s = name;
    if (!s.empty() && s[0] == ':')
        s = s.substr(1);

    if (s == "ptr") return TypeKind::Ptr;
    if (s == "i64") return TypeKind::I64;
    if (s == "u64") return TypeKind::U64;
    if (s == "i32" || s == "int") return TypeKind::I32;
    if (s == "u32" || s == "uint") return TypeKind::U32;
    if (s == "i16") return TypeKind::I16;
    if (s == "u8") return TypeKind::U8;
    if (s == "double") return TypeKind::Double;
    if (s == "tag") return TypeKind::Tag;
    if (s == "reg") return TypeKind::Ptr;
    if (s == "any") return TypeKind::Any;
    return TypeKind::Void;
}

enum class OperandKind
{
    Reg,
    Mem,
    Imm
};

struct Operand
{
    OperandKind kind = OperandKind::Reg;
    TypeKind type = TypeKind::Void;
    std::string text;      // register name or immediate literal
    std::string memBase;   // base register string if kind == Mem
    std::string memOffset; // offset expression string if kind == Mem
    std::string rawOp;     // raw opcode string (e.g. "OP_A(inst)") if bound from rule param

    static Operand makeReg(TypeKind t, const std::string& reg, const std::string& raw = "")
    {
        return Operand{OperandKind::Reg, t, reg, "", "", raw};
    }

    static Operand makeMem(TypeKind t, const std::string& base, const std::string& offset)
    {
        return Operand{OperandKind::Mem, t, "", base, offset, ""};
    }

    static Operand makeImm(TypeKind t, const std::string& imm)
    {
        return Operand{OperandKind::Imm, t, imm, "", "", ""};
    }
};

} // namespace Luwu::Dsl
