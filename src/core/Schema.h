#pragma once
#include <cstddef>

namespace seqb {

enum class FieldType {
    Bool,
    Int,
    String,
    StringList,    // CSV or newline-separated
    MacAddress,
    Enum,          // enumValues = comma-separated list
    PredicateRef,  // editor renders nested predicate inspector
};

struct FieldDef {
    const char* key;
    FieldType   type;
    const char* label;
    const char* defaultValue;  // nullable
    const char* enumValues;    // nullable, CSV
    bool        required;
};

struct BlockSchema {
    const char*        type;
    const char*        label;
    const char*        category;
    const FieldDef*    fields;
    std::size_t        fieldCount;
    const char* const* childSlots;     // nullable
    std::size_t        childSlotCount;
};

struct PredicateSchema {
    const char*     type;
    const char*     label;
    const FieldDef* fields;
    std::size_t     fieldCount;
};

struct TriggerSchema {
    const char*     type;
    const char*     label;
    const FieldDef* fields;
    std::size_t     fieldCount;
    bool            pausesBleScan;
};

}  // namespace seqb
