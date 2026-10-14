#include "index/Index.hpp"

#include <algorithm>

namespace as3lsp::index {

namespace {

template <typename Map, typename Decl> void eraseEntry(Map& map, const std::string& name, const Decl* decl) {
    const auto it = map.find(name);
    if (it == map.end()) {
        return;
    }
    std::erase_if(it->second, [decl](const auto& entry) { return entry.decl == decl; });
    if (it->second.empty()) {
        map.erase(it);
    }
}

} // namespace

void Index::add(model::UnitPtr unit) {
    if (!unit) {
        return;
    }
    remove(unit->key);
    insert(*unit);
    units_.emplace(unit->key, std::move(unit));
}

void Index::remove(std::string_view key) {
    const auto it = units_.find(key);
    if (it == units_.end()) {
        return;
    }
    erase(*it->second);
    units_.erase(it);
}

model::UnitPtr Index::unit(std::string_view key) const {
    const auto it = units_.find(key);
    return it == units_.end() ? nullptr : it->second;
}

template <typename T> const T* Index::best(const std::vector<Entry<T>>& entries) {
    const Entry<T>* result = nullptr;
    for (const auto& entry : entries) {
        if (!result || entry.origin > result->origin) {
            result = &entry;
        }
    }
    return result ? result->decl : nullptr;
}

const model::TypeDecl* Index::findType(std::string_view qualifiedName) const {
    const auto it = types_.find(qualifiedName);
    return it == types_.end() ? nullptr : best(it->second);
}

const model::Global* Index::findGlobal(std::string_view qualifiedName) const {
    const auto it = globals_.find(qualifiedName);
    return it == globals_.end() ? nullptr : best(it->second);
}

bool Index::hasPackage(std::string_view name) const {
    if (name.empty()) {
        return false;
    }
    const auto it = packages_.lower_bound(name);
    if (it == packages_.end() || !it->first.starts_with(name)) {
        return false;
    }
    return it->first.size() == name.size() || it->first[name.size()] == '.';
}

std::vector<std::string> Index::subpackages(std::string_view parent) const {
    std::string prefix{parent};
    if (!prefix.empty()) {
        prefix.push_back('.');
    }
    std::vector<std::string> children;
    for (auto it = packages_.lower_bound(prefix); it != packages_.end() && it->first.starts_with(prefix);
         ++it) {
        if (it->first.size() == prefix.size()) {
            continue;
        }
        const std::string_view rest = std::string_view{it->first}.substr(prefix.size());
        children.emplace_back(rest.substr(0, rest.find('.')));
    }
    std::ranges::sort(children);
    children.erase(std::unique(children.begin(), children.end()), children.end());
    return children;
}

void Index::forEachType(const std::function<void(const model::TypeDecl&)>& visit) const {
    for (const auto& [name, entries] : types_) {
        if (const auto* decl = best(entries)) {
            visit(*decl);
        }
    }
}

void Index::forEachGlobal(const std::function<void(const model::Global&)>& visit) const {
    for (const auto& [name, entries] : globals_) {
        if (const auto* decl = best(entries)) {
            visit(*decl);
        }
    }
}

void Index::forEachUnit(const std::function<void(const model::Unit&)>& visit) const {
    for (const auto& [key, unit] : units_) {
        visit(*unit);
    }
}

void Index::insert(const model::Unit& unit) {
    for (const auto& type : unit.types) {
        if (!type.isFilePrivate) {
            types_[type.qualifiedName()].push_back({&type, unit.origin});
            addPackage(type.package);
        }
    }
    for (const auto& global : unit.globals) {
        if (!global.isFilePrivate) {
            globals_[global.qualifiedName()].push_back({&global, unit.origin});
            addPackage(global.package);
        }
    }
}

void Index::erase(const model::Unit& unit) {
    for (const auto& type : unit.types) {
        if (!type.isFilePrivate) {
            eraseEntry(types_, type.qualifiedName(), &type);
            removePackage(type.package);
        }
    }
    for (const auto& global : unit.globals) {
        if (!global.isFilePrivate) {
            eraseEntry(globals_, global.qualifiedName(), &global);
            removePackage(global.package);
        }
    }
}

void Index::addPackage(const std::string& package) {
    if (!package.empty()) {
        ++packages_[package];
    }
}

void Index::removePackage(const std::string& package) {
    const auto it = packages_.find(package);
    if (it != packages_.end() && --it->second <= 0) {
        packages_.erase(it);
    }
}

} // namespace as3lsp::index
