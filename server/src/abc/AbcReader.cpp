#include "abc/AbcReader.hpp"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

namespace as3lsp::abc {

namespace {

struct AbcError {};

namespace NamespaceKind {
constexpr std::uint8_t Namespace = 0x08;
constexpr std::uint8_t Package = 0x16;
constexpr std::uint8_t PackageInternal = 0x17;
constexpr std::uint8_t Protected = 0x18;
constexpr std::uint8_t Explicit = 0x19;
constexpr std::uint8_t StaticProtected = 0x1A;
} // namespace NamespaceKind

namespace MultinameKind {
constexpr std::uint8_t QName = 0x07;
constexpr std::uint8_t QNameA = 0x0D;
constexpr std::uint8_t RTQName = 0x0F;
constexpr std::uint8_t RTQNameA = 0x10;
constexpr std::uint8_t RTQNameL = 0x11;
constexpr std::uint8_t RTQNameLA = 0x12;
constexpr std::uint8_t Multiname = 0x09;
constexpr std::uint8_t MultinameA = 0x0E;
constexpr std::uint8_t MultinameL = 0x1B;
constexpr std::uint8_t MultinameLA = 0x1C;
constexpr std::uint8_t TypeName = 0x1D;
} // namespace MultinameKind

namespace TraitKind {
constexpr std::uint8_t Slot = 0;
constexpr std::uint8_t Method = 1;
constexpr std::uint8_t Getter = 2;
constexpr std::uint8_t Setter = 3;
constexpr std::uint8_t Class = 4;
constexpr std::uint8_t Function = 5;
constexpr std::uint8_t Const = 6;
} // namespace TraitKind

constexpr std::uint8_t methodNeedRest = 0x04;
constexpr std::uint8_t methodHasOptional = 0x08;
constexpr std::uint8_t methodHasParamNames = 0x80;
constexpr std::uint8_t instanceInterface = 0x04;
constexpr std::uint8_t instanceProtectedNs = 0x08;
constexpr std::uint8_t traitHasMetadata = 0x04;
constexpr std::uint8_t traitFinal = 0x01;
constexpr std::uint8_t traitOverride = 0x02;

constexpr std::string_view builtinNamespace = "http://adobe.com/AS3/2006/builtin";
constexpr std::string_view vectorClass = "__AS3__.vec.Vector";

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> data) : data_(data) {}

    std::uint8_t u8() {
        require(1);
        return data_[pos_++];
    }

    std::uint16_t u16() {
        require(2);
        const auto value = static_cast<std::uint16_t>(data_[pos_] | (data_[pos_ + 1] << 8));
        pos_ += 2;
        return value;
    }

    // Variable-length unsigned integer, used for u30, u32 and s32 values.
    std::uint32_t u32() {
        std::uint32_t result = 0;
        for (int i = 0; i < 5; ++i) {
            const std::uint8_t byte = u8();
            result |= static_cast<std::uint32_t>(byte & 0x7F) << (7 * i);
            if ((byte & 0x80) == 0) {
                break;
            }
        }
        return result;
    }

    std::int32_t s32() { return static_cast<std::int32_t>(u32()); }

    double d64() {
        require(8);
        std::uint64_t bits = 0;
        for (int i = 7; i >= 0; --i) {
            bits = (bits << 8) | data_[pos_ + static_cast<std::size_t>(i)];
        }
        pos_ += 8;
        return std::bit_cast<double>(bits);
    }

    std::string_view bytes(std::size_t count) {
        require(count);
        const auto* start = reinterpret_cast<const char*>(data_.data() + pos_);
        pos_ += count;
        return {start, count};
    }

    void skipU32(std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            u32();
        }
    }

private:
    void require(std::size_t count) const {
        if (pos_ + count > data_.size()) {
            throw AbcError{};
        }
    }

    std::span<const std::uint8_t> data_;
    std::size_t pos_ = 0;
};

struct NamespaceInfo {
    std::uint8_t kind = 0;
    std::uint32_t name = 0;
};

struct MultinameInfo {
    std::uint8_t kind = 0;
    std::uint32_t ns = 0;
    std::uint32_t name = 0;
    std::uint32_t nsSet = 0;
    std::uint32_t base = 0;
    std::vector<std::uint32_t> params;
};

struct MethodInfo {
    std::vector<std::uint32_t> paramTypes;
    std::uint32_t returnType = 0;
    std::uint8_t flags = 0;
    std::vector<std::pair<std::uint32_t, std::uint8_t>> optionals;
    std::vector<std::uint32_t> paramNames;
};

struct TraitInfo {
    std::uint32_t name = 0;
    std::uint8_t kind = 0;
    std::uint8_t attributes = 0;
    // Slot type, class index or method index, depending on the kind.
    std::uint32_t index = 0;
};

struct InstanceInfo {
    std::uint32_t name = 0;
    std::uint32_t superName = 0;
    std::uint8_t flags = 0;
    std::vector<std::uint32_t> interfaces;
    std::uint32_t init = 0;
    std::vector<TraitInfo> traits;
};

struct ClassInfo {
    std::vector<TraitInfo> traits;
};

class AbcFile {
public:
    explicit AbcFile(std::span<const std::uint8_t> data) : reader_(data) {}

    void read(model::Unit& unit) {
        reader_.u16();
        reader_.u16();
        readConstantPool();
        readMethods();
        readMetadata();
        const std::uint32_t classCount = reader_.u32();
        instances_.resize(classCount);
        for (auto& instance : instances_) {
            readInstance(instance);
        }
        classes_.resize(classCount);
        for (auto& info : classes_) {
            reader_.u32();
            info.traits = readTraits();
        }
        const std::uint32_t scriptCount = reader_.u32();
        for (std::uint32_t i = 0; i < scriptCount; ++i) {
            reader_.u32();
            for (const auto& trait : readTraits()) {
                addScriptTrait(trait, unit);
            }
        }
    }

private:
    void readConstantPool() {
        ints_.assign(1, 0);
        for (std::uint32_t i = 1, n = reader_.u32(); i < n; ++i) {
            ints_.push_back(reader_.s32());
        }
        uints_.assign(1, 0);
        for (std::uint32_t i = 1, n = reader_.u32(); i < n; ++i) {
            uints_.push_back(reader_.u32());
        }
        doubles_.assign(1, 0.0);
        for (std::uint32_t i = 1, n = reader_.u32(); i < n; ++i) {
            doubles_.push_back(reader_.d64());
        }
        strings_.assign(1, std::string_view{});
        for (std::uint32_t i = 1, n = reader_.u32(); i < n; ++i) {
            strings_.push_back(reader_.bytes(reader_.u32()));
        }
        namespaces_.assign(1, NamespaceInfo{});
        for (std::uint32_t i = 1, n = reader_.u32(); i < n; ++i) {
            NamespaceInfo ns;
            ns.kind = reader_.u8();
            ns.name = reader_.u32();
            namespaces_.push_back(ns);
        }
        namespaceSets_.assign(1, {});
        for (std::uint32_t i = 1, n = reader_.u32(); i < n; ++i) {
            std::vector<std::uint32_t> set(reader_.u32());
            for (auto& ns : set) {
                ns = reader_.u32();
            }
            namespaceSets_.push_back(std::move(set));
        }
        multinames_.assign(1, MultinameInfo{});
        for (std::uint32_t i = 1, n = reader_.u32(); i < n; ++i) {
            multinames_.push_back(readMultiname());
        }
    }

    MultinameInfo readMultiname() {
        MultinameInfo mn;
        mn.kind = reader_.u8();
        switch (mn.kind) {
        case MultinameKind::QName:
        case MultinameKind::QNameA:
            mn.ns = reader_.u32();
            mn.name = reader_.u32();
            break;
        case MultinameKind::RTQName:
        case MultinameKind::RTQNameA:
            mn.name = reader_.u32();
            break;
        case MultinameKind::RTQNameL:
        case MultinameKind::RTQNameLA:
            break;
        case MultinameKind::Multiname:
        case MultinameKind::MultinameA:
            mn.name = reader_.u32();
            mn.nsSet = reader_.u32();
            break;
        case MultinameKind::MultinameL:
        case MultinameKind::MultinameLA:
            mn.nsSet = reader_.u32();
            break;
        case MultinameKind::TypeName:
            mn.base = reader_.u32();
            mn.params.resize(reader_.u32());
            for (auto& param : mn.params) {
                param = reader_.u32();
            }
            break;
        default:
            throw AbcError{};
        }
        return mn;
    }

    void readMethods() {
        methods_.resize(reader_.u32());
        for (auto& method : methods_) {
            const std::uint32_t paramCount = reader_.u32();
            method.returnType = reader_.u32();
            method.paramTypes.resize(paramCount);
            for (auto& type : method.paramTypes) {
                type = reader_.u32();
            }
            reader_.u32();
            method.flags = reader_.u8();
            if (method.flags & methodHasOptional) {
                method.optionals.resize(reader_.u32());
                for (auto& [value, kind] : method.optionals) {
                    value = reader_.u32();
                    kind = reader_.u8();
                }
            }
            if (method.flags & methodHasParamNames) {
                method.paramNames.resize(paramCount);
                for (auto& name : method.paramNames) {
                    name = reader_.u32();
                }
            }
        }
    }

    void readMetadata() {
        for (std::uint32_t i = 0, n = reader_.u32(); i < n; ++i) {
            reader_.u32();
            reader_.skipU32(2 * static_cast<std::size_t>(reader_.u32()));
        }
    }

    void readInstance(InstanceInfo& instance) {
        instance.name = reader_.u32();
        instance.superName = reader_.u32();
        instance.flags = reader_.u8();
        if (instance.flags & instanceProtectedNs) {
            reader_.u32();
        }
        instance.interfaces.resize(reader_.u32());
        for (auto& interfaceName : instance.interfaces) {
            interfaceName = reader_.u32();
        }
        instance.init = reader_.u32();
        instance.traits = readTraits();
    }

    std::vector<TraitInfo> readTraits() {
        std::vector<TraitInfo> traits(reader_.u32());
        for (auto& trait : traits) {
            trait.name = reader_.u32();
            const std::uint8_t kind = reader_.u8();
            trait.kind = kind & 0x0F;
            trait.attributes = kind >> 4;
            switch (trait.kind) {
            case TraitKind::Slot:
            case TraitKind::Const:
                reader_.u32();
                trait.index = reader_.u32();
                if (reader_.u32() != 0) {
                    reader_.u8();
                }
                break;
            case TraitKind::Class:
            case TraitKind::Function:
            case TraitKind::Method:
            case TraitKind::Getter:
            case TraitKind::Setter:
                reader_.u32();
                trait.index = reader_.u32();
                break;
            default:
                throw AbcError{};
            }
            if (trait.attributes & traitHasMetadata) {
                reader_.skipU32(reader_.u32());
            }
        }
        return traits;
    }

    [[nodiscard]] std::string_view string(std::uint32_t index) const {
        return index < strings_.size() ? strings_[index] : std::string_view{};
    }

    [[nodiscard]] const NamespaceInfo& namespaceAt(std::uint32_t index) const {
        return index < namespaces_.size() ? namespaces_[index] : namespaces_.front();
    }

    [[nodiscard]] const MultinameInfo& multiname(std::uint32_t index) const {
        return index < multinames_.size() ? multinames_[index] : multinames_.front();
    }

    // The namespace of a name, choosing the first package namespace of a namespace set.
    [[nodiscard]] const NamespaceInfo& namespaceOf(const MultinameInfo& mn) const {
        if (mn.kind == MultinameKind::QName || mn.kind == MultinameKind::QNameA) {
            return namespaceAt(mn.ns);
        }
        if (mn.nsSet < namespaceSets_.size()) {
            for (std::uint32_t ns : namespaceSets_[mn.nsSet]) {
                if (namespaceAt(ns).kind == NamespaceKind::Package) {
                    return namespaceAt(ns);
                }
            }
        }
        return namespaces_.front();
    }

    [[nodiscard]] static bool isPackageNamespace(const NamespaceInfo& ns) {
        return ns.kind == NamespaceKind::Package || ns.kind == NamespaceKind::PackageInternal;
    }

    [[nodiscard]] std::string typeName(std::uint32_t index) const {
        if (index == 0 || index >= multinames_.size()) {
            return "*";
        }
        const MultinameInfo& mn = multinames_[index];
        if (mn.kind == MultinameKind::TypeName) {
            std::string base = typeName(mn.base);
            if (base == vectorClass) {
                base = "Vector";
            }
            if (mn.params.empty()) {
                return base;
            }
            return base + ".<" + typeName(mn.params.front()) + ">";
        }
        const std::string_view name = string(mn.name);
        const NamespaceInfo& ns = namespaceOf(mn);
        const std::string_view package = string(ns.name);
        if (!isPackageNamespace(ns) || package.empty()) {
            return std::string{name};
        }
        std::string result{package};
        result.push_back('.');
        result.append(name);
        return result;
    }

    [[nodiscard]] std::string formatDouble(double value) const {
        if (std::isnan(value)) {
            return "NaN";
        }
        if (std::isinf(value)) {
            return value > 0 ? "Infinity" : "-Infinity";
        }
        char buffer[32];
        if (value == std::floor(value) && std::fabs(value) < 1e15) {
            std::snprintf(buffer, sizeof(buffer), "%.0f", value);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%g", value);
        }
        return buffer;
    }

    [[nodiscard]] std::string defaultValue(std::uint32_t index, std::uint8_t kind) const {
        switch (kind) {
        case 0x00:
            return "undefined";
        case 0x01:
            return "\"" + std::string{string(index)} + "\"";
        case 0x03:
            return index < ints_.size() ? std::to_string(ints_[index]) : "0";
        case 0x04:
            return index < uints_.size() ? std::to_string(uints_[index]) : "0";
        case 0x06:
            return index < doubles_.size() ? formatDouble(doubles_[index]) : "0";
        case 0x0A:
            return "false";
        case 0x0B:
            return "true";
        case 0x0C:
            return "null";
        default:
            return std::string{string(namespaceAt(index).name)};
        }
    }

    [[nodiscard]] std::vector<model::Parameter> parameters(const MethodInfo& method) const {
        std::vector<model::Parameter> params(method.paramTypes.size());
        const std::size_t firstOptional = params.size() - std::min(params.size(), method.optionals.size());
        for (std::size_t i = 0; i < params.size(); ++i) {
            auto& param = params[i];
            param.name =
                i < method.paramNames.size() ? std::string{string(method.paramNames[i])} : std::string{};
            if (param.name.empty()) {
                param.name = "arg" + std::to_string(i + 1);
            }
            param.type = typeName(method.paramTypes[i]);
            if (i >= firstOptional) {
                const auto& [value, kind] = method.optionals[i - firstOptional];
                param.defaultValue = defaultValue(value, kind);
            }
        }
        if (method.flags & methodNeedRest) {
            model::Parameter rest;
            rest.name = "rest";
            rest.isRest = true;
            params.push_back(std::move(rest));
        }
        return params;
    }

    [[nodiscard]] const MethodInfo* method(std::uint32_t index) const {
        return index < methods_.size() ? &methods_[index] : nullptr;
    }

    // Returns nullopt for members that code outside the declaring class cannot use.
    [[nodiscard]] std::optional<model::Visibility> visibility(const NamespaceInfo& ns,
                                                              bool inInterface) const {
        switch (ns.kind) {
        case NamespaceKind::Package:
            return model::Visibility::Public;
        case NamespaceKind::Protected:
        case NamespaceKind::StaticProtected:
            return model::Visibility::Protected;
        case NamespaceKind::Namespace:
        case NamespaceKind::Explicit:
            if (inInterface || string(ns.name) == builtinNamespace) {
                return model::Visibility::Public;
            }
            return std::nullopt;
        default:
            return std::nullopt;
        }
    }

    std::optional<model::Member> makeMember(const TraitInfo& trait, bool inInterface, bool isStatic) const {
        const MultinameInfo& mn = multiname(trait.name);
        const auto access = visibility(namespaceOf(mn), inInterface);
        if (!access) {
            return std::nullopt;
        }
        model::Member member;
        member.name = string(mn.name);
        member.visibility = *access;
        member.isStatic = isStatic;
        member.isFinal = (trait.attributes & traitFinal) != 0;
        member.isOverride = (trait.attributes & traitOverride) != 0;
        switch (trait.kind) {
        case TraitKind::Slot:
        case TraitKind::Const:
            member.kind =
                trait.kind == TraitKind::Slot ? model::MemberKind::Variable : model::MemberKind::Constant;
            member.type = typeName(trait.index);
            return member;
        case TraitKind::Method:
        case TraitKind::Function:
        case TraitKind::Getter:
        case TraitKind::Setter: {
            const MethodInfo* info = method(trait.index);
            if (!info) {
                return std::nullopt;
            }
            member.kind = trait.kind == TraitKind::Getter   ? model::MemberKind::Getter
                          : trait.kind == TraitKind::Setter ? model::MemberKind::Setter
                                                            : model::MemberKind::Method;
            member.params = parameters(*info);
            member.type = typeName(info->returnType);
            return member;
        }
        default:
            return std::nullopt;
        }
    }

    static void addMember(std::vector<model::Member>& members, model::Member member) {
        for (const auto& existing : members) {
            if (existing.name == member.name && existing.kind == member.kind &&
                existing.isStatic == member.isStatic) {
                return;
            }
        }
        members.push_back(std::move(member));
    }

    void addScriptTrait(const TraitInfo& trait, model::Unit& unit) const {
        const MultinameInfo& mn = multiname(trait.name);
        const NamespaceInfo& ns = namespaceOf(mn);
        if (ns.kind != NamespaceKind::Package) {
            return;
        }
        if (trait.kind == TraitKind::Class) {
            if (trait.index < instances_.size()) {
                unit.types.push_back(makeType(instances_[trait.index], classes_[trait.index]));
            }
            return;
        }
        if (auto member = makeMember(trait, false, false)) {
            model::Global global;
            global.package = string(ns.name);
            global.decl = std::move(*member);
            unit.globals.push_back(std::move(global));
        }
    }

    model::TypeDecl makeType(const InstanceInfo& instance, const ClassInfo& info) const {
        model::TypeDecl type;
        const MultinameInfo& mn = multiname(instance.name);
        type.name = string(mn.name);
        type.package = string(namespaceOf(mn).name);
        type.kind =
            (instance.flags & instanceInterface) ? model::TypeKind::Interface : model::TypeKind::Class;
        type.visibility = model::Visibility::Public;
        type.isFinal = (instance.flags & 0x02) != 0;
        type.isDynamic = (instance.flags & 0x01) == 0;
        if (instance.superName != 0 && !type.isInterface()) {
            type.baseClass = typeName(instance.superName);
        }
        for (std::uint32_t interfaceName : instance.interfaces) {
            type.interfaces.push_back(typeName(interfaceName));
        }
        if (!type.isInterface()) {
            if (const MethodInfo* init = method(instance.init)) {
                model::Member constructor;
                constructor.name = type.name;
                constructor.kind = model::MemberKind::Constructor;
                constructor.visibility = model::Visibility::Public;
                constructor.params = parameters(*init);
                type.members.push_back(std::move(constructor));
            }
        }
        for (const auto& trait : instance.traits) {
            if (auto member = makeMember(trait, type.isInterface(), false)) {
                addMember(type.members, std::move(*member));
            }
        }
        for (const auto& trait : info.traits) {
            if (auto member = makeMember(trait, false, true)) {
                addMember(type.members, std::move(*member));
            }
        }
        return type;
    }

    ByteReader reader_;
    std::vector<std::int32_t> ints_;
    std::vector<std::uint32_t> uints_;
    std::vector<double> doubles_;
    std::vector<std::string_view> strings_;
    std::vector<NamespaceInfo> namespaces_;
    std::vector<std::vector<std::uint32_t>> namespaceSets_;
    std::vector<MultinameInfo> multinames_;
    std::vector<MethodInfo> methods_;
    std::vector<InstanceInfo> instances_;
    std::vector<ClassInfo> classes_;
};

} // namespace

bool readAbc(std::span<const std::uint8_t> data, model::Unit& unit) {
    try {
        AbcFile{data}.read(unit);
        return true;
    } catch (const AbcError&) {
        return false;
    }
}

} // namespace as3lsp::abc
