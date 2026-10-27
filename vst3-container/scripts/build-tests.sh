#!/usr/bin/env bash
# Build and run the SDK-free unit tests with nothing but a C++17 compiler.
# Used by CI when no CMake/SDK is available and as the fast inner loop locally.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-${ROOT}/build-tests}"
CXX="${CXX:-g++}"
CC="${CC:-gcc}"
CXXFLAGS="${CXXFLAGS:--std=c++17 -Wall -Wextra -Wno-unused-parameter -O1 -g}"

rm -rf "${OUT}"
mkdir -p "${OUT}/obj"

sources=()
while IFS= read -r file; do sources+=("${file}"); done < <(
	# source/vst3 and tests/integration are the SDK-facing layers; they need the Steinberg
	# headers and are built by CMake only. Everything else must compile with nothing but a
	# C++17 compiler.
	find "${ROOT}/source" "${ROOT}/tests" -name '*.cpp' | grep -v '/source/vst3/' |
		grep -v '/tests/integration/' | sort
)

for file in "${sources[@]}"; do
	relative="${file#"${ROOT}/"}"
	object="${OUT}/obj/$(echo "${relative}" | tr '/' '_' | sed 's/\.cpp$/.o/')"
	"${CXX}" ${CXXFLAGS} -I"${ROOT}/source" -I"${ROOT}/tests" -I"${ROOT}" \
		-I"${ROOT}/third_party/sqlite" -c "${file}" -o "${object}"
done

"${CC}" -O1 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION \
	-c "${ROOT}/third_party/sqlite/sqlite3.c" -o "${OUT}/obj/sqlite3.o"

"${CXX}" "${OUT}"/obj/*.o -o "${OUT}/crate_tests"
"${OUT}/crate_tests"
