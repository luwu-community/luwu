// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Core/Value.h"

#include <string>
#include <vector>

namespace Luwu::Dsl
{

class Evaluator;

// Represents a record instance containing a named type and named fields. Records are compared by identity.
struct Record : GcCell
{
    std::string type;
    std::vector<std::string> fields;
    std::vector<Value> values;

    std::string kindName() const { return "a " + type + " record"; }
    std::string show() const;

    void markChildren(std::vector<const GcCell*>& worklist) const override
    {
        for (const auto& v : values)
        {
            if (const GcCell* c = v.cell())
                worklist.push_back(c);
        }
    }
};

bool isRecord(const Value& v, const std::string& type = "");
const Record& recordOf(const Value& v);
// Returns a pointer to the value of field, or nullptr if no such field exists.
const Value* recordField(const Value& v, const std::string& field);
// Returns a copy of record v with field set to value.
Value recordWith(const Value& v, const std::string& field, const Value& value);

// Registers record-manipulation builtins (%make-record, record?, record-type, record-ref, record-with).
void installRecords(Evaluator& evaluator);

} // namespace Luwu::Dsl
