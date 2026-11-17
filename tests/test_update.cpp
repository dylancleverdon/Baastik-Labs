#include "tutti/Update.h"

#include <catch2/catch_test_macros.hpp>

using namespace tutti::update;

namespace
{
    std::string manifest (long build)
    {
        const std::string sha (64, 'a');
        return R"({"schema":1,"version":"0.1.)" + std::to_string (build) + R"(","build":)" + std::to_string (build)
             + R"(,"protocol":1,"notes":"Faster joins","platforms":{
                 "mac":{"name":"Tutti-mac.zip","url":"https://github.com/dylancleverdon/Baastik-Labs/releases/download/tutti-v0.1.)"
             + std::to_string (build) + R"(/Tutti-mac.zip","sha256":")" + sha + R"(","size":10},
                 "windows":{"name":"x.zip","url":"https://evil.example.com/x.zip","sha256":")" + sha + R"(","size":10}},
               "suggestions":{"endpoint":"https://script.google.com/macros/s/abc/exec"}})";
    }
}

TEST_CASE ("Manifests are parsed and checked", "[update]")
{
    std::string error;
    auto m = parse (manifest (42), error);
    REQUIRE (m);
    CHECK (m->build == 42);
    CHECK (m->notes == "Faster joins");
    CHECK (m->suggestionsEndpoint == "https://script.google.com/macros/s/abc/exec");
    CHECK (m->platforms.count ("mac") == 1);
    CHECK_FALSE (parse ("{}", error));
    CHECK_FALSE (parse ("nope", error));
}

TEST_CASE ("Only this repo's tutti releases are trusted", "[update]")
{
    CHECK (isAllowedUrl ("https://github.com/dylancleverdon/Baastik-Labs/releases/download/tutti-v0.1.3/Tutti-mac.zip"));
    CHECK_FALSE (isAllowedUrl ("https://github.com/dylancleverdon/Baastik-Labs/releases/download/norg-v1/Norg.zip"));
    CHECK_FALSE (isAllowedUrl ("http://github.com/dylancleverdon/Baastik-Labs/releases/download/tutti-v1/x.zip"));
    CHECK_FALSE (isAllowedUrl ("https://github.com/dylancleverdon/Baastik-Labs/releases/download/tutti-v1/../../x"));
    CHECK (isAllowedUrl ("file:///tmp/x.zip", { "file:///tmp/" }));
}

TEST_CASE ("Install decisions respect auto-update, skips and platforms", "[update]")
{
    std::string error;
    auto m = *parse (manifest (42), error);
    Settings s;
    CHECK (decide (m, 41, s, false, "mac") == Decision::install);
    CHECK (decide (m, 42, s, false, "mac") == Decision::upToDate);
    CHECK (decide (m, 41, s, false, "windows") == Decision::unavailable); // URL not allowed
    CHECK (decide (m, 41, s, false, "linux") == Decision::unavailable);
    s.skipBuild = 42;
    CHECK (decide (m, 41, s, false, "mac") == Decision::skipped);
    CHECK (decide (m, 41, s, true, "mac") == Decision::install); // "Check now" ignores the skip
    s = {};
    s.autoUpdate = false;
    CHECK (decide (m, 41, s, false, "mac") == Decision::disabled);
    CHECK (decide (m, 41, s, true, "mac") == Decision::install);
}

TEST_CASE ("Zip entries can't escape the folder", "[update]")
{
    CHECK (isSafeEntry ("Tutti.app/Contents/MacOS/Tutti"));
    CHECK (isSafeEntry ("RemoteScript/Tutti/__init__.py"));
    CHECK_FALSE (isSafeEntry ("../evil"));
    CHECK_FALSE (isSafeEntry ("a/../../evil"));
    CHECK_FALSE (isSafeEntry ("/etc/passwd"));
    CHECK_FALSE (isSafeEntry ("C:\\Windows\\x"));
    CHECK_FALSE (isSafeEntry (""));
}
