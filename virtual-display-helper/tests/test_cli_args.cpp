// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "cli_args.h"

using fuvr::vdisplay::CliArgs;
using fuvr::vdisplay::parseCli;
using fuvr::vdisplay::parseModeTriple;
using fuvr::vdisplay::ModeArg;

namespace {
char* lit(const char* s) { return const_cast<char*>(s); }
}

TEST(CliArgs, DefaultsApply) {
    char* argv[] = {lit("helper")};
    CliArgs a = parseCli(1, argv);
    EXPECT_FALSE(a.parseError);
    EXPECT_EQ(a.width, 4128u);
    EXPECT_EQ(a.height, 2208u);
    EXPECT_EQ(a.hz, 90u);
    EXPECT_TRUE(a.modes.empty());
    EXPECT_FALSE(a.listOnly);
    EXPECT_TRUE(a.watchdog);
}

TEST(CliArgs, WidthHeightRefresh) {
    char* argv[] = {lit("helper"),
                    lit("--width"),   lit("1920"),
                    lit("--height"),  lit("1080"),
                    lit("--refresh"), lit("60")};
    CliArgs a = parseCli(7, argv);
    EXPECT_FALSE(a.parseError);
    EXPECT_EQ(a.width, 1920u);
    EXPECT_EQ(a.height, 1080u);
    EXPECT_EQ(a.hz, 60u);
}

TEST(CliArgs, ListFlag) {
    char* argv[] = {lit("helper"), lit("--list")};
    CliArgs a = parseCli(2, argv);
    EXPECT_TRUE(a.listOnly);
}

TEST(CliArgs, MultipleModesParsed) {
    char* argv[] = {lit("helper"),
                    lit("--mode"), lit("3360x2208x90"),
                    lit("--mode"), lit("3360x2208x72")};
    CliArgs a = parseCli(5, argv);
    EXPECT_FALSE(a.parseError);
    ASSERT_EQ(a.modes.size(), 2u);
    EXPECT_EQ(a.modes[0].width, 3360u);
    EXPECT_EQ(a.modes[0].hz, 90u);
    EXPECT_EQ(a.modes[1].hz, 72u);
}

TEST(CliArgs, BadModeTripleSetsError) {
    char* argv[] = {lit("helper"), lit("--mode"), lit("not-a-mode")};
    CliArgs a = parseCli(3, argv);
    EXPECT_TRUE(a.parseError);
}

TEST(CliArgs, UnknownArgRejected) {
    char* argv[] = {lit("helper"), lit("--frobnicate")};
    CliArgs a = parseCli(2, argv);
    EXPECT_TRUE(a.parseError);
}

TEST(CliArgs, NoWatchdogFlag) {
    char* argv[] = {lit("helper"), lit("--no-watchdog")};
    CliArgs a = parseCli(2, argv);
    EXPECT_FALSE(a.watchdog);
}

TEST(ModeTriple, Valid) {
    ModeArg m;
    EXPECT_TRUE(parseModeTriple("1920x1080x60", &m));
    EXPECT_EQ(m.width, 1920u);
    EXPECT_EQ(m.height, 1080u);
    EXPECT_EQ(m.hz, 60u);
}

TEST(ModeTriple, RejectsZero) {
    ModeArg m;
    EXPECT_FALSE(parseModeTriple("0x1080x60", &m));
}
