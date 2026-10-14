#include "Test.hpp"
#include "syntax/Parser.hpp"

using namespace as3lsp;
using model::MemberKind;

namespace {

const model::Member* findMember(const model::TypeDecl& type, std::string_view name) {
    for (const auto& member : type.members) {
        if (member.name == name) {
            return &member;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE(parserReadsClassDeclarations) {
    const auto unit = syntax::parse(R"(package com.example.game {
    import flash.display.Sprite;
    import flash.events.*;

    /** A player. */
    [Event(name="jump", type="flash.events.Event")]
    public final class Player extends Sprite implements IActor, IJumper {
        public static const SPEED:Number = 2.5;
        private var _name:String, _lives:int = 3;
        protected var items:Vector.<Vector.<Item>>;

        public function Player(name:String, ...rest) { _name = name; }
        public function get name():String { return _name; }
        public function set name(value:String):void { _name = value; }
        override protected function update(delta:Number = 1 / 60):void {}
        mx_internal function hidden():void {}
        CONFIG::debug {
            public function debugDraw():void {}
        }
    }
}
)",
                                    {.collectScopes = false});
    CHECK_EQ(unit->package, "com.example.game");
    CHECK_EQ(unit->imports.size(), std::size_t{2});
    CHECK(unit->imports[1].isWildcard);
    CHECK_EQ(unit->imports[1].name, "flash.events");
    CHECK_EQ(unit->types.size(), std::size_t{1});

    const auto& type = unit->types.front();
    CHECK_EQ(type.qualifiedName(), "com.example.game.Player");
    CHECK_EQ(type.baseClass, "Sprite");
    CHECK_EQ(type.interfaces.size(), std::size_t{2});
    CHECK(type.isFinal);
    CHECK(type.doc.find("A player.") != std::string::npos);
    CHECK_EQ(type.unit, unit.get());

    const auto* speed = findMember(type, "SPEED");
    CHECK(speed && speed->kind == MemberKind::Constant && speed->isStatic && speed->type == "Number");
    const auto* lives = findMember(type, "_lives");
    CHECK(lives && lives->visibility == model::Visibility::Private && lives->type == "int");
    CHECK_EQ(findMember(type, "items")->type, "Vector.<Vector.<Item>>");

    const auto* constructor = findMember(type, "Player");
    CHECK(constructor && constructor->kind == MemberKind::Constructor);
    CHECK_EQ(constructor->params.size(), std::size_t{2});
    CHECK(constructor->params[1].isRest);

    CHECK_EQ(findMember(type, "name")->kind, MemberKind::Getter);
    const auto* update = findMember(type, "update");
    CHECK(update && update->isOverride && update->params[0].defaultValue == "1 / 60");
    CHECK_EQ(findMember(type, "hidden")->ns, "mx_internal");
    CHECK(findMember(type, "debugDraw") != nullptr);
}

TEST_CASE(parserReadsInterfacesAndGlobals) {
    const auto unit = syntax::parse(R"(package util {
    public interface IShape extends IDrawable, IBounded {
        function area():Number;
        function get name():String;
    }
}
package util {
    public function clamp(value:Number, min:Number, max:Number):Number { return value; }
}
class Helper {}
)");
    CHECK_EQ(unit->types.size(), std::size_t{2});
    const auto& shape = unit->types[0];
    CHECK(shape.isInterface());
    CHECK_EQ(shape.interfaces.size(), std::size_t{2});
    CHECK_EQ(shape.members[0].visibility, model::Visibility::Public);
    CHECK_EQ(unit->globals.size(), std::size_t{1});
    CHECK_EQ(unit->globals[0].qualifiedName(), "util.clamp");
    CHECK(unit->types[1].isFilePrivate);
}

TEST_CASE(parserCollectsLocalScopes) {
    const auto unit = syntax::parse(R"(package {
    public class Main {
        public function run(count:int):void {
            var total:Number = 0, label:String;
            for (var i:int = 0; i < count; i++) {
                total += i;
            }
            try { } catch (error:Error) { }
            list.forEach(function(item:Object, index:int, array:Array):void {
                var inner:Boolean;
            });
            function helper(x:int):int { return x; }
        }
    }
}
)");
    CHECK_EQ(unit->scopes.size(), std::size_t{3});
    const auto& run = unit->scopes[0];
    std::vector<std::string> names;
    for (const auto& local : run.locals) {
        names.push_back(local.name);
    }
    const std::vector<std::string> expected = {"count", "total", "label", "i", "error", "helper"};
    CHECK(names == expected);
    CHECK_EQ(run.locals[1].type, "Number");
    CHECK_EQ(run.locals[5].kind, model::LocalKind::Function);
    CHECK_EQ(unit->scopes[1].parent, 0);
    CHECK_EQ(unit->scopes[1].locals.size(), std::size_t{4});
}

TEST_CASE(parserRecoversFromUnclosedFunctionBody) {
    const auto unit = syntax::parse(R"(package {
    public class Broken {
        public function first():void {
            if (x) {
                trace(

        public function second():void {}
        private var third:int;
    }
}
)");
    CHECK_EQ(unit->types.size(), std::size_t{1});
    const auto& members = unit->types[0].members;
    CHECK_EQ(members.size(), std::size_t{3});
    CHECK_EQ(members[1].name, "second");
    CHECK_EQ(members[2].name, "third");
}

TEST_CASE(parserFormatsDocComments) {
    const std::string text = syntax::formatDocComment(R"(/**
     * Adds a <code>child</code>.
     *
     *  @param child The child to add.
     * @return The child.
     *  @langversion 3.0
     */)");
    CHECK(text.starts_with("Adds a `child`."));
    CHECK(text.find("*@param* `child` — The child to add.") != std::string::npos);
    CHECK(text.find("*@return* — The child.") != std::string::npos);
    CHECK(text.find("langversion") == std::string::npos);
}
