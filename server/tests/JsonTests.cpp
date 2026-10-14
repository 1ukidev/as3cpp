#include "Test.hpp"

#include "json/Json.hpp"

using namespace as3lsp;

TEST_CASE(jsonParsesNestedValues) {
    const auto value = json::parse(R"({"a": [1, 2.5, true, null], "b": {"c": "text"}})");
    CHECK(value.has_value());
    CHECK_EQ((*value)["a"].array().size(), std::size_t{4});
    CHECK_EQ((*value)["a"].array()[0].asInt(), 1);
    CHECK_EQ((*value)["a"].array()[1].asDouble(), 2.5);
    CHECK((*value)["a"].array()[2].asBool());
    CHECK((*value)["a"].array()[3].isNull());
    CHECK_EQ((*value)["b"]["c"].asString(), "text");
    CHECK((*value)["missing"]["deep"].isNull());
}

TEST_CASE(jsonDecodesEscapes) {
    const auto value = json::parse(R"("line\nquote\" é 😀")");
    CHECK(value.has_value());
    CHECK_EQ(value->asString(), "line\nquote\" \xC3\xA9 \xF0\x9F\x98\x80");
}

TEST_CASE(jsonRoundTrips) {
    json::Value value = json::Object{{"id", 3}, {"text", "a\"b\\c\n"}, {"list", json::Array{1, "x", false}}};
    const std::string text = json::serialize(value);
    CHECK_EQ(text, R"({"id":3,"text":"a\"b\\c\n","list":[1,"x",false]})");
    CHECK(json::parse(text) == value);
}

TEST_CASE(jsonRejectsMalformedInput) {
    CHECK(!json::parse("{\"a\":}").has_value());
    CHECK(!json::parse("[1, 2,]").has_value());
    CHECK(!json::parse("{} trailing").has_value());
}

TEST_CASE(jsonLenientModeAcceptsComments) {
    const auto value =
        json::parse("{\n// comment\n\"a\": [1, 2,], /* block */ \"b\": 1,\n}", {.lenient = true});
    CHECK(value.has_value());
    CHECK_EQ((*value)["a"].array().size(), std::size_t{2});
}
