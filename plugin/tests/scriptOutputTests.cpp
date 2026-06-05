#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <csignal>

#include <QCoreApplication>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

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

	// QVariantMap entries mirroring what the QML config wiring passes to
	// setModules: kind 0/1, a script path string, and (per kind) interval
	// seconds or an RTMIN offset. setModules computes SIGRTMIN + signalOffset.
	QVariantMap timedEntry(const std::filesystem::path &script, int intervalSeconds = 0,
			int outputLimit = 1000) {
		return {
			{ QStringLiteral("kind"),        0 },
			{ QStringLiteral("script"),      QString::fromStdString(script.string()) },
			{ QStringLiteral("interval"),    intervalSeconds },
			{ QStringLiteral("outputLimit"), outputLimit }
		};
	}
	QVariantMap signalEntry(const std::filesystem::path &script, int signalOffset,
			int outputLimit = 1000) {
		return {
			{ QStringLiteral("kind"),         1 },
			{ QStringLiteral("script"),       QString::fromStdString(script.string()) },
			{ QStringLiteral("signalOffset"), signalOffset },
			{ QStringLiteral("outputLimit"),  outputLimit }
		};
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

TEST_CASE("ScriptOutput exposes the host PID and name for signal triggering", "[ScriptOutput]") {
	// The config UI shows these so the user can target the right host process.
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{} };
	REQUIRE( out.hostPid() == static_cast<qint64>(QCoreApplication::applicationPid()) );
	REQUIRE( out.hostPid() > 0 );
	// hostName is /proc/self/comm — what `pkill <name>` matches against
	REQUIRE_FALSE( out.hostName().isEmpty() );
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

TEST_CASE("ScriptOutput.setModules starts a timed module at runtime", "[ScriptOutput]") {
	// Construct empty (the QML path), then configure via the runtime entry point.
	const TempScript script("pss_so_set_timed.sh", "printf 'set timed'\n");
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{} };
	REQUIRE( out.text().isEmpty() );

	out.setModules( QVariantList{ timedEntry(script.path) } );
	REQUIRE( pumpUntil([&]{ return !out.text().isEmpty(); }, 5s) );
	REQUIRE_THAT( out.text().toStdString(), Catch::Matchers::ContainsSubstring("set timed") );
}

TEST_CASE("ScriptOutput.setDelimiter re-joins fragments without restarting workers", "[ScriptOutput]") {
	const TempScript first("pss_so_delim_a.sh", "printf 'AAA'\n");
	const TempScript second("pss_so_delim_b.sh", "printf 'BBB'\n");
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{} };
	REQUIRE( out.delimiter() == QStringLiteral(" | ") ); // historical default

	out.setModules( QVariantList{ timedEntry(first.path, 60), timedEntry(second.path, 60) } );
	REQUIRE( pumpUntil([&]{ return out.text().toStdString() == "AAA | BBB"; }, 5s) );

	// Changing the delimiter rebuilds text_ synchronously from the existing
	// fragments — no new output need arrive, so no pump is required.
	out.setDelimiter( QStringLiteral(" :: ") );
	REQUIRE( out.text().toStdString() == "AAA :: BBB" );

	// An empty delimiter joins with nothing.
	out.setDelimiter( QString{} );
	REQUIRE( out.text().toStdString() == "AAABBB" );
}

TEST_CASE("ScriptOutput.setModules honors a per-entry output limit", "[ScriptOutput]") {
	// The QVariant path carries outputLimit straight from the config; truncation
	// is per module, so a short-limited entry is clipped while a roomier one is not.
	const std::string longLine(200, 'Z');
	const TempScript clipped("pss_so_lim_a.sh", "printf '" + longLine + "'\n");
	const TempScript full("pss_so_lim_b.sh", "printf 'BBB'\n");
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{} };

	out.setModules( QVariantList{
		timedEntry(clipped.path, 0, /*outputLimit=*/10),
		timedEntry(full.path,    0, /*outputLimit=*/1000) } );
	REQUIRE( pumpUntil([&]{
		const std::string text = out.text().toStdString();
		return text.find('Z') != std::string::npos && text.find("BBB") != std::string::npos;
	}, 5s) );
	// first fragment clipped to 10 'Z's, joined with the un-clipped second
	REQUIRE( out.text().toStdString() == std::string(10, 'Z') + " | BBB" );
}

TEST_CASE("ScriptOutput.setModules runs two timed modules with a real interval", "[ScriptOutput]") {
	// Mirrors the GUI: each timed worker runs once then sleeps for the interval,
	// so a lost first-update race would leave the second fragment empty (the
	// interval-0 cases mask such a race by re-running immediately).
	const TempScript first("pss_so_two_a.sh", "printf 'AAA'\n");
	const TempScript second("pss_so_two_b.sh", "printf 'BBB'\n");
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{} };

	out.setModules( QVariantList{ timedEntry(first.path, 60), timedEntry(second.path, 60) } );
	REQUIRE( pumpUntil([&]{
		const std::string text = out.text().toStdString();
		return text.find("AAA") != std::string::npos && text.find("BBB") != std::string::npos;
	}, 5s) );
	REQUIRE( out.text().toStdString() == "AAA | BBB" );
}

TEST_CASE("ScriptOutput.setModules runs a timed module alongside a signal module", "[ScriptOutput]") {
	// The likely failing GUI config: a timed first module and a signal second one.
	const TempScript timed("pss_so_mix_t.sh", "printf 'TMD'\n");
	const TempScript signaled("pss_so_mix_s.sh", "printf 'SIG'\n");
	const int signalOffset = 6;
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{} };

	out.setModules( QVariantList{ timedEntry(timed.path, 60), signalEntry(signaled.path, signalOffset) } );
	// the timed fragment appears immediately; the signal one stays empty until raised
	REQUIRE( pumpUntil([&]{ return out.text().toStdString().find("TMD") != std::string::npos; }, 5s) );
	REQUIRE( out.text().toStdString() == "TMD | " );

	raise(SIGRTMIN + signalOffset);
	REQUIRE( pumpUntil([&]{ return out.text().toStdString().find("SIG") != std::string::npos; }, 5s) );
	REQUIRE( out.text().toStdString() == "TMD | SIG" );
}

TEST_CASE("ScriptOutput.setModules reconfigures from one script to another", "[ScriptOutput]") {
	const TempScript first("pss_so_recfg_a.sh", "printf 'AAA'\n");
	const TempScript second("pss_so_recfg_b.sh", "printf 'BBB'\n");
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{} };

	out.setModules( QVariantList{ timedEntry(first.path) } );
	REQUIRE( pumpUntil([&]{ return out.text().toStdString().find("AAA") != std::string::npos; }, 5s) );

	// Reconfiguring tears down the first worker and starts the second; the
	// display must end up showing only the new module's output.
	out.setModules( QVariantList{ timedEntry(second.path) } );
	REQUIRE( pumpUntil([&]{
		const std::string text = out.text().toStdString();
		return text.find("BBB") != std::string::npos && text.find("AAA") == std::string::npos;
	}, 5s) );
}

TEST_CASE("ScriptOutput.setModules reconfigure drops stale queued bridge updates", "[ScriptOutput]") {
	// Regression guard for a use-after-free / wrong-slot write: a bridge update is
	// posted with QMetaObject::invokeMethod(Qt::QueuedConnection) capturing a raw
	// ModuleSlot*. If such an update is still queued when a reconfigure tears that
	// slot down, stopModules_ must flush it (removePostedEvents); otherwise the GUI
	// loop later applies the OLD script's output to whatever now occupies that slot
	// address. interval 0 makes the first worker flood the queue with updates.
	const TempScript first("pss_so_stale_a.sh", "printf 'AAA'\n");
	const TempScript second("pss_so_stale_b.sh", "printf 'BBB'\n");
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{} };

	out.setModules( QVariantList{ timedEntry(first.path) } );
	// Let the worker/bridge enqueue several "AAA" updates WITHOUT pumping, so
	// metacalls referencing the first module's slot pile up in the event queue.
	std::this_thread::sleep_for(50ms);

	// Reconfiguring tears down the first slot; the queued "AAA" metacalls must be
	// discarded here. The second module only ever emits "BBB", so the first script's
	// output reaching the display can only happen via an un-flushed stale metacall.
	out.setModules( QVariantList{ timedEntry(second.path) } );

	bool sawFirstScriptOutput = false;
	QObject::connect(&out, &PSSspace::ScriptOutput::textChanged, [&]{
		if (out.text().toStdString().find("AAA") != std::string::npos) {
			sawFirstScriptOutput = true;
		}
	});

	REQUIRE( pumpUntil([&]{ return out.text().toStdString() == "BBB"; }, 5s) );
	REQUIRE_FALSE( sawFirstScriptOutput ); // no stale "AAA" ever surfaced
}

TEST_CASE("ScriptOutput.setModules with an empty list clears the text", "[ScriptOutput]") {
	const TempScript script("pss_so_clear.sh", "printf 'transient'\n");
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{} };

	out.setModules( QVariantList{ timedEntry(script.path) } );
	REQUIRE( pumpUntil([&]{ return !out.text().isEmpty(); }, 5s) );

	out.setModules( QVariantList{} );
	REQUIRE( pumpUntil([&]{ return out.text().isEmpty(); }, 5s) );
}

TEST_CASE("ScriptOutput.setModules skips entries with an empty script path", "[ScriptOutput]") {
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{} };
	// An unconfigured script (empty path) must not spawn a module.
	out.setModules( QVariantList{ timedEntry(std::filesystem::path{}) } );
	// give any erroneously spawned worker a chance to publish, then assert empty
	REQUIRE_FALSE( pumpUntil([&]{ return !out.text().isEmpty(); }, 500ms) );
	REQUIRE( out.text().isEmpty() );
}

TEST_CASE("ScriptOutput surfaces runScript's error when the script is missing", "[ScriptOutput]") {
	// A configured-but-absent script (e.g. deleted while the widget runs) must not
	// be silently dropped: runScript returns "<path> does not exist" and that text
	// has to reach the text property so the user sees something is wrong. The path
	// must be non-empty (an empty script is filtered out by setModules), so use a
	// temp path that is explicitly removed first.
	const std::filesystem::path missing =
		std::filesystem::temp_directory_path() / "pss_so_absent.sh";
	std::filesystem::remove(missing); // ensure it really isn't there
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{} };

	out.setModules( QVariantList{ timedEntry(missing) } );
	REQUIRE( pumpUntil([&]{ return !out.text().isEmpty(); }, 5s) );
	REQUIRE_THAT( out.text().toStdString(),
		Catch::Matchers::ContainsSubstring("does not exist") );
}

TEST_CASE("ScriptOutput.setModules runs a signal module via its RTMIN offset", "[ScriptOutput]") {
	const TempScript script("pss_so_set_signal.sh", "printf 'set signal'\n");
	const int signalOffset = 5;
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{} };

	out.setModules( QVariantList{ signalEntry(script.path, signalOffset) } );
	REQUIRE( out.text().isEmpty() ); // idle until triggered

	raise(SIGRTMIN + signalOffset); // setModules maps the offset to this raw signal
	REQUIRE( pumpUntil([&]{ return !out.text().isEmpty(); }, 5s) );
	REQUIRE_THAT( out.text().toStdString(), Catch::Matchers::ContainsSubstring("set signal") );
}

TEST_CASE("ScriptOutput.setModules skips a signal entry whose offset is out of range", "[ScriptOutput]") {
	// signalOffset reaches setModules from a hand-editable config string and from a
	// directly callable Q_INVOKABLE, neither bounded by the UI spinbox. An offset
	// resolving outside [SIGRTMIN, SIGRTMAX] must be dropped, not used to index the
	// _NSIG-sized signal table (gSignalFired) — an OOB the Test build's ASan traps.
	const TempScript timed("pss_so_badsig_t.sh", "printf 'TMD'\n");
	const TempScript signaled("pss_so_badsig_s.sh", "printf 'SIG'\n");
	PSSspace::ScriptOutput out{ std::vector<PSSspace::ModuleSpec>{} };

	// Far above SIGRTMAX, and (via a large negative offset) below 0 — both invalid.
	out.setModules( QVariantList{
		timedEntry(timed.path, 60),
		signalEntry(signaled.path, 1000),
		signalEntry(signaled.path, -1000) } );

	// Only the timed module survives. Its fragment shows with NO trailing delimiter,
	// proving the signal entries were dropped rather than spawned-but-idle (which
	// would render "TMD | " for the first, plus another " | " for the second).
	REQUIRE( pumpUntil([&]{ return out.text().toStdString() == "TMD"; }, 5s) );
}
