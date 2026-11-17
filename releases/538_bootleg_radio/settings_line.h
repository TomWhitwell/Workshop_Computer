// Settings line parsing for Bootleg Radio.
//
// This is deliberately hardware-independent so it can be exercised by a small
// host test (tests/test_settings_line.cpp) as well as by the firmware.
//
// The format is close to Music Thing's Radio Music settings.txt:
//   key = value
// - '#' and ';' start a comment, even after a value
// - whitespace inside the key is ignored ("loop Mode" == "loopmode")
// - keys are case-insensitive (they are normalised to lower case)
// - a UTF-8 BOM at the start of the first line is ignored
// - CR / LF / CRLF line endings are all accepted by the caller
//
// Unknown keys and values are not rejected here; the caller ignores them.

#pragma once

#include <cstddef>
#include <cstring>

struct SettingLine
{
	char key[64];
	char value[128];
};

// Lower-case a single ASCII character. Kept local so parsing does not depend on
// locale tables or <cctype>'s global state.
inline char SettingsLower(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

// Parse one line. Returns false for blank lines, comments, and lines without a
// key or a value. On success, out.key is lower-case with spaces/tabs removed and
// out.value is trimmed but otherwise unchanged.
inline bool ParseSettingLine(const char *raw, SettingLine &out)
{
	if (raw == nullptr)
	{
		return false;
	}

	// Skip a UTF-8 BOM at the beginning of the file/line.
	const unsigned char *u = (const unsigned char *)raw;
	if (strlen(raw) >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF)
	{
		raw += 3;
	}

	// A comment starts anywhere on the line.
	const char *end = raw;
	while (*end != 0 && *end != '#' && *end != ';')
	{
		end++;
	}

	// Trim the remaining text.
	const char *begin = raw;
	while (begin < end && (*begin == ' ' || *begin == '\t' || *begin == '\r' || *begin == '\n'))
	{
		begin++;
	}
	while (end > begin && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
	{
		end--;
	}
	if (begin == end)
	{
		return false;
	}

	// Split at the first '='.
	const char *eq = begin;
	while (eq < end && *eq != '=')
	{
		eq++;
	}
	if (eq == end)
	{
		return false;
	}

	const char *keyBegin = begin;
	const char *keyEnd = eq;
	const char *valueBegin = eq + 1;
	const char *valueEnd = end;

	while (keyBegin < keyEnd && (*keyBegin == ' ' || *keyBegin == '\t'))
	{
		keyBegin++;
	}
	while (keyEnd > keyBegin && (keyEnd[-1] == ' ' || keyEnd[-1] == '\t'))
	{
		keyEnd--;
	}
	while (valueBegin < valueEnd && (*valueBegin == ' ' || *valueBegin == '\t'))
	{
		valueBegin++;
	}
	while (valueEnd > valueBegin && (valueEnd[-1] == ' ' || valueEnd[-1] == '\t'))
	{
		valueEnd--;
	}

	size_t keyLen = 0;
	for (const char *p = keyBegin; p < keyEnd; p++)
	{
		if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		{
			continue;
		}
		if (keyLen + 1 >= sizeof(out.key))
		{
			return false;
		}
		out.key[keyLen++] = SettingsLower(*p);
	}
	out.key[keyLen] = 0;

	size_t valueLen = 0;
	for (const char *p = valueBegin; p < valueEnd; p++)
	{
		if (valueLen + 1 >= sizeof(out.value))
		{
			return false;
		}
		out.value[valueLen++] = *p;
	}
	out.value[valueLen] = 0;

	return keyLen > 0 && valueLen > 0;
}

// Radio Music's loopMode uses integers 0/1/2. Return that number, or -1 if the
// value is not exactly one of them. Trailing whitespace is allowed.
inline int ParseLoopModeNumber(const char *value)
{
	if (value == nullptr)
	{
		return -1;
	}
	while (*value == ' ' || *value == '\t')
	{
		value++;
	}
	if (*value < '0' || *value > '9')
	{
		return -1;
	}

	long n = 0;
	while (*value >= '0' && *value <= '9')
	{
		n = n * 10 + (*value - '0');
		if (n > 2)
		{
			return -1;
		}
		value++;
	}
	while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
	{
		value++;
	}
	if (*value != 0)
	{
		return -1;
	}
	return (int)n;
}
