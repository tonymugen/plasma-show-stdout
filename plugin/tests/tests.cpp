#include <fstream>
#include <string>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>

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

/*
 * runScript function
 */

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

/*
 * TimedModule
 */

TEST_CASE("TimedModule executes script and writes output", "[TimedModule]") {
	const TempScript script("pss_timed_output.sh", "echo 'timed hello'\n");
	auto mutex        = std::make_shared<std::mutex>();
	auto stopSignal   = std::make_shared<std::condition_variable>();
	auto stop         = std::make_shared<bool>(false);
	auto signalToMain = std::make_unique<std::condition_variable>();
	auto* signalRaw   = signalToMain.get();
	auto outputString = std::make_unique<std::string>();
	auto* outputRaw   = outputString.get();

	PSSspace::TimedModule mod(
		std::chrono::duration<uint32_t>{0},
		stopSignal, mutex, stop,
		std::move(signalToMain),
		script.path, 1000,
		std::move(outputString)
	);
	std::thread t( std::ref(mod) );

	{
		std::unique_lock<std::mutex> lock(*mutex);
		const bool notified = signalRaw->wait_for(
			lock,
			std::chrono::seconds(5),
			[&]{ return !outputRaw->empty(); }
		);
		REQUIRE(notified);
		REQUIRE_THAT( *outputRaw, Catch::Matchers::ContainsSubstring("timed hello") );
	}

	{ std::lock_guard<std::mutex> lg(*mutex); *stop = true; }
	stopSignal->notify_one();
	t.join();
}

TEST_CASE("TimedModule truncates output to limit", "[TimedModule]") {
	const std::string longLine(200, 'B');
	const TempScript script("pss_timed_trunc.sh", "printf '" + longLine + "'\n");
	auto mutex        = std::make_shared<std::mutex>();
	auto stopSignal   = std::make_shared<std::condition_variable>();
	auto stop         = std::make_shared<bool>(false);
	auto signalToMain = std::make_unique<std::condition_variable>();
	auto* signalRaw   = signalToMain.get();
	auto outputString = std::make_unique<std::string>();
	auto* outputRaw   = outputString.get();

	PSSspace::TimedModule mod(
		std::chrono::duration<uint32_t>{0},
		stopSignal, mutex, stop,
		std::move(signalToMain),
		script.path, 50,
		std::move(outputString)
	);
	std::thread t( std::ref(mod) );

	{
		std::unique_lock<std::mutex> lock(*mutex);
		const bool notified = signalRaw->wait_for(
			lock,
			std::chrono::seconds(5),
			[&]{ return !outputRaw->empty(); }
		);
		REQUIRE(notified);
		REQUIRE(outputRaw->size() <= 50);
	}

	{ std::lock_guard<std::mutex> lg(*mutex); *stop = true; }
	stopSignal->notify_one();
	t.join();
}

TEST_CASE("TimedModule stops on stop signal", "[TimedModule]") {
	const TempScript script("pss_timed_stop.sh", "echo 'x'\n");
	auto mutex        = std::make_shared<std::mutex>();
	auto stopSignal   = std::make_shared<std::condition_variable>();
	auto stop         = std::make_shared<bool>(false);
	auto signalToMain = std::make_unique<std::condition_variable>();
	auto* signalRaw   = signalToMain.get();
	auto outputString = std::make_unique<std::string>();
	auto* outputRaw   = outputString.get();

	PSSspace::TimedModule mod(
		std::chrono::duration<uint32_t>{0},
		stopSignal, mutex, stop,
		std::move(signalToMain),
		script.path, 1000,
		std::move(outputString)
	);
	std::thread t( std::ref(mod) );

	// wait for at least one execution to confirm the loop is running
	{
		std::unique_lock<std::mutex> lock(*mutex);
		signalRaw->wait_for(
			lock,
			std::chrono::seconds(5),
			[&]{ return !outputRaw->empty(); }
		);
	}

	{ std::lock_guard<std::mutex> lg(*mutex); *stop = true; }
	stopSignal->notify_one();
	t.join(); // returns only if the thread actually exited
}

TEST_CASE("TimedModule executes repeatedly", "[TimedModule]") {
	const TempScript script("pss_timed_repeat.sh", "echo 'repeat'\n");
	auto mutex        = std::make_shared<std::mutex>();
	auto stopSignal   = std::make_shared<std::condition_variable>();
	auto stop         = std::make_shared<bool>(false);
	auto signalToMain = std::make_unique<std::condition_variable>();
	auto* signalRaw   = signalToMain.get();
	auto outputString = std::make_unique<std::string>();
	auto* outputRaw   = outputString.get();

	PSSspace::TimedModule mod(
		std::chrono::duration<uint32_t>{0},
		stopSignal, mutex, stop,
		std::move(signalToMain),
		script.path, 1000,
		std::move(outputString)
	);
	std::thread t( std::ref(mod) );

	bool first  = false;
	bool second = false;
	{
		std::unique_lock<std::mutex> lock(*mutex);
		first = signalRaw->wait_for(
			lock,
			std::chrono::seconds(5),
			[&]{ return !outputRaw->empty(); }
		);
		outputRaw->clear(); // reset under lock so the next notification is unambiguous
	}
	{
		std::unique_lock<std::mutex> lock(*mutex);
		second = signalRaw->wait_for(
			lock,
			std::chrono::seconds(5),
			[&]{ return !outputRaw->empty(); }
		);
	}
	REQUIRE(first);
	REQUIRE(second);

	{ std::lock_guard<std::mutex> lg(*mutex); *stop = true; }
	stopSignal->notify_one();
	t.join();
}

/*
 * SignalModule
 */

TEST_CASE("SignalModule executes script on trigger and writes output", "[SignalModule]") {
	const TempScript script("pss_sig_output.sh", "echo 'sig hello'\n");
	auto mutex        = std::make_shared<std::mutex>();
	auto execute      = std::make_shared<bool>(false);
	auto stop         = std::make_shared<bool>(false);
	auto execSignal   = std::make_unique<std::condition_variable>();
	auto* execRaw     = execSignal.get();
	auto signalToMain = std::make_unique<std::condition_variable>();
	auto* signalRaw   = signalToMain.get();
	auto outputString = std::make_unique<std::string>();
	auto* outputRaw   = outputString.get();

	PSSspace::SignalModule mod(
		std::move(execSignal), mutex, execute, stop,
		std::move(signalToMain),
		script.path, 1000,
		outputString
	);
	std::thread t( std::ref(mod) );

	{ std::lock_guard<std::mutex> lg(*mutex); *execute = true; }
	execRaw->notify_one();

	{
		std::unique_lock<std::mutex> lock(*mutex);
		const bool notified = signalRaw->wait_for(
			lock,
			std::chrono::seconds(5),
			[&]{ return !outputRaw->empty(); }
		);
		REQUIRE(notified);
		REQUIRE_THAT( *outputRaw, Catch::Matchers::ContainsSubstring("sig hello") );
	}

	{ std::lock_guard<std::mutex> lg(*mutex); *stop = true; }
	execRaw->notify_one();
	t.join();
}

TEST_CASE("SignalModule truncates output to limit", "[SignalModule]") {
	const std::string longLine(200, 'C');
	const TempScript script("pss_sig_trunc.sh", "printf '" + longLine + "'\n");
	auto mutex        = std::make_shared<std::mutex>();
	auto execute      = std::make_shared<bool>(false);
	auto stop         = std::make_shared<bool>(false);
	auto execSignal   = std::make_unique<std::condition_variable>();
	auto* execRaw     = execSignal.get();
	auto signalToMain = std::make_unique<std::condition_variable>();
	auto* signalRaw   = signalToMain.get();
	auto outputString = std::make_unique<std::string>();
	auto* outputRaw   = outputString.get();

	PSSspace::SignalModule mod(
		std::move(execSignal), mutex, execute, stop,
		std::move(signalToMain),
		script.path, 50,
		outputString
	);
	std::thread t( std::ref(mod) );

	{ std::lock_guard<std::mutex> lg(*mutex); *execute = true; }
	execRaw->notify_one();

	{
		std::unique_lock<std::mutex> lock(*mutex);
		const bool notified = signalRaw->wait_for(
			lock,
			std::chrono::seconds(5),
			[&]{ return !outputRaw->empty(); }
		);
		REQUIRE(notified);
		REQUIRE(outputRaw->size() <= 50);
	}

	{ std::lock_guard<std::mutex> lg(*mutex); *stop = true; }
	execRaw->notify_one();
	t.join();
}

TEST_CASE("SignalModule stops on stop signal", "[SignalModule]") {
	const TempScript script("pss_sig_stop.sh", "echo 'y'\n");
	auto mutex        = std::make_shared<std::mutex>();
	auto execute      = std::make_shared<bool>(false);
	auto stop         = std::make_shared<bool>(false);
	auto execSignal   = std::make_unique<std::condition_variable>();
	auto* execRaw     = execSignal.get();
	auto outputString = std::make_unique<std::string>();

	PSSspace::SignalModule mod(
		std::move(execSignal), mutex, execute, stop,
		std::make_unique<std::condition_variable>(),
		script.path, 1000,
		outputString
	);
	std::thread t( std::ref(mod) );

	{ std::lock_guard<std::mutex> lg(*mutex); *stop = true; }
	execRaw->notify_one();
	t.join(); // returns only if the thread actually exited
}

TEST_CASE("SignalModule executes on each trigger", "[SignalModule]") {
	const TempScript script("pss_sig_repeat.sh", "echo 'trigger'\n");
	auto mutex        = std::make_shared<std::mutex>();
	auto execute      = std::make_shared<bool>(false);
	auto stop         = std::make_shared<bool>(false);
	auto execSignal   = std::make_unique<std::condition_variable>();
	auto* execRaw     = execSignal.get();
	auto signalToMain = std::make_unique<std::condition_variable>();
	auto* signalRaw   = signalToMain.get();
	auto outputString = std::make_unique<std::string>();
	auto* outputRaw   = outputString.get();

	PSSspace::SignalModule mod(
		std::move(execSignal), mutex, execute, stop,
		std::move(signalToMain),
		script.path, 1000,
		outputString
	);
	std::thread t( std::ref(mod) );

	{ std::lock_guard<std::mutex> lg(*mutex); *execute = true; }
	execRaw->notify_one();

	bool first  = false;
	bool second = false;
	{
		std::unique_lock<std::mutex> lock(*mutex);
		first = signalRaw->wait_for(
			lock,
			std::chrono::seconds(5),
			[&]{ return !outputRaw->empty(); }
		);
		outputRaw->clear(); // reset under lock before sending the next trigger
	}

	{ std::lock_guard<std::mutex> lg(*mutex); *execute = true; }
	execRaw->notify_one();

	{
		std::unique_lock<std::mutex> lock(*mutex);
		second = signalRaw->wait_for(
			lock,
			std::chrono::seconds(5),
			[&]{ return !outputRaw->empty(); }
		);
	}
	REQUIRE(first);
	REQUIRE(second);

	{ std::lock_guard<std::mutex> lg(*mutex); *stop = true; }
	execRaw->notify_one();
	t.join();
}
