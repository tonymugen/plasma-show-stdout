#include <fstream>
#include <string>
#include <filesystem>

#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers.hpp"
#include "catch2/matchers/catch_matchers_string.hpp"

#include "../scriptModules.hpp"

namespace {
	struct TempScript {
		std::filesystem::path path;
		TempScript(const std::string &name, const std::string &body, bool executable = true) {
			path = std::filesystem::temp_directory_path() / name;
			std::ofstream out(path);
			out << "#!/bin/sh\n" << body;
			out.close();
			if (executable) {
				std::filesystem::permissions(path,
					std::filesystem::perms::owner_exec |
					std::filesystem::perms::group_exec |
					std::filesystem::perms::others_exec,
					std::filesystem::perm_options::add);
			}
		}
		~TempScript() { std::filesystem::remove(path); }
	};
}

TEST_CASE("runScript non-existent path", "[runScript]") {
	const auto path = std::filesystem::temp_directory_path() / "pss_nonexistent.sh";
	std::filesystem::remove(path);
	const std::string result = PSSspace::runScript(path);
	REQUIRE_THAT( result, Catch::Matchers::ContainsSubstring( path.string() ) );
	REQUIRE_THAT( result, Catch::Matchers::ContainsSubstring("does not exist") );
}

TEST_CASE("runScript captures stdout", "[runScript]") {
	const TempScript script("pss_output.sh", "echo 'hello world'\n");
	const std::string result = PSSspace::runScript(script.path);
	REQUIRE_THAT( result, Catch::Matchers::ContainsSubstring("hello world") );
}

TEST_CASE("runScript empty output", "[runScript]") {
	const TempScript script("pss_empty.sh", "exit 0\n");
	const std::string result = PSSspace::runScript(script.path);
	REQUIRE( result.empty() );
}

TEST_CASE("runScript output longer than read buffer", "[runScript]") {
	// internal buffer is 100 chars; 200-char output forces multiple fgets iterations
	const std::string longLine(200, 'A');
	const TempScript script("pss_long.sh", "printf '" + longLine + "'\n");
	const std::string result = PSSspace::runScript(script.path);
	REQUIRE( result.size() >= 200 );
	REQUIRE_THAT( result, Catch::Matchers::ContainsSubstring(longLine) );
}

TEST_CASE("runScript stderr not captured", "[runScript]") {
	const TempScript script("pss_stderr.sh", "echo 'error output' >&2\n");
	const std::string result = PSSspace::runScript(script.path);
	REQUIRE( result.empty() );
}

TEST_CASE("runScript non-executable file", "[runScript]") {
	// popen() itself succeeds (the shell forks), but the shell cannot exec the file,
	// so no stdout is produced — the "Failed to execute" path is not taken here
	const TempScript script("pss_noexec.sh", "echo 'should not appear'\n", false);
	const std::string result = PSSspace::runScript(script.path);
	REQUIRE_THAT( result, !Catch::Matchers::ContainsSubstring("should not appear") );
}
