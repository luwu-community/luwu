// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/TypeFunction.h"
#include "Luau/TypeFunctionError.h"

namespace Luau
{

struct TypeFunctionContext;

using TypeFunctionKind = Variant<TypeFunctionTypeId, TypeFunctionTypePackId>;

template<typename T>
const T* get(const TypeFunctionKind& tfkind)
{
    return get_if<T>(&tfkind);
}

struct TypeFunctionRuntimeBuilderState
{
    NotNull<TypeFunctionContext> ctx;

    // List of errors that occur during serialization/deserialization
    // At every iteration of serialization/deserialization, if this list.size() != 0, we halt the process
    std::vector<std::string> errors_DEPRECATED{};

    std::vector<TypeFunctionError> errors{};

    TypeFunctionRuntimeBuilderState(NotNull<TypeFunctionContext> ctx)
        : ctx(ctx)
    {
    }
};

TypeFunctionTypeId serialize(TypeId ty, TypeFunctionRuntimeBuilderState* state);
TypeFunctionTypePackId serialize(TypePackId tp, TypeFunctionRuntimeBuilderState* state);

TypeId deserialize(TypeFunctionTypeId ty, TypeFunctionRuntimeBuilderState* state);
TypePackId deserialize(TypeFunctionTypePackId tp, TypeFunctionRuntimeBuilderState* state);

// Luwu doc references: a runtime type's metadata, with the type function call as where its doc comments were written
std::shared_ptr<const TypeMetadataTable> deserializeMetadata(TypeFunctionTypeId ty, TypeFunctionRuntimeBuilderState* state);

} // namespace Luau
