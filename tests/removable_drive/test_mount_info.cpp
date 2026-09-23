#include <catch2/catch_test_macros.hpp>

#include <Slic3r/Biz/RemovableDrive/MountInfoLinux.hpp>

using namespace Slic3r::Biz::RemovableDrive;

TEST_CASE("A mountinfo line yields its mount point and source", "[MountInfo]")
{
    const auto entry = parse_mount_info_line(
        "36 35 98:0 / /run/media/user/USB rw,noatime shared:1 - vfat /dev/sdb1 rw,errors=continue"
    );

    REQUIRE(entry);
    CHECK(entry->mount_point == "/run/media/user/USB");
    CHECK(entry->source == "/dev/sdb1");
}

TEST_CASE("The number of optional fields does not matter", "[MountInfo]")
{
    // The fields between the mount point and the separator are what the
    // separator exists for: there can be none of them, or several.
    const auto none = parse_mount_info_line("36 35 98:0 / /mnt rw,noatime - vfat /dev/sdb1 rw");
    REQUIRE(none);
    CHECK(none->source == "/dev/sdb1");

    const auto several = parse_mount_info_line(
        "36 35 98:0 / /mnt rw,noatime shared:1 master:2 propagate_from:3 - vfat /dev/sdb1 rw"
    );
    REQUIRE(several);
    CHECK(several->mount_point == "/mnt");
    CHECK(several->source == "/dev/sdb1");
}

TEST_CASE("Escaped characters in a mount point are decoded", "[MountInfo]")
{
    // Whitespace and backslashes are octal escaped so that no field can
    // contain whitespace, which is what makes the line splittable at all.
    const auto spaces = parse_mount_info_line(
        "36 35 98:0 / /run/media/user/My\\040USB\\040Stick rw - vfat /dev/sdb1 rw"
    );
    REQUIRE(spaces);
    CHECK(spaces->mount_point == "/run/media/user/My USB Stick");

    const auto backslash = parse_mount_info_line(
        "36 35 98:0 / /run/media/user/back\\134slash rw - vfat /dev/sdb1 rw"
    );
    REQUIRE(backslash);
    CHECK(backslash->mount_point == "/run/media/user/back\\slash");

    const auto tab = parse_mount_info_line("36 35 98:0 / /mnt/a\\011b rw - vfat /dev/sdb1 rw");
    REQUIRE(tab);
    CHECK(tab->mount_point == "/mnt/a\tb");
}

TEST_CASE("Something that only looks like an escape is left alone", "[MountInfo]")
{
    CHECK(unescape_mount_info_field("/mnt/a\\09") == "/mnt/a\\09");   // Too short.
    CHECK(unescape_mount_info_field("/mnt/a\\09z") == "/mnt/a\\09z"); // Not octal.
    CHECK(unescape_mount_info_field("/mnt/trailing\\") == "/mnt/trailing\\");
    CHECK(unescape_mount_info_field("/mnt/plain") == "/mnt/plain");
    CHECK(unescape_mount_info_field("").empty());
}

TEST_CASE("A line without a mount point and source yields nothing", "[MountInfo]")
{
    CHECK_FALSE(parse_mount_info_line(""));
    CHECK_FALSE(parse_mount_info_line("36 35 98:0 / /mnt"));               // Ends early.
    CHECK_FALSE(parse_mount_info_line("36 35 98:0 / /mnt rw,noatime"));    // No separator.
    CHECK_FALSE(parse_mount_info_line("36 35 98:0 / /mnt rw - vfat"));     // No source.
    CHECK_FALSE(parse_mount_info_line("this is not a mountinfo line"));
}

TEST_CASE("A device mapper node is reported as it is written", "[MountInfo]")
{
    // An encrypted drive is unlocked as a device mapper node, and arrives
    // through a symlink whose name is not the one UDisks2 knows it by.
    const auto entry = parse_mount_info_line(
        "36 35 254:0 / /run/media/user/Encrypted rw - ext4 /dev/mapper/luks-1234 rw"
    );
    REQUIRE(entry);
    CHECK(entry->source == "/dev/mapper/luks-1234");
}
