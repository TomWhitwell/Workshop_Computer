// Prints the start-up tables as JSON, for scripts/bench_cycles.py (the emulator cannot run the float set-up).
#include <cstdio>
#include "qi_fx.h"
using namespace qi;
template <typename T> static void Arr(const char* name, const T* a, int n, bool last = false)
{
	std::printf("  \"%s\": [", name);
	for (int i = 0; i < n; ++i) std::printf("%s%ld", i ? "," : "", (long)a[i]);
	std::printf("]%s\n", last ? "" : ",");
}
int main()
{
	InitTables(); InitFxTables();
	std::printf("{\n");
	Arr("cardkit::gSine", gSine, 1025); Arr("cardkit::gNoteInc", gNoteInc, 130);
	Arr("qi::gRevFbTab", gRevFbTab, 33); Arr("qi::gRevFbCapTab", gRevFbCapTab, 33); Arr("qi::gRevInTab", gRevInTab, 33);
	Arr("qi::gDampTab", gDampTab, 33); Arr("qi::gDampCompTab", gDampCompTab, 33); Arr("qi::gDelTimeTab", gDelTimeTab, 33, true);
	std::printf("}\n");
	return 0;
}
