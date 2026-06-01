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
#include <QQmlExtensionPlugin>

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace PSSspace {
	class ScriptOutput : public QObject {
		// macros that define private declarations
		Q_OBJECT
		Q_PROPERTY(QString text READ text NOTIFY textChanged)
	public:
		/** \brief Constructor 
		 *
		 * \param[in] parent Pointer to the parent object
		 */
		explicit ScriptOutput(QObject *parent = nullptr);
		/** \brief Destructor */
		~ScriptOutput() override;
		/** \brief Text output 
		 *
		 * \return A `QString` object for display
		 */
		QString text() const { return text_; }
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
	};

} // namespace PSSspace

class ShowStdoutPlugin final : public QQmlExtensionPlugin {
	// macros that define private declarations
	Q_OBJECT
	Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QQmlExtensionInterface")
public:
	void registerTypes(const char *uri) override;
};
