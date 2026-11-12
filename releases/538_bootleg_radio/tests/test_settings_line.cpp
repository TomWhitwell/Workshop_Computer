// Host-side test for settings_line.h. Build and run it with:
//
//   c++ -std=c++17 -Wall -Wextra tests/test_settings_line.cpp -o tests/test_settings_line
//   ./tests/test_settings_line
//
// This is not part of the Pico firmware build. It covers the formatting rules
// that make an original Radio Music settings.txt safe to parse.

#include "../settings_line.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

static int failures = 0;

static void Check(bool ok, const char *what)
{
	if (!ok)
	{
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

static void CheckLine(const char *input, const char *expectedKey, const char *expectedValue)
{
	SettingLine line;
	const bool ok = ParseSettingLine(input, line);
	Check(ok, input);
	if (!ok)
	{
		return;
	}
	Check(std::strcmp(line.key, expectedKey) == 0, input);
	Check(std::strcmp(line.value, expectedValue) == 0, input);
}

static void CheckRejected(const char *input)
{
	SettingLine line;
	Check(!ParseSettingLine(input, line), input);
}

int main()
{
	// Plain key/value, spaces around '='.
	CheckLine("loopMode = 1", "loopmode", "1");
	CheckLine("  mode=sync  ", "mode", "sync");

	// Radio Music ignores whitespace inside key names.
	CheckLine("loop Mode = 2", "loopmode", "2");
	CheckLine("Start Pot immediate = 1", "startpotimmediate", "1");

	// Comments can appear after a value, with or without a space.
	CheckLine("loopMode = 0 # no loop", "loopmode", "0");
	CheckLine("loopMode = 0# no loop", "loopmode", "0");
	CheckLine("loopMode = 2 ; ping-pong", "loopmode", "2");

	// Blank lines, comments and malformed lines are rejected.
	CheckRejected("");
	CheckRejected("   ");
	CheckRejected("# comment");
	CheckRejected("; comment");
	CheckRejected("loopMode");
	CheckRejected("loopMode =");

	// A UTF-8 BOM at the start of the first line is ignored.
	CheckLine("\xEF\xBB\xBFloopMode = 1", "loopmode", "1");

	// Values are case-preserved; the caller compares case-insensitively.
	CheckLine("MODE = Sync", "mode", "Sync");

	// Radio Music's numeric loopMode values.
	Check(ParseLoopModeNumber("0") == 0, "loopMode 0");
	Check(ParseLoopModeNumber("1") == 1, "loopMode 1");
	Check(ParseLoopModeNumber("2") == 2, "loopMode 2");
	Check(ParseLoopModeNumber(" 2 ") == 2, "loopMode 2 with spaces");
	Check(ParseLoopModeNumber("3") == -1, "loopMode 3 is not valid");
	Check(ParseLoopModeNumber("10") == -1, "loopMode 10 is not valid");
	Check(ParseLoopModeNumber("loop") == -1, "word loop is not numeric");
	Check(ParseLoopModeNumber("") == -1, "empty loopMode is not numeric");

	// Read the fixture: it is deliberately larger than the old 1 KB limit and
	// puts loopMode near the end, behind a long comment block.
	std::ifstream fixture("tests/fixtures/radio-music-settings.txt");
	Check(fixture.good(), "fixture opens");
	if (fixture.good())
	{
		std::string text((std::istreambuf_iterator<char>(fixture)),
		                 std::istreambuf_iterator<char>());
		Check(text.size() > 1024, "fixture is larger than the old 1 KB limit");

		SettingLine line;
		bool found = false;
		int lastLoopMode = -2;
		std::string current;
		for (char c : text)
		{
			if (c == '\n')
			{
				if (ParseSettingLine(current.c_str(), line) &&
				    std::strcmp(line.key, "loopmode") == 0)
				{
					found = true;
					lastLoopMode = ParseLoopModeNumber(line.value);
				}
				current.clear();
			}
			else if (c != '\r')
			{
				current += c;
			}
		}
		if (!current.empty() &&
		    ParseSettingLine(current.c_str(), line) &&
		    std::strcmp(line.key, "loopmode") == 0)
		{
			found = true;
			lastLoopMode = ParseLoopModeNumber(line.value);
		}
		Check(found, "fixture loopMode is found");
		Check(lastLoopMode == 0, "fixture's last loopMode (near the end) wins");
	}

	if (failures == 0)
	{
		std::printf("OK: settings_line tests passed\n");
		return 0;
	}
	std::printf("%d failure(s)\n", failures);
	return 1;
}
