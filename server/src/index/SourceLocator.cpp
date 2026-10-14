#include "index/SourceLocator.hpp"

#include "syntax/Parser.hpp"
#include "util/Files.hpp"
#include "util/Strings.hpp"
#include "util/Uri.hpp"

#include <algorithm>

namespace as3lsp::index {

namespace fs = std::filesystem;

SourceLocator::SourceLocator(std::vector<fs::path> roots, std::shared_ptr<const syntax::MxmlContext> mxml)
    : roots_(std::move(roots)), mxml_(std::move(mxml)) {}

std::vector<fs::path> SourceLocator::flexSourceRoots(const fs::path& sdk) {
    std::vector<fs::path> roots;
    std::error_code error;
    for (const auto& project : fs::directory_iterator{sdk / "frameworks" / "projects", error}) {
        if (const auto src = project.path() / "src"; fs::is_directory(src, error)) {
            roots.push_back(src);
        }
    }
    std::ranges::sort(roots);
    return roots;
}

void SourceLocator::scan() const {
    scanned_ = true;
    for (const auto& root : roots_) {
        std::error_code error;
        fs::recursive_directory_iterator it{root, error};
        for (const fs::recursive_directory_iterator end; !error && it != end; it.increment(error)) {
            const fs::path& path = it->path();
            const std::string extension = path.extension().string();
            if (extension != ".as" && extension != ".mxml") {
                continue;
            }
            fs::path relative = path.lexically_relative(root);
            relative.replace_extension();
            std::string qualified;
            for (const auto& part : relative) {
                if (!qualified.empty()) {
                    qualified.push_back('.');
                }
                qualified += part.string();
            }
            // When two projects declare the same class, the first root in alphabetical order wins.
            files_.emplace(std::move(qualified), path);
        }
    }
}

const model::TypeDecl* SourceLocator::findType(std::string_view qualifiedName) const {
    if (!scanned_) {
        scan();
    }
    const auto file = files_.find(std::string{qualifiedName});
    if (file == files_.end()) {
        return nullptr;
    }
    const std::string key = util::pathKey(file->second);
    auto cached = units_.find(key);
    if (cached == units_.end()) {
        model::UnitPtr unit;
        if (const auto text = util::readTextFile(file->second)) {
            auto parsed = syntax::isMxmlFile(file->second)
                              ? syntax::parseMxml(*text, file->second, *mxml_, {.collectScopes = false})
                              : syntax::parse(*text, {.collectScopes = false});
            parsed->key = key;
            parsed->uri = util::pathToUri(file->second);
            parsed->origin = model::Origin::Library;
            unit = std::move(parsed);
        }
        cached = units_.emplace(key, std::move(unit)).first;
    }
    if (!cached->second) {
        return nullptr;
    }
    for (const auto& type : cached->second->types) {
        if (type.qualifiedName() == qualifiedName) {
            return &type;
        }
    }
    return nullptr;
}

const model::Member* SourceLocator::findMember(const model::TypeDecl& sourceType,
                                               const model::Member& compiled) {
    const model::Member* byName = nullptr;
    for (const auto& member : sourceType.members) {
        if (member.name != compiled.name || member.isStatic != compiled.isStatic) {
            continue;
        }
        if (member.kind == compiled.kind) {
            return &member;
        }
        byName = byName ? byName : &member;
    }
    return byName;
}

} // namespace as3lsp::index
