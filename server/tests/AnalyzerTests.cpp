#include "Test.hpp"
#include "analysis/Analyzer.hpp"
#include "syntax/Parser.hpp"

#include <algorithm>
#include <memory>

using namespace as3lsp;
using analysis::CompletionKind;

namespace {

constexpr std::string_view cursorMarker = "@@";

constexpr std::string_view builtins = R"(
package {
    public class Object {
        public function hasOwnProperty(name:String):Boolean { return false; }
        public function toString():String { return ""; }
    }
}
package {
    public final class String {
        public function get length():int { return 0; }
        public function charAt(index:Number = 0):String { return ""; }
    }
}
package {
    public final class Number {}
}
package {
    public function trace(...args):void {}
}
package __AS3__.vec {
    public final dynamic class Vector$object {
        public function pop():* { return null; }
        public function get length():uint { return 0; }
    }
}
)";

constexpr std::string_view pointSource = R"(package geom {
    /** A point in 2D space. */
    public class Point {
        public var x:Number;
        public var y:Number;
        public static const ORIGIN:Point = new Point();

        public function Point(x:Number = 0, y:Number = 0) {}

        /** Returns the distance to another point. */
        public function distanceTo(other:Point):Number { return 0; }
        public function clone():Point { return null; }
        protected function secret():void {}
        private function hidden():void {}
    }
}
)";

constexpr std::string_view shapeSource = R"(package geom {
    public class Shape {
        public var points:Vector.<Point>;
        public function get first():Point { return null; }
    }
}
)";

class Fixture {
public:
    Fixture() {
        addSource("/lib/builtins.as", builtins);
        addSource("/src/geom/Point.as", pointSource);
        addSource("/src/geom/Shape.as", shapeSource);
        addSource("/src/ui/Button.as", "package ui { public class Button {} }");
    }

    void addSource(const std::string& key, std::string_view text) {
        auto unit = syntax::parse(text, {.collectScopes = false});
        unit->key = key;
        unit->uri = "file://" + key;
        index_.add(std::move(unit));
    }

    // A compiled library, which has no source location.
    void addLibrary(const std::string& key, std::string_view text) {
        auto unit = syntax::parse(text, {.collectScopes = false});
        unit->key = key;
        unit->origin = model::Origin::Library;
        index_.add(std::move(unit));
    }

    analysis::Analyzer open(std::string_view textWithCursor) {
        std::string text{textWithCursor};
        const auto marker = text.find(cursorMarker);
        CHECK(marker != std::string::npos);
        text.erase(marker, cursorMarker.size());
        document_ = std::make_unique<analysis::Document>("file:///src/Main.as", "/src/Main.as", text, 1);
        document_->refresh();
        index_.add(document_->unit());
        cursor_ = document_->positionAt(static_cast<std::uint32_t>(marker));
        return analysis::Analyzer{index_, *document_};
    }

    [[nodiscard]] model::Position cursor() const { return cursor_; }

private:
    index::Index index_;
    std::unique_ptr<analysis::Document> document_;
    model::Position cursor_;
};

std::vector<std::string> labels(const analysis::CompletionList& list) {
    std::vector<std::string> result;
    for (const auto& item : list.items) {
        result.push_back(item.label);
    }
    std::ranges::sort(result);
    return result;
}

bool contains(const std::vector<std::string>& values, std::string_view value) {
    return std::ranges::find(values, value) != values.end();
}

const analysis::CompletionItem* findItem(const analysis::CompletionList& list, std::string_view label) {
    for (const auto& item : list.items) {
        if (item.label == label) {
            return &item;
        }
    }
    return nullptr;
}

std::string wrap(std::string_view body) {
    return "package app {\n    import geom.Point;\n    import geom.Shape;\n\n    public class Main {\n"
           "        private var count:int;\n        public function run(shape:Shape):void {\n" +
           std::string{body} + "\n        }\n    }\n}\n";
}

} // namespace

TEST_CASE(completionListsInstanceMembers) {
    Fixture fixture;
    const auto analyzer = fixture.open(wrap("var p:Point = new Point();\np.@@"));
    const auto items = labels(analyzer.completion(fixture.cursor()));
    CHECK(contains(items, "x"));
    CHECK(contains(items, "distanceTo"));
    CHECK(contains(items, "hasOwnProperty"));
    CHECK(!contains(items, "secret"));
    CHECK(!contains(items, "hidden"));
    CHECK(!contains(items, "ORIGIN"));
}

TEST_CASE(completionListsStaticMembers) {
    Fixture fixture;
    const auto analyzer = fixture.open(wrap("Point.@@"));
    const auto items = labels(analyzer.completion(fixture.cursor()));
    CHECK(items == std::vector<std::string>{"ORIGIN"});
}

TEST_CASE(completionFollowsExpressionChains) {
    Fixture fixture;
    for (std::string_view expression :
         {"shape.first.@@", "shape.points[0].@@", "shape.points.pop().@@", "Point.ORIGIN.clone().@@",
          "(shape.first as Point).@@", "new Point(1, 2).@@"}) {
        const auto analyzer = fixture.open(wrap(expression));
        const auto items = labels(analyzer.completion(fixture.cursor()));
        if (!contains(items, "distanceTo")) {
            ::as3lsp::test::fail(__FILE__, __LINE__, "no Point members for " + std::string{expression});
        }
    }
}

TEST_CASE(completionListsLocalsAndClassMembers) {
    Fixture fixture;
    const auto analyzer = fixture.open(wrap("var total:Number = 0;\nfunction helper():void {}\n@@"));
    const auto list = analyzer.completion(fixture.cursor());
    const auto items = labels(list);
    CHECK(contains(items, "total"));
    CHECK(contains(items, "shape"));
    CHECK(contains(items, "helper"));
    CHECK(contains(items, "count"));
    CHECK(contains(items, "run"));
    CHECK(contains(items, "Point"));
    CHECK(contains(items, "trace"));
    CHECK(contains(items, "return"));
    CHECK(!contains(items, "Button"));
    CHECK(list.isIncomplete);
}

TEST_CASE(completionAddsImportForUnimportedTypes) {
    Fixture fixture;
    const auto analyzer =
        fixture.open("package app {\n    public class Other {\n        var a:Poi@@\n    }\n}\n");
    const auto list = analyzer.completion(fixture.cursor());
    const auto* item = findItem(list, "Point");
    CHECK(item != nullptr);
    CHECK(item->importEdit.has_value());
    CHECK_EQ(item->importEdit->newText, "    import geom.Point;\n\n");
    CHECK_EQ(item->importEdit->range.start.line, 1u);
    CHECK(findItem(list, "count") == nullptr);
}

TEST_CASE(completionIgnoresCommentsAndStrings) {
    Fixture fixture;
    CHECK(fixture.open(wrap("// p.@@")).completion(fixture.cursor()).items.empty());
    CHECK(fixture.open(wrap("var s:String = \"p.@@\";")).completion(fixture.cursor()).items.empty());
}

TEST_CASE(completionTriggeredByColonOnlyInTypeAnnotations) {
    Fixture fixture;
    CHECK(fixture.open(wrap("var o:Object = {a:@@")).completion(fixture.cursor(), ":").items.empty());
    CHECK(!fixture.open(wrap("var o:@@")).completion(fixture.cursor(), ":").items.empty());
}

TEST_CASE(definitionFindsMembersAndLocals) {
    Fixture fixture;
    const auto definitionAt = [&](std::string_view body) {
        return fixture.open(wrap(body)).definition(fixture.cursor());
    };

    auto locations = definitionAt("var p:Point = new Point();\np.dist@@anceTo(p);");
    CHECK_EQ(locations.size(), std::size_t{1});
    CHECK_EQ(locations[0].uri, "file:///src/geom/Point.as");
    CHECK_EQ(locations[0].range.start.line, 10u);

    locations = definitionAt("var p:Point = new Point();\np@@.x = 1;");
    CHECK_EQ(locations.size(), std::size_t{1});
    CHECK_EQ(locations[0].uri, "file:///src/Main.as");
    CHECK_EQ(locations[0].range.start.line, 7u);

    locations = definitionAt("var p:Po@@int;");
    CHECK_EQ(locations.size(), std::size_t{1});
    CHECK_EQ(locations[0].range.start.line, 2u);
}

TEST_CASE(hoverShowsSignatureAndDocs) {
    Fixture fixture;
    const auto analyzer = fixture.open(wrap("var p:Point;\np.distance@@To(null);"));
    const auto hover = analyzer.hover(fixture.cursor());
    CHECK(hover.has_value());
    CHECK(hover->markdown.find("public function distanceTo(other:Point):Number") != std::string::npos);
    CHECK(hover->markdown.find("Returns the distance") != std::string::npos);
}

TEST_CASE(signatureHelpTracksActiveParameter) {
    Fixture fixture;
    auto help = fixture.open(wrap("var p:Point = new Point(1, @@")).signatureHelp(fixture.cursor());
    CHECK(help.has_value());
    CHECK_EQ(help->signature.label, "Point(x:Number = 0, y:Number = 0)");
    CHECK_EQ(help->activeParameter, 1u);
    CHECK(help->signature.parameters[1] == std::make_pair(std::uint32_t{20}, std::uint32_t{32}));

    help = fixture.open(wrap("shape.first.distanceTo(@@")).signatureHelp(fixture.cursor());
    CHECK(help.has_value());
    CHECK_EQ(help->signature.label, "distanceTo(other:Point):Number");
    CHECK_EQ(help->activeParameter, 0u);
}

TEST_CASE(completionHidesGeneratedLibraryNames) {
    Fixture fixture;
    fixture.addLibrary("library:generated.swc", R"(package {
    public class _Main_WatcherSetupUtil {}
    public class Library {
        public static function _init():void {}
        public function _internalHelper():void {}
        public function load():void {}
    }
})");
    auto items = labels(fixture.open(wrap("var library:Library;\nlibrary.@@")).completion(fixture.cursor()));
    CHECK(contains(items, "load"));
    CHECK(!contains(items, "_internalHelper"));

    const std::string source = "package app {\n    public class Other {\n        private var _total:int;\n"
                               "        public function run():void {\n            _@@\n        }\n    }\n}\n";
    items = labels(fixture.open(source).completion(fixture.cursor()));
    CHECK(contains(items, "_total"));
    CHECK(!contains(items, "_Main_WatcherSetupUtil"));
}
