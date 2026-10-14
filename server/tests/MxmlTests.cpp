#include "Test.hpp"
#include "analysis/Analyzer.hpp"
#include "syntax/Mxml.hpp"
#include "syntax/Parser.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <memory>

using namespace as3lsp;

namespace {

constexpr std::string_view sparkManifest = R"(<componentPackage>
    <component id="Group" class="spark.components.Group"/>
    <component id="Button" class="spark.components.Button"/>
</componentPackage>)";

constexpr std::string_view sparkSource = R"(
package spark.components {
    public class Group {
        public function addElement(element:Object):Object { return null; }
    }
}
package spark.components {
    public class Button {
        public var label:String;
        public function setFocus():void {}
    }
}
package flash.events {
    public class Event {
        public function get type():String { return null; }
    }
}
)";

// The cursor marker is removed from the text before parsing.
constexpr std::string_view cursorMarker = "@@";

constexpr std::string_view viewSource = R"mxml(<?xml version="1.0" encoding="utf-8"?>
<s:Group xmlns:fx="http://ns.adobe.com/mxml/2009" xmlns:s="library://ns.adobe.com/flex/spark"
         creationComplete="iniciar(event)">
    <fx:Script>
        <![CDATA[
            import flash.events.Event;

            private var total:int = 0;

            private function iniciar(evento:Event):void {
                btnSalvar.@@
            }
        ]]>
    </fx:Script>
    <s:Button id="btnSalvar" label="Salvar ção" click="btnSalvar.setFocus(); total = 1"/>
    <s:Button label="{btnSalvar.label}"/>
</s:Group>
)mxml";

std::shared_ptr<syntax::MxmlContext> context() {
    auto mxml = std::make_shared<syntax::MxmlContext>();
    mxml->addManifest("library://ns.adobe.com/flex/spark", sparkManifest);
    mxml->addSourceRoot("/work/app/src");
    return mxml;
}

class Fixture {
public:
    Fixture() {
        auto unit = syntax::parse(sparkSource, {.collectScopes = false});
        unit->key = "/lib/spark.as";
        unit->uri = "file:///lib/spark.as";
        index_.add(std::move(unit));
    }

    analysis::Analyzer open(std::string_view textWithCursor, std::string_view cursor = cursorMarker) {
        std::string text{textWithCursor};
        const auto marker = text.find(cursor);
        if (marker != std::string::npos) {
            text.erase(marker, cursor.size());
            const auto before = std::string_view{text}.substr(0, marker);
            const auto line = static_cast<std::uint32_t>(std::ranges::count(before, '\n'));
            const auto lineStart = before.rfind('\n');
            const auto lineText = lineStart == std::string_view::npos ? before : before.substr(lineStart + 1);
            cursor_ = {line, static_cast<std::uint32_t>(util::utf16Length(lineText))};
        }
        document_ =
            std::make_unique<analysis::Document>("file:///work/app/src/views/Cadastro.mxml",
                                                 "/work/app/src/views/Cadastro.mxml", text, 1, context());
        document_->refresh();
        index_.add(document_->unit());
        return analysis::Analyzer{index_, *document_};
    }

    [[nodiscard]] model::Position cursor() const { return cursor_; }
    [[nodiscard]] const model::Unit& unit() const { return *document_->unit(); }

private:
    index::Index index_;
    std::unique_ptr<analysis::Document> document_;
    model::Position cursor_;
};

bool hasLabel(const analysis::CompletionList& list, std::string_view label) {
    return std::ranges::any_of(list.items, [&](const auto& item) { return item.label == label; });
}

} // namespace

TEST_CASE(mxmlTranslationKeepsPositions) {
    std::string text{viewSource};
    text.erase(text.find(cursorMarker), cursorMarker.size());
    const auto translation = syntax::translateMxml(text, "/work/app/src/views/Cadastro.mxml", *context());
    CHECK_EQ(std::ranges::count(translation.source, '\n'), std::ranges::count(text, '\n'));
    CHECK(translation.source.find("private var total:int = 0;") != std::string::npos);
    CHECK(translation.source.find("btnSalvar.setFocus(); total = 1;") != std::string::npos);
    CHECK(translation.source.find("{btnSalvar.label}") != std::string::npos);
    CHECK(translation.source.find("Salvar ção") == std::string::npos);
    CHECK(translation.source.find("<s:Button") == std::string::npos);
    CHECK_EQ(translation.type.qualifiedName(), "views.Cadastro");
    CHECK_EQ(translation.type.baseClass, "spark.components.Group");
    CHECK_EQ(translation.type.members.size(), std::size_t{1});
    CHECK_EQ(translation.type.members[0].name, "btnSalvar");
    CHECK_EQ(translation.type.members[0].type, "spark.components.Button");
    CHECK_EQ(translation.eventRanges.size(), std::size_t{2});
}

TEST_CASE(mxmlParsesScriptMembers) {
    Fixture fixture;
    (void)fixture.open(viewSource);
    const auto& type = fixture.unit().types.front();
    CHECK(fixture.unit().isMarkup);
    CHECK_EQ(fixture.unit().imports.size(), std::size_t{1});
    std::vector<std::string> names;
    for (const auto& member : type.members) {
        names.push_back(member.name);
    }
    CHECK((names == std::vector<std::string>{"btnSalvar", "total", "iniciar"}));
}

TEST_CASE(mxmlCompletesComponentMembersInScripts) {
    Fixture fixture;
    const auto analyzer = fixture.open(viewSource);
    const auto list = analyzer.completion(fixture.cursor());
    CHECK(hasLabel(list, "setFocus"));
    CHECK(hasLabel(list, "label"));
}

TEST_CASE(mxmlCompletesInEventAttributes) {
    Fixture fixture;
    std::string text{viewSource};
    text.replace(text.find("btnSalvar.@@"), 12, "btnSalvar.label;");
    text.replace(text.find("click=\"btnSalvar."), 17, "click=\"event.@@");
    const auto analyzer = fixture.open(text);
    CHECK(hasLabel(analyzer.completion(fixture.cursor()), "type"));
}

TEST_CASE(mxmlHasNoCompletionInMarkup) {
    Fixture fixture;
    std::string text{viewSource};
    text.replace(text.find("btnSalvar.@@"), 12, "btnSalvar.label;");
    text.replace(text.find("label=\"Salvar"), 13, "label=\"Sal@@var");
    const auto analyzer = fixture.open(text);
    CHECK(analyzer.completion(fixture.cursor()).items.empty());
}

TEST_CASE(mxmlFindsDefinitions) {
    Fixture fixture;
    std::string text{viewSource};
    text.replace(text.find("btnSalvar.@@"), 12, "btnSalvar.label;");

    // A component id used in a script goes to the id attribute.
    text.replace(text.find("btnSalvar.label;"), 16, "btnSal@@var.label;");
    auto locations = fixture.open(text).definition(fixture.cursor());
    CHECK_EQ(locations.size(), std::size_t{1});
    CHECK_EQ(locations[0].range.start.line, 14u);

    // A function called by an event attribute goes to the function.
    text = viewSource;
    text.replace(text.find("btnSalvar.@@"), 12, "btnSalvar.label;");
    text.replace(text.find("iniciar(event)"), 7, "inic@@iar");
    locations = fixture.open(text).definition(fixture.cursor());
    CHECK_EQ(locations.size(), std::size_t{1});
    CHECK_EQ(locations[0].range.start.line, 9u);

    // A tag goes to its class.
    text = viewSource;
    text.replace(text.find("btnSalvar.@@"), 12, "btnSalvar.label;");
    text.replace(text.find("<s:Button id"), 12, "<s:But@@ton id");
    locations = fixture.open(text).definition(fixture.cursor());
    CHECK_EQ(locations.size(), std::size_t{1});
    CHECK_EQ(locations[0].uri, "file:///lib/spark.as");
}

TEST_CASE(mxmlPackageComesFromSourceRoot) {
    syntax::MxmlContext mxml;
    mxml.addSourceRoot("/work/app/src");
    CHECK_EQ(mxml.packageOf("/work/app/src/br/com/View.mxml"), "br.com");
    CHECK_EQ(mxml.packageOf("/work/app/src/Main.mxml"), "");
    syntax::MxmlContext withoutRoots;
    CHECK_EQ(withoutRoots.packageOf("/other/project/src/a/b/View.mxml"), "a.b");
    CHECK_EQ(mxml.classOf("br.com.custom.*", "Panel", "").value_or(""), "br.com.custom.Panel");
    CHECK_EQ(mxml.classOf("*", "Panel", "views").value_or(""), "views.Panel");
}
