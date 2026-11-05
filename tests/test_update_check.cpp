#include "frontend/update_check.h"
#include "test_framework.h"

using namespace chaotix;

TEST(update_check, versions_compare_by_their_numbers) {
    CHECK(update_check::is_newer("v0.15.19", "0.15.18"));
    CHECK(update_check::is_newer("v0.16.0", "0.15.18"));
    CHECK(update_check::is_newer("v1.0", "0.15.18"));
    CHECK(update_check::is_newer("v0.15.10", "0.15.9"));  // numbers, not text
    CHECK(!update_check::is_newer("v0.15.18", "0.15.18"));
    CHECK(!update_check::is_newer("v0.15.18", "0.15.18.0"));
    CHECK(!update_check::is_newer("v0.15.17", "0.15.18"));
    CHECK(!update_check::is_newer("v0.9.99", "0.15.18"));
}

TEST(update_check, anything_that_is_not_a_version_is_never_newer) {
    CHECK(!update_check::is_newer("", "0.15.18"));
    CHECK(!update_check::is_newer("latest", "0.15.18"));
    CHECK(!update_check::is_newer("v9.9.9-beta", "0.15.18"));
    CHECK(!update_check::is_newer("v9..9", "0.15.18"));
    CHECK(!update_check::is_newer("v9.9.", "0.15.18"));
    CHECK(!update_check::is_newer("v9.9.9", "unknown"));  // a build without a version
}

TEST(update_check, the_tag_is_read_from_githubs_answer) {
    const std::string body =
        "{\n  \"url\": \"https://api.github.com/repos/x/y/releases/1\",\n"
        "  \"tag_name\": \"v0.15.19\",\n  \"name\": \"0.15.19\"\n}";
    CHECK(update_check::tag_from_json(body) == std::string("v0.15.19"));
    CHECK(update_check::tag_from_json("{\"tag_name\":\"v1.2.3\"}") == std::string("v1.2.3"));
    CHECK(update_check::tag_from_json("{\"message\":\"Not Found\"}") == std::string());
    CHECK(update_check::tag_from_json("{\"tag_name\":\"<b>hi</b>\"}") == std::string());
    CHECK(update_check::tag_from_json("{\"tag_name\":\"v1.2") == std::string());
    CHECK(update_check::tag_from_json("") == std::string());
}

TEST(update_check, the_menu_and_the_link_name_the_release) {
    CHECK(update_check::display_version("v0.15.19") == std::string("0.15.19"));
    CHECK(update_check::release_page("v0.15.19") ==
          std::string("https://github.com/YuutaTsubasa/ChaotixRecompiled/releases/tag/v0.15.19"));
}
