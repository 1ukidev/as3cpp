#include "syntax/Mxml.hpp"

#include "syntax/Lexer.hpp"
#include "util/Files.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <array>
#include <map>

namespace as3lsp::syntax {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view languageNamespace2009 = "http://ns.adobe.com/mxml/2009";
constexpr std::string_view languageNamespace2006 = "http://www.adobe.com/2006/mxml";
constexpr std::string_view eventType = "flash.events.Event";

// Properties whose values are text, even when they contain parentheses or an equals sign.
constexpr std::array<std::string_view, 12> textAttributes = {
    "text",     "label",       "title",       "headerText", "toolTip",   "prompt",
    "htmlText", "errorString", "description", "source",     "styleName", "id",
};

// Code keywords that can start the value of an event attribute.
constexpr std::array<std::string_view, 9> statementKeywords = {"if",  "var",    "for",  "while", "return",
                                                               "new", "delete", "this", "super"};

bool isNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-' || c == '.' || c == ':' || static_cast<unsigned char>(c) >= 0x80;
}

bool isIdentifierChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool isLanguageNamespace(std::string_view uri) {
    return uri == languageNamespace2009 || uri == languageNamespace2006;
}

// Whether an attribute value is ActionScript code, such as "save(event)" or "index = 0".
// The same rule colors these values in the MXML grammar of the extension.
bool isEventCode(std::string_view name, std::string_view value) {
    if (std::ranges::find(textAttributes, name) != textAttributes.end()) {
        return false;
    }
    value = util::trim(value);
    std::size_t i = 0;
    while (i < value.size() && isIdentifierChar(value[i])) {
        ++i;
    }
    if (i == 0) {
        return false;
    }
    if (std::ranges::find(statementKeywords, value.substr(0, i)) != statementKeywords.end()) {
        return true;
    }
    while (i < value.size() && (isIdentifierChar(value[i]) || value[i] == '.')) {
        ++i;
    }
    const std::string_view rest = value.substr(i);
    if (rest.starts_with('(') || rest.starts_with("++") || rest.starts_with("--")) {
        return true;
    }
    const std::string_view assignment = util::trim(rest);
    return assignment.starts_with('=') && !assignment.starts_with("==");
}

std::optional<std::string> attributeValue(std::string_view xml, std::string_view name) {
    const auto start = xml.find(std::string{name} + "=\"");
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const auto valueStart = start + name.size() + 2;
    const auto end = xml.find('"', valueStart);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return std::string{xml.substr(valueStart, end - valueStart)};
}

class Translator {
public:
    Translator(std::string_view text, const fs::path& file, const MxmlContext& context)
        : text_(text), context_(context) {
        lineStarts_.push_back(0);
        for (std::size_t i = 0; i < text_.size(); ++i) {
            if (text_[i] == '\n' || (text_[i] == '\r' && (i + 1 >= text_.size() || text_[i + 1] != '\n'))) {
                lineStarts_.push_back(i + 1);
            }
        }
        result_.type.name = file.stem().string();
        result_.type.package = context_.packageOf(file);
        result_.type.kind = model::TypeKind::Class;
        result_.type.visibility = model::Visibility::Public;
        result_.type.range = {{0, 0}, positionAt(text_.size())};
    }

    MxmlTranslation run() {
        std::size_t i = 0;
        while (i < text_.size()) {
            if (lookingAt(i, "<!--")) {
                i = skipPastOrReport(i, 4, "-->", "Unterminated comment: '-->' is missing.");
            } else if (lookingAt(i, "<![CDATA[")) {
                i = skipPastOrReport(i, 9, "]]>", "Unterminated CDATA section: ']]>' is missing.");
            } else if (lookingAt(i, "<?")) {
                i = skipPastOrReport(i, 2, "?>", "Unterminated processing instruction: '?>' is missing.");
            } else if (lookingAt(i, "</")) {
                i = endTag(i);
            } else if (lookingAt(i, "<!")) {
                i = skipPast(i, ">");
            } else if (text_[i] == '<' && i + 1 < text_.size() && isNameChar(text_[i + 1])) {
                i = element(i);
            } else {
                ++i;
            }
        }
        for (auto it = open_.rbegin(); it != open_.rend(); ++it) {
            reportNotClosed(*it);
        }
        buildSource();
        return std::move(result_);
    }

private:
    struct Attribute {
        std::string_view name;
        std::size_t nameStart = 0;
        std::size_t valueStart = 0;
        std::size_t valueEnd = 0;
    };

    [[nodiscard]] bool lookingAt(std::size_t offset, std::string_view prefix) const {
        return text_.substr(offset, prefix.size()) == prefix;
    }

    struct OpenElement {
        std::string_view name;
        std::size_t nameStart = 0;
    };

    void report(std::size_t start, std::size_t end, std::string message) {
        result_.diagnostics.push_back({rangeOf(start, end), std::move(message)});
    }

    void reportNotClosed(const OpenElement& element) {
        report(element.nameStart, element.nameStart + element.name.size(),
               "Element '<" + std::string{element.name} + ">' is not closed: '</" +
                   std::string{element.name} + ">' is missing.");
    }

    std::size_t skipPastOrReport(std::size_t offset, std::size_t openerSize, std::string_view terminator,
                                 std::string message) {
        if (text_.find(terminator, offset + openerSize) == std::string_view::npos) {
            report(offset, offset + openerSize, std::move(message));
            return text_.size();
        }
        return skipPast(offset + openerSize, terminator);
    }

    // An end tag such as </s:Group>. It closes the matching open element, and reports the elements inside
    // it that were left open.
    std::size_t endTag(std::size_t start) {
        std::size_t i = start + 2;
        const std::size_t nameStart = i;
        while (i < text_.size() && isNameChar(text_[i])) {
            ++i;
        }
        const std::string_view name = text_.substr(nameStart, i - nameStart);
        const std::size_t end = skipPast(i, ">");
        const auto match = std::find_if(open_.rbegin(), open_.rend(),
                                        [&](const OpenElement& element) { return element.name == name; });
        if (match == open_.rend()) {
            report(start, end,
                   "Unexpected end tag '</" + std::string{name} + ">': no element '<" + std::string{name} +
                       ">' is open.");
            return end;
        }
        while (open_.back().name != name) {
            reportNotClosed(open_.back());
            open_.pop_back();
        }
        open_.pop_back();
        return end;
    }

    [[nodiscard]] std::size_t skipPast(std::size_t offset, std::string_view terminator) const {
        const auto end = text_.find(terminator, offset);
        return end == std::string_view::npos ? text_.size() : end + terminator.size();
    }

    [[nodiscard]] model::Position positionAt(std::size_t offset) const {
        const auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset);
        const auto line = static_cast<std::size_t>(std::distance(lineStarts_.begin(), it) - 1);
        const std::size_t start = lineStarts_[line];
        return {static_cast<std::uint32_t>(line),
                static_cast<std::uint32_t>(util::utf16Length(text_.substr(start, offset - start)))};
    }

    [[nodiscard]] model::Range rangeOf(std::size_t start, std::size_t end) const {
        return {positionAt(start), positionAt(end)};
    }

    void addCode(std::size_t start, std::size_t end) {
        if (start < end) {
            code_.emplace_back(start, end);
            result_.codeRanges.push_back(rangeOf(start, end));
        }
    }

    // Reads a start tag at `start` and returns the offset after the element or after its start tag.
    std::size_t element(std::size_t start) {
        std::size_t i = start + 1;
        const std::size_t nameStart = i;
        while (i < text_.size() && isNameChar(text_[i])) {
            ++i;
        }
        const std::string_view qualifiedName = text_.substr(nameStart, i - nameStart);
        std::vector<Attribute> attributes;
        bool selfClosing = false;
        while (i < text_.size()) {
            while (i < text_.size() && isSpace(text_[i])) {
                ++i;
            }
            if (lookingAt(i, "/>")) {
                selfClosing = true;
                i += 2;
                break;
            }
            if (i >= text_.size() || text_[i] == '>') {
                ++i;
                break;
            }
            Attribute attribute;
            attribute.nameStart = i;
            while (i < text_.size() && isNameChar(text_[i])) {
                ++i;
            }
            attribute.name = text_.substr(attribute.nameStart, i - attribute.nameStart);
            while (i < text_.size() && isSpace(text_[i])) {
                ++i;
            }
            if (attribute.name.empty()) {
                if (text_[i] == '<') {
                    report(start, start + 1 + qualifiedName.size(),
                           "Start tag '<" + std::string{qualifiedName} + ">' is not closed: '>' is missing.");
                    break;
                }
                report(i, i + 1, "Unexpected character in tag '<" + std::string{qualifiedName} + ">'.");
                ++i;
                continue;
            }
            if (i >= text_.size() || text_[i] != '=') {
                report(attribute.nameStart, attribute.nameStart + attribute.name.size(),
                       "Attribute '" + std::string{attribute.name} + "' has no value: '=' is missing.");
                continue;
            }
            ++i;
            while (i < text_.size() && isSpace(text_[i])) {
                ++i;
            }
            if (i >= text_.size() || (text_[i] != '"' && text_[i] != '\'')) {
                report(attribute.nameStart, attribute.nameStart + attribute.name.size(),
                       "The value of attribute '" + std::string{attribute.name} + "' must be in quotes.");
                while (i < text_.size() && !isSpace(text_[i]) && text_[i] != '>' && !lookingAt(i, "/>")) {
                    ++i;
                }
                continue;
            }
            const char quote = text_[i++];
            attribute.valueStart = i;
            while (i < text_.size() && text_[i] != quote) {
                ++i;
            }
            attribute.valueEnd = i;
            if (i < text_.size()) {
                ++i;
            } else {
                report(attribute.valueStart - 1, attribute.valueStart,
                       "The value of attribute '" + std::string{attribute.name} + "' is not closed.");
            }
            attributes.push_back(attribute);
        }
        const std::size_t tagEnd = i;
        if (i >= text_.size() && !text_.ends_with('>')) {
            report(start, start + 1 + qualifiedName.size(),
                   "Start tag '<" + std::string{qualifiedName} + ">' is not closed: '>' is missing.");
        }

        for (const auto& attribute : attributes) {
            if (attribute.name == "xmlns" || attribute.name.starts_with("xmlns:")) {
                const auto prefix = attribute.name.size() > 5 ? attribute.name.substr(6) : std::string_view{};
                namespaces_[std::string{prefix}] =
                    text_.substr(attribute.valueStart, attribute.valueEnd - attribute.valueStart);
            }
        }
        const auto colon = qualifiedName.find(':');
        const std::string_view prefix =
            colon == std::string_view::npos ? std::string_view{} : qualifiedName.substr(0, colon);
        const std::string_view localName =
            colon == std::string_view::npos ? qualifiedName : qualifiedName.substr(colon + 1);
        const auto ns = namespaces_.find(std::string{prefix});
        const std::string_view uri =
            ns == namespaces_.end() ? std::string_view{} : std::string_view{ns->second};

        if ((uri.empty() || isLanguageNamespace(uri)) &&
            (localName == "Script" || localName == "Metadata" || localName == "Style")) {
            return selfClosing ? tagEnd
                               : embeddedBlock(tagEnd, nameStart, qualifiedName, localName != "Style");
        }

        const auto type = context_.classOf(uri, localName, result_.type.package);
        const bool isRoot = !rootSeen_;
        rootSeen_ = true;
        const model::Range nameRange = rangeOf(nameStart, nameStart + qualifiedName.size());
        if (isRoot) {
            result_.type.selectionRange = nameRange;
            result_.type.baseClass = type.value_or("");
        }
        if (type) {
            result_.references.push_back({nameRange, *type, {}});
        }
        for (const auto& attribute : attributes) {
            const std::string_view value =
                text_.substr(attribute.valueStart, attribute.valueEnd - attribute.valueStart);
            if (attribute.name == "xmlns" || attribute.name.find(':') != std::string_view::npos) {
                continue;
            }
            if (attribute.name == "id" && !isRoot) {
                model::Member member;
                member.name = value;
                member.kind = model::MemberKind::Variable;
                member.visibility = model::Visibility::Public;
                member.type = type.value_or("");
                member.range = rangeOf(start, tagEnd);
                member.selectionRange = rangeOf(attribute.valueStart, attribute.valueEnd);
                result_.type.members.push_back(std::move(member));
                continue;
            }
            if (type) {
                result_.references.push_back(
                    {rangeOf(attribute.nameStart, attribute.nameStart + attribute.name.size()), *type,
                     std::string{attribute.name}});
            }
            attributeCode(attribute, value);
        }
        if (!selfClosing) {
            open_.push_back({qualifiedName, nameStart});
        }
        return tagEnd;
    }

    void attributeCode(const Attribute& attribute, std::string_view value) {
        if (!value.starts_with('{') && isEventCode(attribute.name, value)) {
            addCode(attribute.valueStart, attribute.valueEnd);
            // The closing quote becomes a semicolon, so the statement ends there.
            replacements_[attribute.valueEnd] = ';';
            result_.eventRanges.push_back(rangeOf(attribute.valueStart, attribute.valueEnd));
            return;
        }
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (value[i] != '{') {
                continue;
            }
            int depth = 0;
            std::size_t end = i;
            for (; end < value.size(); ++end) {
                if (value[end] == '{') {
                    ++depth;
                } else if (value[end] == '}' && --depth == 0) {
                    break;
                }
            }
            if (end >= value.size()) {
                return;
            }
            addCode(attribute.valueStart + i, attribute.valueStart + end + 1);
            i = end;
        }
    }

    // A <fx:Script>, <fx:Metadata> or <fx:Style> element. Returns the offset after its end tag.
    std::size_t embeddedBlock(std::size_t contentStart, std::size_t nameStart, std::string_view qualifiedName,
                              bool isCode) {
        const std::string closing = "</" + std::string{qualifiedName};
        std::size_t contentEnd = text_.find(closing, contentStart);
        std::size_t after = 0;
        if (contentEnd == std::string_view::npos) {
            // Without its end tag, the block ends where the parent element ends.
            contentEnd = std::min(text_.find("</", contentStart), text_.size());
            after = contentEnd;
            reportNotClosed({qualifiedName, nameStart});
        } else {
            after = skipPast(contentEnd, ">");
        }
        if (!isCode) {
            return after;
        }
        const std::string_view content = text_.substr(contentStart, contentEnd - contentStart);
        std::size_t codeStart = contentStart;
        std::size_t i = 0;
        bool hasCdata = false;
        while (true) {
            const auto open = content.find("<![CDATA[", i);
            if (open == std::string_view::npos) {
                break;
            }
            hasCdata = true;
            const auto close = content.find("]]>", open);
            if (close == std::string_view::npos) {
                report(contentStart + open, contentStart + open + 9,
                       "Unterminated CDATA section: ']]>' is missing.");
            }
            const std::size_t start = contentStart + open + 9;
            const std::size_t end = close == std::string_view::npos ? contentEnd : contentStart + close;
            if (codeStart == contentStart) {
                codeStart = start;
            }
            addCode(start, end);
            if (close == std::string_view::npos) {
                break;
            }
            i = close + 3;
        }
        if (!hasCdata) {
            addCode(contentStart, contentEnd);
        }
        if (qualifiedName.ends_with("Script") && !result_.scriptStart) {
            result_.scriptStart = positionAt(codeStart);
        }
        return after;
    }

    // Copies the code ranges and replaces the markup with spaces, one per UTF-16 unit, so positions stay
    // valid.
    void buildSource() {
        std::ranges::sort(code_);
        std::string& out = result_.source;
        out.reserve(text_.size());
        std::size_t segment = 0;
        for (std::size_t i = 0; i < text_.size(); ++i) {
            while (segment < code_.size() && code_[segment].second <= i) {
                ++segment;
            }
            const char c = text_[i];
            if (segment < code_.size() && code_[segment].first <= i) {
                out.push_back(c);
            } else if (const auto replacement = replacements_.find(i); replacement != replacements_.end()) {
                out.push_back(replacement->second);
            } else if (c == '\n' || c == '\r') {
                out.push_back(c);
            } else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
                out.append(static_cast<unsigned char>(c) >= 0xF0 ? 2 : 1, ' ');
            }
        }
    }

    std::string_view text_;
    const MxmlContext& context_;
    std::vector<std::size_t> lineStarts_;
    std::map<std::string, std::string, std::less<>> namespaces_;
    std::vector<std::pair<std::size_t, std::size_t>> code_;
    std::map<std::size_t, char> replacements_;
    bool rootSeen_ = false;
    std::vector<OpenElement> open_;
    MxmlTranslation result_;
};

} // namespace

void MxmlContext::loadSdk(const fs::path& sdk) {
    const fs::path frameworks = sdk / "frameworks";
    const auto config = util::readTextFile(frameworks / "flex-config.xml");
    if (!config) {
        return;
    }
    std::string_view rest = *config;
    while (true) {
        const auto start = rest.find("<namespace>");
        if (start == std::string_view::npos) {
            break;
        }
        const auto end = rest.find("</namespace>", start);
        const std::string_view entry =
            rest.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        const auto element = [&](std::string_view name) -> std::string {
            const std::string open = "<" + std::string{name} + ">";
            const auto from = entry.find(open);
            const auto to = entry.find("</" + std::string{name} + ">");
            if (from == std::string_view::npos || to == std::string_view::npos || to < from) {
                return {};
            }
            return std::string{util::trim(entry.substr(from + open.size(), to - from - open.size()))};
        };
        const std::string uri = element("uri");
        const std::string manifest = element("manifest");
        if (!uri.empty() && !manifest.empty()) {
            if (const auto xml = util::readTextFile(frameworks / manifest)) {
                addManifest(uri, *xml);
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        rest = rest.substr(end);
    }
}

void MxmlContext::addManifest(std::string uri, std::string_view manifestXml) {
    auto& components = manifests_[std::move(uri)];
    std::size_t i = 0;
    while (true) {
        const auto start = manifestXml.find("<component", i);
        if (start == std::string_view::npos) {
            return;
        }
        const auto end = manifestXml.find('>', start);
        const std::string_view tag =
            manifestXml.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        auto id = attributeValue(tag, " id");
        auto type = attributeValue(tag, "class");
        if (id && type) {
            components.emplace(std::move(*id), std::move(*type));
        }
        if (end == std::string_view::npos) {
            return;
        }
        i = end;
    }
}

void MxmlContext::addSourceRoot(const fs::path& root) {
    std::error_code error;
    auto normal = fs::weakly_canonical(root, error);
    sourceRoots_.push_back(error ? root.lexically_normal() : normal.lexically_normal());
}

std::string MxmlContext::packageOf(const fs::path& file) const {
    std::error_code error;
    fs::path directory = fs::weakly_canonical(file, error).parent_path();
    if (error) {
        directory = file.lexically_normal().parent_path();
    }
    std::optional<fs::path> best;
    for (const auto& root : sourceRoots_) {
        const auto relative = directory.lexically_relative(root);
        if (!relative.empty() && *relative.begin() != ".." &&
            (!best || root.string().size() > best->string().size())) {
            best = relative;
        }
    }
    fs::path relative = best.value_or(fs::path{});
    if (!best) {
        // Without a known source root, the conventional "src" directory is the root.
        std::vector<fs::path> parts{directory.begin(), directory.end()};
        const auto src = std::find(parts.rbegin(), parts.rend(), fs::path{"src"});
        if (src != parts.rend()) {
            for (auto it = src.base(); it != parts.end(); ++it) {
                relative /= *it;
            }
        }
    }
    std::string package;
    for (const auto& part : relative) {
        if (part == ".") {
            continue;
        }
        if (!package.empty()) {
            package.push_back('.');
        }
        package += part.string();
    }
    return package;
}

std::optional<std::string> MxmlContext::classOf(std::string_view namespaceUri, std::string_view localName,
                                                std::string_view filePackage) const {
    if (namespaceUri == "*") {
        return filePackage.empty() ? std::string{localName}
                                   : std::string{filePackage} + "." + std::string{localName};
    }
    if (namespaceUri.ends_with(".*")) {
        return std::string{namespaceUri.substr(0, namespaceUri.size() - 1)} + std::string{localName};
    }
    const auto manifest = manifests_.find(std::string{namespaceUri});
    if (manifest == manifests_.end()) {
        return std::nullopt;
    }
    const auto component = manifest->second.find(std::string{localName});
    if (component == manifest->second.end()) {
        return std::nullopt;
    }
    return component->second;
}

MxmlTranslation translateMxml(std::string_view text, const fs::path& file, const MxmlContext& context) {
    if (text.starts_with("\xEF\xBB\xBF")) {
        text.remove_prefix(3);
    }
    return Translator{text, file, context}.run();
}

std::shared_ptr<model::Unit> parseMxml(std::string_view text, const fs::path& file,
                                       const MxmlContext& context, const ParseOptions& options,
                                       std::string* source, LexResult* lex) {
    MxmlTranslation translation = translateMxml(text, file, context);
    std::string localSource;
    LexResult localLex;
    std::string& target = source ? *source : localSource;
    LexResult& tokens = lex ? *lex : localLex;
    target = std::move(translation.source);
    tokens = tokenize(target);

    auto unit = parseClassBody(target, tokens, std::move(translation.type), options);
    unit->isMarkup = true;
    unit->codeRanges = std::move(translation.codeRanges);
    unit->references = std::move(translation.references);
    unit->scriptStart = translation.scriptStart;
    if (options.collectDiagnostics) {
        auto& diagnostics = unit->diagnostics;
        diagnostics.insert(diagnostics.end(), translation.diagnostics.begin(), translation.diagnostics.end());
        std::ranges::stable_sort(diagnostics, {}, [](const model::Diagnostic& d) { return d.range.start; });
    }
    if (options.collectScopes) {
        for (const auto& range : translation.eventRanges) {
            model::Scope scope;
            scope.range = range;
            model::Local event;
            event.name = "event";
            event.type = eventType;
            event.kind = model::LocalKind::Parameter;
            event.selectionRange = range;
            scope.locals.push_back(std::move(event));
            unit->scopes.push_back(std::move(scope));
        }
    }
    return unit;
}

bool isMxmlFile(const fs::path& path) {
    return util::iequals(path.extension().string(), ".mxml");
}

} // namespace as3lsp::syntax
