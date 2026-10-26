#include "test_framework.h"

#include <cstring>

namespace crate::test {

int failures = 0;
int checks = 0;

std::vector<TestCase>& registry ()
{
	static std::vector<TestCase> tests;
	return tests;
}

void reportFailure (const char* file, int line, const std::string& message)
{
	++failures;
	std::fprintf (stderr, "FAIL %s:%d: %s\n", file, line, message.c_str ());
}

int runAll ()
{
	int failedTests = 0;
	for (const auto& test : registry ())
	{
		int before = failures;
		std::printf ("[ RUN      ] %s\n", test.name);
		test.fn ();
		if (failures == before)
			std::printf ("[       OK ] %s\n", test.name);
		else
		{
			std::printf ("[  FAILED  ] %s\n", test.name);
			++failedTests;
		}
	}
	std::printf ("\n%d checks, %d failures, %zu tests, %d failed tests\n", checks, failures,
	             registry ().size (), failedTests);
	return failures == 0 ? 0 : 1;
}

} // namespace crate::test

int main ()
{
	return crate::test::runAll ();
}
