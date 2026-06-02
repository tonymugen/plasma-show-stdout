#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <csignal>

#include <QCoreApplication>
#include <QObject>

#include "catch2/catch_session.hpp"
#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers.hpp"
#include "catch2/matchers/catch_matchers_string.hpp"

#include "../plasma-show-stdout.hpp"

using namespace std::chrono_literals;

namespace {
	// Writes a throwaway executable script under the temp dir and removes it on
	// destruction. Mirrors the helper in tests.cpp (kept local so the two test
	// binaries stay independent).
	struct TempScript {
		std::filesystem::path path;
		TempScript(const std::string &name, const std::string &body) {
			path = std::filesystem::temp_directory_path() / name;
			std::ofstream out(path);
			out << "#!/bin/sh\n" << body;
			out.close();
			std::filesystem::permissions(path,
				std::filesystem::perms::owner_exec |
				std::filesystem::perms::group_exec |
				std::filesystem::perms::others_exec,
				std::filesystem::perm_options::add);
		}
		~TempScript() { std::filesystem::remove(path); }
	};

	// Service the main-thread Qt event loop until pred() holds or the timeout
	// elapses. ScriptOutput's bridge posts updates with
	// QMetaObject::invokeMethod(Qt::QueuedConnection), so text()/textChanged only
	// advance while the loop is being pumped. Returns whether pred() became true.
	template<typename Pred>
	bool pumpUntil(Pred pred, std::chrono::milliseconds timeout) {
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while ( !pred() ) {
			if (std::chrono::steady_clock::now() >= deadline) {
				return false;
			}
			QCoreApplication::processEvents();
			std::this_thread::sleep_for(5ms); // yield to the worker/bridge threads
		}
		return true;
	}

	// interval 0 makes a timed module re-run as fast as it can, so output appears
	// promptly without the test waiting on a real polling interval.
	PSSspace::ModuleSpec timedSpec(const std::filesystem::path &script, size_t outputLimit = 1000) {
		return { PSSspace::ModuleSpec::Kind::Timed, script,
			std::chrono::duration<uint32_t>{0}, 0, outputLimit };
	}
	PSSspace::ModuleSpec signalSpec(const std::filesystem::path &script, int signalNumber,
			size_t outputLimit = 1000) {
		return { PSSspace::ModuleSpec::Kind::Signal, script,
			std::chrono::duration<uint32_t>{}, signalNumber, outputLimit };
	}
}

// Custom main so the QCoreApplication (the event-loop owner the bridge posts to)
// is created before any test runs and destroyed before the leak check at exit.
int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	return Catch::Session().run(argc, argv);
}

TEST_CASE("ScriptOutput with no modules exposes empty text", "[ScriptOutput]") {
	// Also exercises the no-thread construct/destruct path (no workers, no
	// signal-wait thread, no signal handlers installed).
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{} };
	REQUIRE( out.text().isEmpty() );
}

TEST_CASE("ScriptOutput runs a timed module and publishes its output", "[ScriptOutput]") {
	const TempScript script("pss_so_timed.sh", "printf 'so timed'\n");
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{ timedSpec(script.path) } };

	REQUIRE( pumpUntil([&]{ return !out.text().isEmpty(); }, 5s) );
	REQUIRE_THAT( out.text().toStdString(), Catch::Matchers::ContainsSubstring("so timed") );
}

TEST_CASE("ScriptOutput truncates a module's output to its spec limit", "[ScriptOutput]") {
	const std::string longLine(200, 'Z');
	const TempScript script("pss_so_trunc.sh", "printf '" + longLine + "'\n");
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{ timedSpec(script.path, 50) } };

	REQUIRE( pumpUntil([&]{ return !out.text().isEmpty(); }, 5s) );
	REQUIRE( out.text().size() <= 50 );
}

TEST_CASE("ScriptOutput joins multiple module fragments in order with ' | '", "[ScriptOutput]") {
	const TempScript first("pss_so_first.sh", "printf 'AAA'\n");
	const TempScript second("pss_so_second.sh", "printf 'BBB'\n");
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{
		timedSpec(first.path), timedSpec(second.path) } };

	const bool bothPresent = pumpUntil([&]{
		const std::string text = out.text().toStdString();
		return text.find("AAA") != std::string::npos
			&& text.find("BBB") != std::string::npos;
	}, 5s);
	REQUIRE(bothPresent);
	// fragments are joined in slot (spec) order, independent of which finished first
	REQUIRE( out.text().toStdString() == "AAA | BBB" );
}

TEST_CASE("ScriptOutput emits textChanged when a module produces output", "[ScriptOutput]") {
	const TempScript script("pss_so_notify.sh", "printf 'notify'\n");
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{ timedSpec(script.path) } };

	int emissions = 0;
	QObject::connect(&out, &PSSspace::ScriptOutput::textChanged,
		[&emissions]{ ++emissions; });

	REQUIRE( pumpUntil([&]{ return emissions > 0; }, 5s) );
	REQUIRE_THAT( out.text().toStdString(), Catch::Matchers::ContainsSubstring("notify") );
}

TEST_CASE("ScriptOutput runs a signal module only when its signal is raised", "[ScriptOutput]") {
	const TempScript script("pss_so_signal.sh", "printf 'so signal'\n");
	const int signalNumber = SIGRTMIN + 4;
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{
		signalSpec(script.path, signalNumber) } };

	// nothing has triggered the module yet, so it must not have run
	REQUIRE( out.text().isEmpty() );

	raise(signalNumber); // delivered to this thread; the file-scope handler posts the semaphore
	REQUIRE( pumpUntil([&]{ return !out.text().isEmpty(); }, 5s) );
	REQUIRE_THAT( out.text().toStdString(), Catch::Matchers::ContainsSubstring("so signal") );
}
