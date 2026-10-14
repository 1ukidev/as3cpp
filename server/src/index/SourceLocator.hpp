#pragma once

#include "model/Model.hpp"
#include "syntax/Mxml.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace as3lsp::index {

// Finds the source code of classes that the index reads from compiled libraries, such as the Flex
// framework classes in framework.swc, whose sources are in the SDK. Files are parsed on first use and cached.
class SourceLocator {
public:
    SourceLocator(std::vector<std::filesystem::path> roots, std::shared_ptr<const syntax::MxmlContext> mxml);

    // The source directories of a Flex SDK: frameworks/projects/*/src.
    [[nodiscard]] static std::vector<std::filesystem::path> flexSourceRoots(const std::filesystem::path& sdk);

    // The declaration of a class in its source file, or nullptr when the source is not available.
    [[nodiscard]] const model::TypeDecl* findType(std::string_view qualifiedName) const;

    // The member of `sourceType` that matches a member of the compiled class.
    [[nodiscard]] static const model::Member* findMember(const model::TypeDecl& sourceType,
                                                         const model::Member& compiled);

private:
    void scan() const;

    std::vector<std::filesystem::path> roots_;
    std::shared_ptr<const syntax::MxmlContext> mxml_;
    // The cache is filled by the const lookups. The server uses the locator from one thread only.
    mutable bool scanned_ = false;
    mutable std::unordered_map<std::string, std::filesystem::path> files_;
    mutable std::unordered_map<std::string, model::UnitPtr> units_;
};

} // namespace as3lsp::index
