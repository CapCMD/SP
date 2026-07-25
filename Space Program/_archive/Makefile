# Fenêtre — build sans dépendance. C++20.
# -ffp-contract=off : le déterminisme bit-à-bit n'est pas négociable (mécanisme de commit).
CXX      ?= g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -ffp-contract=off \
            -Iastro_core/include -Imission/include -Itests
BUILD    := build
CORE     := $(wildcard astro_core/src/*.cpp)

.PHONY: all test m00 clean
all: $(BUILD)/fenetre $(BUILD)/test_astro_core $(BUILD)/m00_iterate $(BUILD)/m00_ops $(BUILD)/m00_nav $(BUILD)/m00_program $(BUILD)/m00_postmortem $(BUILD)/m00_sobol $(BUILD)/m01_porkchop $(BUILD)/m01_economy $(BUILD)/m01_corridor $(BUILD)/t01_tour $(BUILD)/t01_veega $(BUILD)/t01_dsm $(BUILD)/t01_refine $(BUILD)/t01_facture $(BUILD)/m00_design $(BUILD)/m00_play

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/fenetre: $(CORE) app/fenetre_cli.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/test_astro_core: $(CORE) tests/test_astro_core.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/m00_iterate: $(CORE) scripts/m00_iterate.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/m00_ops: $(CORE) scripts/m00_ops.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/m00_nav: $(CORE) scripts/m00_nav.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/m00_program: $(CORE) scripts/m00_program.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/m00_postmortem: $(CORE) scripts/m00_postmortem.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/m00_sobol: $(CORE) scripts/m00_sobol.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/m01_porkchop: $(CORE) scripts/m01_porkchop.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/m01_economy: $(CORE) scripts/m01_economy.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/m01_corridor: $(CORE) scripts/m01_corridor.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/t01_tour: $(CORE) scripts/t01_tour.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/t01_veega: $(CORE) scripts/t01_veega.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/t01_dsm: $(CORE) scripts/t01_dsm.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/t01_refine: $(CORE) scripts/t01_refine.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/t01_facture: $(CORE) scripts/t01_facture.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/m00_design: $(CORE) scripts/m00_design.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/m00_play: $(CORE) scripts/m00_play.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

test: $(BUILD)/test_astro_core
	@./$(BUILD)/test_astro_core

m00: all
	@echo "### 1. plan impulsionnel naif (il doit ECHOUER)"; ./$(BUILD)/fenetre design missions/m00_geo.fpl | tail -6
	@echo "### 2. convergence du plan de vol"; ./$(BUILD)/m00_iterate missions/m00_geo.fpl | tail -6
	@echo "### 3. COMMIT, graine 4071 (il doit ECHOUER : Gates)"; ./$(BUILD)/fenetre run missions/m00_geo_solution.fpl --seed 4071 | tail -5
	@echo "### 4. Monte-Carlo boucle ouverte"; ./$(BUILD)/fenetre mc missions/m00_geo_solution.fpl --n 200 | grep "P("
	@echo "### 5. BOUCLE FERMEE : navigation + correction"; ./$(BUILD)/m00_ops missions/m00_geo_solution.fpl 200 | tail -10

clean:
	rm -rf $(BUILD)
