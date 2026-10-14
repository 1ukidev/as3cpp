#include "Test.hpp"
#include "syntax/Lexer.hpp"
#include "syntax/Mxml.hpp"
#include "syntax/Parser.hpp"

#include <string>

using namespace as3lsp;

namespace {

std::vector<model::Diagnostic> diagnosticsOf(std::string_view source) {
    return syntax::parse(source, {.collectScopes = true, .collectDiagnostics = true})->diagnostics;
}

std::vector<model::Diagnostic> mxmlDiagnosticsOf(std::string_view source) {
    const syntax::MxmlContext context;
    return syntax::parseMxml(source, "/src/View.mxml", context, {.collectDiagnostics = true})->diagnostics;
}

// "line:column message" of the only diagnostic, with 1-based positions, or a list when there are several.
std::string describe(const std::vector<model::Diagnostic>& diagnostics) {
    std::string text;
    for (const auto& diagnostic : diagnostics) {
        if (!text.empty()) {
            text += " | ";
        }
        text += std::to_string(diagnostic.range.start.line + 1) + ":" +
                std::to_string(diagnostic.range.start.character + 1) + " " + diagnostic.message;
    }
    return text;
}

std::string wrap(std::string_view members) {
    return "package app {\n    public class Main {\n" + std::string{members} + "\n    }\n}\n";
}

} // namespace

TEST_CASE(diagnosticsAreEmptyForValidCode) {
    CHECK(diagnosticsOf(wrap(R"(        private var items:Vector.<Vector.<int>> = new <Vector.<int>>[];
        public function run(value:*=null, ...rest):void {
            var re:RegExp = /[{(]/g;
            var xml:XML = <a b="{value}"><c/></a>;
            public::run(null);
            CONFIG::debug {
                trace("debug");
            }
        }
        CONFIG::release
        public function release():void {}
        CONFIG::debug {
            public function debug():void {}
        })"))
              .empty());
    CHECK(diagnosticsOf("package { public interface IA extends IB, IC { function f():void; } }").empty());
}

TEST_CASE(diagnosticsFindUnterminatedStringsAndComments) {
    CHECK_EQ(describe(diagnosticsOf(wrap("        private var s:String = \"texto;"))),
             "3:32 Unterminated string: the closing quote is missing.");
    CHECK_EQ(describe(diagnosticsOf("package {\n/* aberto\n}")),
             "1:9 '{' is not closed: '}' is missing. | 2:1 Unterminated comment: '*/' is missing.");
}

TEST_CASE(diagnosticsFindTheFunctionWithoutClosingBrace) {
    CHECK_EQ(describe(diagnosticsOf(wrap(R"(        public function first():void {
            if (ready) {
                trace("ok");
            }

        public function second():void {})"))),
             "3:38 '{' is not closed: '}' is missing.");
}

TEST_CASE(diagnosticsFindUnbalancedBrackets) {
    CHECK_EQ(describe(diagnosticsOf(wrap("        public function f():void { trace(a, b; }"))),
             "3:41 '(' is not closed: ')' is missing.");
    CHECK_EQ(describe(diagnosticsOf(wrap("        public function f():void { g(a]; }"))),
             "3:37 '(' is not closed: ')' is missing. | 3:39 Unexpected ']': there is no matching '['.");
    CHECK_EQ(describe(diagnosticsOf("package {\n}\n}\n")), "3:1 Unexpected '}': there is no matching '{'.");
}

TEST_CASE(diagnosticsFindIncompleteDeclarations) {
    CHECK_EQ(describe(diagnosticsOf("package { public class extends Base {} }")),
             "1:24 Expected a class name.");
    CHECK_EQ(describe(diagnosticsOf("package { public class A extends {} }")),
             "1:34 Expected a class after 'extends'.");
    CHECK_EQ(describe(diagnosticsOf("package { public class A implements {} }")),
             "1:37 Expected an interface after 'implements'.");
    CHECK_EQ(describe(diagnosticsOf(wrap("        public function (a:int):void {}"))),
             "3:25 Expected a function name.");
    CHECK_EQ(describe(diagnosticsOf(wrap("        private var x: = 1;"))), "3:24 Expected a type after ':'.");
    CHECK_EQ(describe(diagnosticsOf(wrap("        public function f(a:int b:int):void {}"))),
             "3:33 Expected ',' or ')' in the parameter list.");
    CHECK_EQ(describe(diagnosticsOf("package { import flash.display.; }")),
             "1:32 Expected a name or '*' after '.'.");
    CHECK_EQ(describe(diagnosticsOf("package { public class A\n}")),
             "1:25 Expected '{' to start the body of the class.");
}

TEST_CASE(diagnosticsAreNotCollectedByDefault) {
    CHECK(syntax::parse("package { class }")->diagnostics.empty());
}

TEST_CASE(mxmlDiagnosticsFindStructureErrors) {
    CHECK(mxmlDiagnosticsOf(R"(<?xml version="1.0"?>
<s:Group xmlns:s="library://ns.adobe.com/flex/spark" xmlns:fx="http://ns.adobe.com/mxml/2009">
    <!-- <s:Button> inside a comment -->
    <fx:Script><![CDATA[ private var a:int = 1 < 2 ? 1 : 0; ]]></fx:Script>
    <s:Button label="OK" click="a = 2"/>
</s:Group>)")
              .empty());
    CHECK_EQ(describe(mxmlDiagnosticsOf("<s:Group>\n    <s:VGroup>\n</s:Group>")),
             "2:6 Element '<s:VGroup>' is not closed: '</s:VGroup>' is missing.");
    CHECK_EQ(describe(mxmlDiagnosticsOf("<s:Group>\n</s:VGroup>\n</s:Group>")),
             "2:1 Unexpected end tag '</s:VGroup>': no element '<s:VGroup>' is open.");
    CHECK_EQ(describe(mxmlDiagnosticsOf("<s:Group width=100/>")),
             "1:10 The value of attribute 'width' must be in quotes.");
    CHECK_EQ(describe(mxmlDiagnosticsOf("<s:Group>\n<fx:Script><![CDATA[ var a:int;\n</s:Group>")),
             "2:2 Element '<fx:Script>' is not closed: '</fx:Script>' is missing. | "
             "2:12 Unterminated CDATA section: ']]>' is missing.");
}

TEST_CASE(mxmlDiagnosticsIncludeScriptErrors) {
    CHECK_EQ(describe(mxmlDiagnosticsOf(R"(<s:Group xmlns:fx="http://ns.adobe.com/mxml/2009">
    <fx:Script>
        <![CDATA[
            private function a():void {
                trace("x";
            }
        ]]>
    </fx:Script>
</s:Group>)")),
             "5:22 '(' is not closed: ')' is missing.");
}
