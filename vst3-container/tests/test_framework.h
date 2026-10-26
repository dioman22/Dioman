// Minimal test harness. No external framework: the container must be buildable on a
// machine that has nothing but a C++17 compiler and the VST3 SDK.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace crate::test {

struct TestCase
{
	const char* name;
	void (*fn) ();
};

std::vector<TestCase>& registry ();
int runAll ();

struct Registrar
{
	Registrar (const char* name, void (*fn) ()) { registry ().push_back ({name, fn}); }
};

extern int failures;
extern int checks;

void reportFailure (const char* file, int line, const std::string& message);

} // namespace crate::test

#define CRATE_TEST(name)                                                                       \
	static void crate_test_##name ();                                                          \
	static ::crate::test::Registrar crate_test_registrar_##name (#name, crate_test_##name);    \
	static void crate_test_##name ()

#define CHECK(cond)                                                                            \
	do                                                                                         \
	{                                                                                          \
		++::crate::test::checks;                                                               \
		if (!(cond))                                                                           \
			::crate::test::reportFailure (__FILE__, __LINE__, "CHECK(" #cond ") failed");      \
	} while (0)

#define CHECK_EQ(a, b)                                                                         \
	do                                                                                         \
	{                                                                                          \
		++::crate::test::checks;                                                               \
		if (!((a) == (b)))                                                                     \
			::crate::test::reportFailure (__FILE__, __LINE__,                                   \
			                              std::string ("CHECK_EQ(" #a ", " #b ") failed"));    \
	} while (0)

#define CHECK_MSG(cond, msg)                                                                   \
	do                                                                                         \
	{                                                                                          \
		++::crate::test::checks;                                                               \
		if (!(cond))                                                                           \
			::crate::test::reportFailure (__FILE__, __LINE__, (msg));                          \
	} while (0)
