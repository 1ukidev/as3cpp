#include "Test.hpp"
#include "analysis/Analyzer.hpp"
#include "index/SourceLocator.hpp"
#include "syntax/Parser.hpp"
#include "util/Uri.hpp"

#include <filesystem>
#include <fstream>

using namespace as3lsp;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view buttonSource = R"(package spark.components {
    /** A button with a label. */
    public class Button {
        /** The text of the button. */
        public var label:String;

        /** Moves the focus to the button. */
        public function setFocus():void {}
    }
}
)";

// A temporary Flex SDK with the source of one class.
class FakeSdk {
public:
    FakeSdk() : root_(fs::temp_directory_path() / "as3cpp-source-locator-test") {
        const fs::path directory =
            root_ / "frameworks" / "projects" / "spark" / "src" / "spark" / "components";
        fs::create_directories(directory);
        std::ofstream{directory / "Button.as"} << buttonSource;
        fs::create_directories(root_ / "frameworks" / "projects" / "empty");
    }
    FakeSdk(const FakeSdk&) = delete;
    FakeSdk& operator=(const FakeSdk&) = delete;
    ~FakeSdk() { fs::remove_all(root_); }

    [[nodiscard]] const fs::path& root() const { return root_; }
    [[nodiscard]] fs::path buttonFile() const {
        return root_ / "frameworks" / "projects" / "spark" / "src" / "spark" / "components" / "Button.as";
    }

private:
    fs::path root_;
};

// The compiled class, as the index reads it from a SWC: no source and no documentation.
model::UnitPtr compiledLibrary() {
    auto unit = syntax::parse(R"(package spark.components {
    public class Button {
        public var label:String;
        public function setFocus():void {}
    }
})",
                              {.collectScopes = false});
    unit->key = "library:spark.swc";
    unit->uri.clear();
    unit->origin = model::Origin::Library;
    for (auto& type : unit->types) {
        type.doc.clear();
        for (auto& member : type.members) {
            member.doc.clear();
        }
    }
    return unit;
}

} // namespace

TEST_CASE(sourceLocatorFindsFlexSourceRoots) {
    FakeSdk sdk;
    const auto roots = index::SourceLocator::flexSourceRoots(sdk.root());
    CHECK_EQ(roots.size(), std::size_t{1});
    CHECK_EQ(roots[0].filename().string(), "src");
}

TEST_CASE(sourceLocatorFindsCompiledClassesInSources) {
    FakeSdk sdk;
    const index::SourceLocator locator{index::SourceLocator::flexSourceRoots(sdk.root()),
                                       std::make_shared<syntax::MxmlContext>()};
    CHECK(locator.findType("spark.components.Missing") == nullptr);
    const auto* type = locator.findType("spark.components.Button");
    CHECK(type != nullptr);
    CHECK_EQ(type->unit->uri, util::pathToUri(sdk.buttonFile()));
    CHECK(type->doc.find("A button") != std::string::npos);
    CHECK(locator.findType("spark.components.Button") == type);

    index::Index index;
    index.add(compiledLibrary());
    analysis::Document document{"file:///src/Main.as", "/src/Main.as",
                                "package {\n"
                                "    import spark.components.Button;\n"
                                "    public class Main {\n"
                                "        public function run(button:Button):void {\n"
                                "            button.setFocus();\n"
                                "        }\n"
                                "    }\n"
                                "}\n",
                                1};
    document.refresh();
    index.add(document.unit());
    const analysis::Analyzer analyzer{index, document, &locator};

    auto locations = analyzer.definition({4, 22});
    CHECK_EQ(locations.size(), std::size_t{1});
    CHECK_EQ(locations[0].uri, util::pathToUri(sdk.buttonFile()));
    CHECK_EQ(locations[0].range.start.line, 7u);

    locations = analyzer.definition({3, 39});
    CHECK_EQ(locations.size(), std::size_t{1});
    CHECK_EQ(locations[0].range.start.line, 2u);

    const auto hover = analyzer.hover({4, 22});
    CHECK(hover.has_value());
    CHECK(hover->markdown.find("Moves the focus") != std::string::npos);

    const analysis::Analyzer withoutSources{index, document};
    CHECK(withoutSources.definition({4, 22}).empty());
}
