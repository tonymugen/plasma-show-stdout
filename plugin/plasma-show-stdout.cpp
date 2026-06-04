/*
 * Copyright (c) <2026> Anthony J. Greenberg
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/// Plasma interface to display script output
/** \file
 * \author Anthony J. Greenberg
 * \copyright Copyright (c) 2026 Anthony J. Greenberg
 * \version 0.1.0
 *
 * Implementation of display script output in PLasma.
 */


#include <QtQml>
#include <QCoreApplication>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <KLocalizedString>

// C headers for RTMIN signaling
#include <csignal>
#include <semaphore.h>
#include <cerrno>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "plasma-show-stdout.hpp"
#include "scriptModules.hpp"

namespace {
	// A module is run via std::visit on this variant inside its worker thread;
	// both alternatives expose `void operator()() const`, so the visitor is a
	// one-liner and scriptModules.hpp needs no base class.
	using ModuleVariant = std::variant<PSSspace::TimedModule, PSSspace::SignalModule>;

	// Dispatch table for realtime signals. Sized by _NSIG (a macro constant, so
	// it can size a std::array) and indexed by the raw signal number; note that
	// SIGRTMIN/SIGRTMAX are glibc runtime calls and cannot size an array.
	constexpr size_t signalTableSize{_NSIG};
	static_assert(std::atomic<bool>::is_always_lock_free,
		"signal handler requires lock-free atomics");

	// Set in the handler, cleared in the wait loop. Static storage zero-initializes
	// every flag to false before the handler can run.
	std::array<std::atomic<bool>, signalTableSize> gSignalFired;
	// Posted by the async-signal-safe handler, drained by signalWaitLoop_.
	sem_t gTriggerSemaphore;

	// Signal handler: only async-signal-safe operations (a lock-free atomic store
	// and sem_post). All routing to the right module happens in the wait loop.
	void handleTriggerSignal(int signum) {
		if (signum >= 0 && static_cast<size_t>(signum) < signalTableSize) {
			gSignalFired[static_cast<size_t>(signum)].store(true, std::memory_order_relaxed);
		}
		sem_post(&gTriggerSemaphore);
	}

	// Module list for the QML constructor. Empty until a config panel supplies
	// one, so the widget ships with no system-specific scripts baked in.
	std::vector<PSSspace::ModuleSpec> defaultSpecs() {
		return {};
	}

	// Installed once, process-wide, so the Qt-free module layer can surface
	// localized error text without linking KI18n itself. The msgid literals here
	// are what xgettext extracts; the module layer's English fallback mirrors them.
	std::once_flag gTranslatorInstalled;
	void installMessageTranslator() {
		std::call_once(gTranslatorInstalled, [] {
			PSSspace::setMessageTranslator(
				[](const std::string &messageId, const std::string &argument) -> std::string {
					const QString arg = QString::fromStdString(argument);
					if (messageId == "doesNotExist") {
						return i18n("%1 does not exist", arg).toStdString();
					}
					if (messageId == "failedToExecute") {
						return i18n("Failed to execute %1", arg).toStdString();
					}
					return argument;
				}
			);
		});
	}
}

PSSspace::ScriptOutput::ScriptOutput(QObject *parent)
	: ScriptOutput(defaultSpecs(), parent) {}

PSSspace::ScriptOutput::ScriptOutput(std::vector<ModuleSpec> specs, QObject *parent)
	: QObject(parent),
	  mutex_( std::make_shared<std::mutex>() ),
	  stopSignal_( std::make_shared<std::condition_variable>() ),
	  stop_( std::make_shared<bool>(false) ) {
	installMessageTranslator();
	startModules_( std::move(specs) );
}

PSSspace::ScriptOutput::~ScriptOutput() {
	stopModules_();
}

void PSSspace::ScriptOutput::startModules_(std::vector<ModuleSpec> specs) {
	// The instance may be restarting (setModules), so clear the stop flag that a
	// previous stopModules_ set before the new workers consult it.
	{
		std::lock_guard<std::mutex> lock(*mutex_);
		*stop_ = false;
	}
	// Transient holder for the built modules; moved into the workers in pass 2.
	std::vector<ModuleVariant> modules;
	modules.reserve( specs.size() );

	// Pass 1: build every slot and its module, no threads yet. slots_ is a
	// std::deque, so pointers into its elements (captured by the bridges in
	// pass 2) stay valid as further slots are appended here.
	for (const ModuleSpec &spec : specs) {
		ModuleSlot slot;
		auto changeSignal = std::make_unique<std::condition_variable>();
		slot.changeSignal = changeSignal.get();
		auto outputBuffer = std::make_unique<std::string>();
		slot.outputBuffer = outputBuffer.get();

		if (spec.kind == ModuleSpec::Kind::Timed) {
			modules.emplace_back(
				std::in_place_type<PSSspace::TimedModule>,
				spec.interval,
				stopSignal_, mutex_, stop_,
				std::move(changeSignal),
				spec.script,
				spec.outputLimit,
				std::move(outputBuffer)
			);
		} else {
			auto execFlag        = std::make_shared<bool>(false);
			slot.execute         = execFlag;
			slot.signalNumber    = spec.signalNumber;
			auto execSignal      = std::make_unique<std::condition_variable>();
			slot.executionSignal = execSignal.get();
			modules.emplace_back(
				std::in_place_type<PSSspace::SignalModule>,
				std::move(execSignal),
				mutex_, execFlag, stop_,
				std::move(changeSignal),
				spec.script,
				spec.outputLimit,
				outputBuffer // SignalModule takes unique_ptr<string>& and moves from it
			);
		}
		slots_.push_back( std::move(slot) );
	}

	// Global signal state (the trigger semaphore, the process-wide handlers and
	// the wait thread) is touched only when a signal module is actually present,
	// so a module-less instance leaves the shared globals untouched.
	bool anySignal = false;
	for (const ModuleSlot &slot : slots_) {
		if (slot.signalNumber != 0) {
			anySignal = true;
			break;
		}
	}
	if (anySignal) {
		sem_init(&gTriggerSemaphore, 0, 0);
		// Install one handler per distinct signal number before any run is
		// triggered; clear each fired flag so a stale post can't trigger a run.
		for (const ModuleSlot &slot : slots_) {
			if (slot.signalNumber != 0) {
				gSignalFired[static_cast<size_t>(slot.signalNumber)].store(false, std::memory_order_relaxed);
				struct sigaction triggerSpec{};
				triggerSpec.sa_handler = handleTriggerSignal;
				sigemptyset(&triggerSpec.sa_mask);
				triggerSpec.sa_flags = SA_RESTART;
				sigaction(slot.signalNumber, &triggerSpec, nullptr);
			}
		}
	}
	signalsActive_ = anySignal;

	// Pass 2: spawn the workers (each runs its variant) and their bridges.
	for (size_t i = 0; i < slots_.size(); ++i) {
		ModuleSlot &slot = slots_[i];
		slot.worker = std::thread([mod = std::move(modules[i])]() mutable {
			std::visit([](auto &actual){ actual(); }, mod);
		});
		slot.bridge = std::thread(&ScriptOutput::bridgeLoop_, this, &slot);
	}

	if (signalsActive_) {
		signalWaitThread_ = std::thread(&ScriptOutput::signalWaitLoop_, this);
	}
}

void PSSspace::ScriptOutput::stopModules_() {
	// Nothing running and no global signal state installed: nothing to undo.
	if (slots_.empty() && !signalsActive_) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock(*mutex_);
		*stop_ = true;
	}
	// wake every waiter so each loop observes *stop_ and exits
	for (ModuleSlot &slot : slots_) {
		if (slot.changeSignal != nullptr) {
			slot.changeSignal->notify_all();
		}
		if (slot.executionSignal != nullptr) {
			slot.executionSignal->notify_all();
		}
	}
	stopSignal_->notify_all();
	if (signalsActive_) {
		sem_post(&gTriggerSemaphore); // unblock signalWaitLoop_
	}

	// The bridges and the signal-wait loop reference worker-owned condition
	// variables / output strings (and the SignalModule-owned execution CV)
	// through raw pointers, so they must exit before the workers that own them.
	for (ModuleSlot &slot : slots_) {
		if (slot.bridge.joinable()) {
			slot.bridge.join();
		}
	}
	if (signalWaitThread_.joinable()) {
		signalWaitThread_.join();
	}
	for (ModuleSlot &slot : slots_) {
		if (slot.worker.joinable()) {
			slot.worker.join();
		}
	}

	if (signalsActive_) {
		// stop delivering signals to the (about-to-be-destroyed) semaphore
		for (const ModuleSlot &slot : slots_) {
			if (slot.signalNumber != 0) {
				struct sigaction ignoreSpec{};
				ignoreSpec.sa_handler = SIG_IGN;
				sigemptyset(&ignoreSpec.sa_mask);
				sigaction(slot.signalNumber, &ignoreSpec, nullptr);
			}
		}
		sem_destroy(&gTriggerSemaphore);
	}

	// Reset for reuse: a subsequent startModules_ rebuilds from scratch.
	slots_.clear();
	signalsActive_ = false;
}

void PSSspace::ScriptOutput::bridgeLoop_(ModuleSlot *slot) {
	while (true) {
		std::string snapshot;
		{
			std::unique_lock<std::mutex> lock(*mutex_);
			slot->changeSignal->wait(lock, [this, slot]{
				return *stop_ || !slot->outputBuffer->empty();
			});
			if (*stop_) {
				return;
			}
			snapshot = *slot->outputBuffer;
			slot->outputBuffer->clear();
		}
		QString newText = QString::fromStdString(snapshot);
		QMetaObject::invokeMethod(this, [this, slot, newText] {
			if (slot->fragment == newText) {
				return;
			}
			slot->fragment = newText;
			rebuildCombinedText_();
			emit textChanged();
		}, Qt::QueuedConnection);
	}
}

void PSSspace::ScriptOutput::signalWaitLoop_() {
	while (true) {
		// retry if an unrelated signal interrupts the wait
		while (sem_wait(&gTriggerSemaphore) == -1 && errno == EINTR) {
		}
		std::lock_guard<std::mutex> lock(*mutex_);
		if (*stop_) {
			return;
		}
		// route each fired signal to the module that listens on it
		for (ModuleSlot &slot : slots_) {
			if ( slot.signalNumber != 0
				&& gSignalFired[static_cast<size_t>(slot.signalNumber)].exchange(false) ) {
				*slot.execute = true;
				slot.executionSignal->notify_one();
			}
		}
	}
}

void PSSspace::ScriptOutput::setDelimiter(const QString &delimiter) {
	if (delimiter_ == delimiter) {
		return;
	}
	delimiter_ = delimiter;
	emit delimiterChanged();
	// Re-join the existing fragments with the new delimiter; no worker restart.
	rebuildCombinedText_();
	emit textChanged();
}

void PSSspace::ScriptOutput::rebuildCombinedText_() {
	QString combined;
	bool first = true;
	for (const ModuleSlot &slot : slots_) {
		if (!first) {
			combined += delimiter_;
		}
		combined += slot.fragment;
		first = false;
	}
	text_ = combined;
}

int PSSspace::ScriptOutput::maxSignalOffset() const {
	return SIGRTMAX - SIGRTMIN;
}

qint64 PSSspace::ScriptOutput::hostPid() const {
	return QCoreApplication::applicationPid();
}

QString PSSspace::ScriptOutput::hostName() const {
	// /proc/self/comm is what `pkill <name>` matches against (kernel comm,
	// truncated to 15 chars); read it directly rather than trusting
	// QCoreApplication::applicationName(), which may differ from comm.
	std::ifstream comm("/proc/self/comm");
	std::string name;
	std::getline(comm, name); // one line, no trailing newline
	return QString::fromStdString(name);
}

void PSSspace::ScriptOutput::setModules(const QVariantList &specs) {
	stopModules_();

	std::vector<ModuleSpec> parsed;
	parsed.reserve( static_cast<size_t>(specs.size()) );
	for (const QVariant &entry : specs) {
		const QVariantMap map = entry.toMap();
		const QString script  = map.value(QStringLiteral("script")).toString();
		if (script.isEmpty()) {
			continue; // an unconfigured script produces no module
		}
		ModuleSpec spec;
		spec.kind = map.value(QStringLiteral("kind")).toInt() == 1
			? ModuleSpec::Kind::Signal
			: ModuleSpec::Kind::Timed;
		spec.script = script.toStdString();
		spec.outputLimit = map.contains(QStringLiteral("outputLimit"))
			? static_cast<size_t>(map.value(QStringLiteral("outputLimit")).toInt())
			: spec.outputLimit;
		if (spec.kind == ModuleSpec::Kind::Timed) {
			spec.interval = std::chrono::duration<uint32_t>{
				static_cast<uint32_t>(map.value(QStringLiteral("interval")).toInt()) };
		} else {
			// the config stores an RTMIN offset; the raw signal is glibc-runtime
			spec.signalNumber = SIGRTMIN + map.value(QStringLiteral("signalOffset")).toInt();
		}
		parsed.push_back( std::move(spec) );
	}

	startModules_( std::move(parsed) );

	// Drop any stale fragment text so the display reflects the new module set
	// immediately; the freshly spawned modules will publish their own output.
	text_.clear();
	emit textChanged();
}

void ShowStdoutPlugin::registerTypes(const char *uri) {
	qmlRegisterType<PSSspace::ScriptOutput>(uri, 1, 0, "ScriptOutput");
}
