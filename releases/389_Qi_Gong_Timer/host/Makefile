# PC tests for the card (NOT the RP2040).  `make fast` = quick build; `make test_render` = with the overflow sanitizer
# (UBSan + ASan): any runtime error it prints is a REAL bug that could corrupt the card.  Same warning flags as the card build.
CXX ?= g++
WARN = -Wall -Wextra -Wdouble-promotion -Wfloat-conversion -Wshadow
INC  = -Imock -I..
DEPS = test_render.cpp test_fx.inc test_keeper.inc ../qi_fx.h ../qi_engine.h ../qi_config.h ../qi_card.h ../cardkit_dsp.h mock/ComputerCard.h
test_render: $(DEPS)
	@mkdir -p out
	$(CXX) -std=c++17 -O2 $(WARN) $(INC) -DQI_HOST_TEST -fsanitize=undefined,address -fno-sanitize-recover=undefined -g -o $@ test_render.cpp
fast: $(DEPS)
	@mkdir -p out
	$(CXX) -std=c++17 -O2 $(WARN) $(INC) -DQI_HOST_TEST -o test_render_fast test_render.cpp
# Compile the engine + glue EXACTLY as the card does (no QI_HOST_TEST), to catch test-only names leaking into card code.
cardcheck: $(DEPS)
	$(CXX) -std=c++17 -O2 $(WARN) $(INC) -fsyntax-only -x c++ cardcheck.cpp
# The start-up tables as JSON (for scripts/bench_cycles.py)
tables: dump_tables.cpp ../qi_fx.h ../qi_config.h ../cardkit_dsp.h
	@mkdir -p out
	$(CXX) -std=c++17 -O2 $(WARN) -I.. -o dump_tables dump_tables.cpp && ./dump_tables > out/tables.json
clean:
	rm -f test_render test_render_fast dump_tables out/*
