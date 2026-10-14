#pragma once

#include "model/Model.hpp"

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace as3lsp::index {

struct StringHash {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view text) const noexcept {
        return std::hash<std::string_view>{}(text);
    }
};

template <typename T> using StringMap = std::unordered_map<std::string, T, StringHash, std::equal_to<>>;

// Global symbol table of all parsed source files and loaded libraries.
// When two units declare the same name, the declaration with the highest origin wins.
class Index {
public:
    // Adds a unit, replacing any unit with the same key.
    void add(model::UnitPtr unit);
    void remove(std::string_view key);

    [[nodiscard]] model::UnitPtr unit(std::string_view key) const;
    [[nodiscard]] std::size_t unitCount() const { return units_.size(); }

    [[nodiscard]] const model::TypeDecl* findType(std::string_view qualifiedName) const;
    [[nodiscard]] const model::Global* findGlobal(std::string_view qualifiedName) const;

    // True when `name` is a package, or the parent of a package.
    [[nodiscard]] bool hasPackage(std::string_view name) const;
    // Names of the direct children of `parent`. An empty parent lists the top-level packages.
    [[nodiscard]] std::vector<std::string> subpackages(std::string_view parent) const;

    void forEachType(const std::function<void(const model::TypeDecl&)>& visit) const;
    void forEachGlobal(const std::function<void(const model::Global&)>& visit) const;
    void forEachUnit(const std::function<void(const model::Unit&)>& visit) const;

private:
    template <typename T> struct Entry {
        const T* decl = nullptr;
        model::Origin origin = model::Origin::Library;
    };

    template <typename T> [[nodiscard]] static const T* best(const std::vector<Entry<T>>& entries);

    void insert(const model::Unit& unit);
    void erase(const model::Unit& unit);
    void addPackage(const std::string& package);
    void removePackage(const std::string& package);

    StringMap<model::UnitPtr> units_;
    StringMap<std::vector<Entry<model::TypeDecl>>> types_;
    StringMap<std::vector<Entry<model::Global>>> globals_;
    // Number of declarations in each package.
    std::map<std::string, int, std::less<>> packages_;
};

} // namespace as3lsp::index
