// TransitGen core — style JSON loading and validation (04 §1). Message thread only: allocates.
#pragma once

#include "transitgen/StyleTable.h"

#include <memory>
#include <string>
#include <string_view>

namespace tg {

enum class StyleOrigin : uint8_t { Factory, User };   // factory ids 1..99, user ids >= 100

/// Parses and fully validates one style document. On failure returns false, leaves `out`
/// unspecified and sets `error` to "<key path>: <problem>" or "line L, column C: <problem>".
bool parseStyle(std::string_view json, Style& out, std::string& error, StyleOrigin origin = StyleOrigin::User);

/// Appends `s` to `table`. Fails on a duplicate id or a full table.
bool addStyle(StyleTable& table, const Style& s, std::string& error);

/// Factory style JSON compiled into the binary from styles/*.json (generated at build time).
struct EmbeddedStyle {
    const char* fileName;
    const char* json;
};
const EmbeddedStyle* factoryStyleSources(int& count) noexcept;

/// Parses every embedded factory style into `table` (appending). Fails on the first error,
/// which is prefixed with the file name.
bool loadFactoryStyles(StyleTable& table, std::string& error);

/// Convenience: a heap-allocated table holding the factory styles (nullptr + error on failure).
std::unique_ptr<StyleTable> makeFactoryStyleTable(std::string* error = nullptr);

} // namespace tg
