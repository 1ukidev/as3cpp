#pragma once

#include "model/Model.hpp"
#include "syntax/Parser.hpp"
#include "syntax/Token.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace as3lsp::syntax {

// Maps MXML tags to ActionScript classes, and MXML files to packages.
class MxmlContext {
public:
    // Reads the namespaces and the component manifests of a Flex SDK, listed in frameworks/flex-config.xml.
    void loadSdk(const std::filesystem::path& sdk);
    void addManifest(std::string uri, std::string_view manifestXml);
    void addSourceRoot(const std::filesystem::path& root);

    // The package of the class declared by a file, from the source root that contains it.
    [[nodiscard]] std::string packageOf(const std::filesystem::path& file) const;
    // The qualified class of a tag. `filePackage` resolves the "*" namespace.
    [[nodiscard]] std::optional<std::string>
    classOf(std::string_view namespaceUri, std::string_view localName, std::string_view filePackage) const;

private:
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> manifests_;
    std::vector<std::filesystem::path> sourceRoots_;
};

// An MXML file seen as ActionScript.
struct MxmlTranslation {
    // ActionScript text with the same lines and UTF-16 columns as the MXML text. Script blocks, event
    // attributes and data bindings keep their text; everything else becomes spaces.
    std::string source;
    // The class that the file declares, with one member for each tag that has an id.
    model::TypeDecl type;
    std::vector<model::Range> codeRanges;
    // Values of event attributes, where the "event" variable exists.
    std::vector<model::Range> eventRanges;
    std::vector<model::MarkupReference> references;
    std::optional<model::Position> scriptStart;
    // Errors in the XML structure, such as tags without an end tag.
    std::vector<model::Diagnostic> diagnostics;
};

[[nodiscard]] MxmlTranslation translateMxml(std::string_view text, const std::filesystem::path& file,
                                            const MxmlContext& context);

// Parses an MXML file. When given, `source` and `lex` receive the translated text and its tokens;
// the tokens point into `source`.
[[nodiscard]] std::shared_ptr<model::Unit> parseMxml(std::string_view text, const std::filesystem::path& file,
                                                     const MxmlContext& context,
                                                     const ParseOptions& options = {},
                                                     std::string* source = nullptr, LexResult* lex = nullptr);

[[nodiscard]] bool isMxmlFile(const std::filesystem::path& path);

} // namespace as3lsp::syntax
