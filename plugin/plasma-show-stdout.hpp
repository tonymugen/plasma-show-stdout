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
 * API definitions to display script output in PLasma.
 */

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QQmlExtensionPlugin>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace PSSspace {
	/** \brief Inert description of one module
	 *
	 * Plain configuration data describing a single script module: which script
	 * to run and how it is triggered. A `TimedModule` uses `interval`; a
	 * `SignalModule` uses `signalNumber`. This is the unit a future config panel
	 * will produce and serialize; `ScriptOutput` turns a list of these into live
	 * worker threads.
	 */
	struct ModuleSpec {
		/** \brief How the module is triggered */
		enum class Kind { Timed, Signal };
		/** \brief Trigger kind */
		Kind                            kind;
		/** \brief Path to the script to run */
		std::filesystem::path           script;
		/** \brief Polling interval (Timed modules only) */
		std::chrono::duration<uint32_t> interval{};
		/** \brief Realtime signal to listen on, e.g. SIGRTMIN+2 (Signal modules only) */
		int                             signalNumber{0};
		/** \brief Maximum number of output characters to retain */
		size_t                          outputLimit{300};
	};

	class ScriptOutput : public QObject {
		// macros that define private declarations
		Q_OBJECT
		Q_PROPERTY(QString text READ text NOTIFY textChanged)
		Q_PROPERTY(int maxSignalOffset READ maxSignalOffset CONSTANT)
	public:
		/** \brief QML constructor
		 *
		 * The entry point used by QML, which can only invoke a
		 * `(QObject *parent)` constructor. Starts with an empty module list
		 * (`defaultSpecs()`); the QML config wiring then populates it at runtime
		 * via `setModules`. Delegates to the spec-taking constructor.
		 *
		 * \param[in] parent Pointer to the parent object
		 */
		explicit ScriptOutput(QObject *parent = nullptr);
		/** \brief Constructor from a list of module specifications
		 *
		 * Spawns one worker (and bridge) thread per spec, in the given order;
		 * their outputs are joined into the `text` property. This is the real
		 * constructor; the QML constructor delegates to it.
		 *
		 * \param[in] specs  modules to run, in display order
		 * \param[in] parent Pointer to the parent object
		 */
		explicit ScriptOutput(std::vector<ModuleSpec> specs, QObject *parent = nullptr);
		/** \brief Destructor */
		~ScriptOutput() override;
		/** \brief Text output
		 *
		 * \return A `QString` object for display
		 */
		QString text() const { return text_; }
		/** \brief Largest valid realtime-signal offset
		 *
		 * The inclusive upper bound for a signal module's RTMIN offset, i.e.
		 * `SIGRTMAX - SIGRTMIN`. Exposed so the config UI can size its spinbox
		 * without hard-coding glibc-specific signal numbers (`SIGRTMIN`/
		 * `SIGRTMAX` are runtime calls, not constants).
		 *
		 * \return the maximum offset N usable as `SIGRTMIN + N`
		 */
		int maxSignalOffset() const;
		/** \brief (Re)configure the running modules from QML
		 *
		 * Tears down any currently running modules and starts the ones described
		 * by `specs`. Each entry is a map with keys `kind` (0 = Timed,
		 * 1 = Signal), `script` (path string), `interval` (seconds, Timed only),
		 * `signalOffset` (RTMIN offset, Signal only) and optional `outputLimit`.
		 * Entries with an empty `script` are skipped. This is the runtime entry
		 * point the QML config wiring calls; the signal number passed to a
		 * `SignalModule` is computed here as `SIGRTMIN + signalOffset`.
		 *
		 * \param[in] specs module descriptions, in display order
		 */
		Q_INVOKABLE void setModules(const QVariantList &specs);
	signals:
		/** \brief Change-notification signal for the text property */
		void textChanged();
	private:
		/** \brief Per-module worker/bridge state and output bookkeeping
		 *
		 * One slot per configured module, held in `slots_` in display order.
		 * The observer pointers reference objects owned by the worker thread
		 * (or, for `executionSignal`, by the `SignalModule`), which outlive the
		 * slot's observers because the destructor joins observers first.
		 */
		struct ModuleSlot {
			/** \brief CV the worker notifies when new output is ready (observer) */
			std::condition_variable *changeSignal{nullptr};
			/** \brief Worker-owned output buffer (observer) */
			std::string *outputBuffer{nullptr};
			/** \brief Latest output produced by this module */
			QString fragment;
			/** \brief Script execution thread */
			std::thread worker;
			/** \brief Qt bridge thread */
			std::thread bridge;
			/** \brief Per-module execution-trigger flag (signal modules only) */
			std::shared_ptr<bool> execute;
			/** \brief CV used to trigger a run (signal modules only, observer) */
			std::condition_variable *executionSignal{nullptr};
			/** \brief Realtime signal this module listens on (0 for timed modules) */
			int signalNumber{0};
		};

		/** \brief Build and spawn the modules described by `specs`
		 *
		 * The two-pass spawn path shared by the spec-taking constructor and
		 * `setModules`: pass 1 builds every `ModuleSlot` (so the bridges can
		 * capture stable `&slot` pointers), then signal handlers are installed
		 * for the distinct signal numbers in use, then pass 2 spawns the worker
		 * and bridge threads (and the signal-wait thread if any signal module is
		 * present). Global signal state (`sem_init`, handler install, the
		 * wait thread) is touched only when at least one signal module exists.
		 *
		 * \param[in] specs modules to run, in display order
		 */
		void startModules_(std::vector<ModuleSpec> specs);
		/** \brief Stop and join all running modules, resetting for reuse
		 *
		 * Sets the stop flag, wakes every waiter, joins the bridges and the
		 * signal-wait loop before the workers they observe, then (only if this
		 * instance installed them) restores `SIG_IGN` and destroys the trigger
		 * semaphore. Finally clears `slots_` so the instance can be restarted by
		 * a subsequent `startModules_`. Safe to call when nothing is running.
		 */
		void stopModules_();
		/** \brief Script output to Qt bridge
		 *
		 * Waits on the slot's change CV, snapshots and clears its output buffer,
		 * then posts an update to the GUI thread that refreshes the slot's
		 * fragment and rebuilds the combined `text_`. One instance runs per slot.
		 *
		 * \param[in] slot the module slot this bridge serves
		 */
		void bridgeLoop_(ModuleSlot *slot);
		/** \brief Realtime-signal wait loop
		 *
		 * Drains the trigger semaphore (posted by the async-signal-safe handler),
		 * then triggers each signal module whose flag fired. Exits when `stop_`
		 * is set. A single instance serves all signal modules.
		 */
		void signalWaitLoop_();
		/** \brief Rebuild the combined display text
		 *
		 * Joins every slot's fragment in order with `" | "`. Runs on the GUI
		 * thread (from the bridge's queued update).
		 */
		void rebuildCombinedText_();

		/** \brief Combined text to be displayed (all module fragments joined) */
		QString text_;

		/** \brief Shared `mutex` */
		std::shared_ptr<std::mutex> mutex_;
		/** \brief Timed-module termination signal */
		std::shared_ptr<std::condition_variable> stopSignal_;
		/** \brief Termination flag shared by all modules */
		std::shared_ptr<bool> stop_;

		/** \brief Per-module state, in display order
		 *
		 * A `deque` (not `vector`) so that pointers into a slot stay valid across
		 * `push_back`: the bridges capture `&slot` fields, and `deque` keeps
		 * references to existing elements valid when new ones are appended.
		 */
		std::deque<ModuleSlot> slots_;
		/** \brief Realtime-signal wait thread (serves all signal modules) */
		std::thread signalWaitThread_;
		/** \brief Whether this instance installed signal handlers / `sem_init`
		 *
		 * Set by `startModules_` when at least one signal module is present, so
		 * `stopModules_` only undoes the global signal state this instance set
		 * up. Lets a module-less instance (e.g. the config UI's probe used to
		 * read `maxSignalOffset`) coexist with the running widget without
		 * touching the shared trigger semaphore.
		 */
		bool signalsActive_{false};
	};

} // namespace PSSspace

class ShowStdoutPlugin final : public QQmlExtensionPlugin {
	// macros that define private declarations
	Q_OBJECT
	Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QQmlExtensionInterface")
public:
	void registerTypes(const char *uri) override;
};
