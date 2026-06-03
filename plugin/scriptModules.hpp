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

/// Modules that run shell scripts
/** \file
 * \author Anthony J. Greenberg
 * \copyright Copyright (c) 2026 Anthony J. Greenberg
 * \version 0.1.0
 *
 * API definitions to manage named shell script execution.
 */

#pragma once

#include <string>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <mutex>
#include <filesystem>

namespace PSSspace {
	/** \brief Run a script once.
	 *
	 * Runs a shell script and return the output.
	 *
	 * \param[in] script path to the script
	 * \return string output
	 */
	[[nodiscard]] std::string runScript(const std::filesystem::path &script);
	/** \brief Truncate a UTF-8 string to a codepoint limit, in place.
	 *
	 * Shortens `text` to at most `maxCodepoints` Unicode codepoints, cutting only
	 * on a codepoint boundary so a multi-byte UTF-8 sequence is never split (which
	 * would otherwise leave an invalid trailing byte that renders as the
	 * replacement character). For ASCII this is identical to a byte truncation.
	 * Leaves `text` unchanged if it already fits. Malformed lead/continuation bytes
	 * are each counted as one codepoint so the function always makes progress and
	 * degrades gracefully. The module layer's output limit is expressed in
	 * characters (codepoints), matching the config UI.
	 *
	 * \param[in,out] text          UTF-8 string to truncate in place
	 * \param[in]     maxCodepoints maximum number of codepoints to keep
	 */
	void truncateUtf8(std::string &text, size_t maxCodepoints);
	/** \brief A module executed on a timer.
	 *
	 * Executes a shell script on a timer.
	 */
	class TimedModule {
	public:
		/** \brief Default constructor */
		TimedModule() = default;
		/** \brief Constructor with data
		 *
		 * \param[in] executionInterval refresh interval
		 * \param[in] stopSignal condition variable the spawning thread notifies to stop the loop
		 * \param[in] mutex mutex shared with the spawning thread; guards `stop`, `outputTarget`, and the stop wait
		 * \param[in] stop flag shared with the spawning thread; set to `true` before notifying `stopSignal` to terminate
		 * \param[in] signalOnChange condition variable to notify the spawning thread that an execution happened
		 * \param[in] script path to the shell script
		 * \param[in] outputLengthLimit limit on output length, in UTF-8 codepoints (characters)
		 * \param[in] outputTarget pointer to the target string
		 */
		TimedModule(
			const std::chrono::duration<uint32_t> &executionInterval,
			std::shared_ptr<std::condition_variable> stopSignal,
			std::shared_ptr<std::mutex> mutex,
			std::shared_ptr<bool> stop,
			std::unique_ptr<std::condition_variable> signalOnChange,
			const std::filesystem::path &script,
			const size_t &outputLengthLimit,
			std::unique_ptr<std::string> outputTarget
		) : refreshInterval_{executionInterval},
			stopSignal_{std::move(stopSignal)},
			mutex_{std::move(mutex)},
			stop_{std::move(stop)},
			signalToMain_{std::move(signalOnChange)},
			script_{script},
			outputLengthLimit_{outputLengthLimit},
			outputString_{std::move(outputTarget)} {};
		/** \brief Move constructor
		 *
		 * \param[in] toMove object to move
		 */
		TimedModule(TimedModule &&toMove) noexcept = default;
		/** \brief Move assignment operator
		 *
		 * \param[in] toMove object to move
		 * \return `TimedModule` object
		 */
		TimedModule& operator=(TimedModule &&toMove) noexcept = default;
		/** \brief Destructor */
		~TimedModule() = default;
		/** Run the module
		 *
		 * Runs the module, refreshing at the specified interval.
		 */
		void operator()() const;
	private:
		/** \brief Refresh interval in seconds */
		std::chrono::duration<uint32_t> refreshInterval_;
		/** \brief Condition variable to receive stop signal from spawning thread */
		std::shared_ptr<std::condition_variable> stopSignal_;
		/** \brief Mutex shared with spawning thread */
		std::shared_ptr<std::mutex> mutex_;
		/** \brief Stop flag shared with spawning thread */
		std::shared_ptr<bool> stop_;
		/** \brief Condition variable pointer to signal state change to spawning thread */
		std::unique_ptr<std::condition_variable> signalToMain_;
		/** \brief Path to the script */
		std::filesystem::path script_;
		/** \brief Output length limit, in UTF-8 codepoints (characters) */
		size_t outputLengthLimit_ = 0;
		/** \brief Pointer to the output string */
		std::unique_ptr<std::string> outputString_;
	};
	/** \brief A module executed on a signal.
	 *
	 * Executes a shell script on receiving a signal from the main thread.
	 */
	class SignalModule {
	public:
		/** \brief Default constructor */
		SignalModule() = default;
		/** \brief Constructor with data
		 *
		 * \param[in] signalToExecute condition variable the spawning thread notifies to trigger execution or stop
		 * \param[in] mutex mutex shared with the spawning thread; guards `execute`, `stop`, and `outputTarget`
		 * \param[in] execute flag shared with the spawning thread; set to `true` before notifying `signalToExecute` to trigger a run
		 * \param[in] stop flag shared with the spawning thread; set to `true` before notifying `signalToExecute` to terminate
		 * \param[in] signalOnChange condition variable to notify the spawning thread that an execution happened
		 * \param[in] script path to the shell script
		 * \param[in] outputLengthLimit limit on output length, in UTF-8 codepoints (characters)
		 * \param[in] outputTarget pointer to the target string
		 */
		SignalModule(
			std::unique_ptr<std::condition_variable> signalToExecute,
			std::shared_ptr<std::mutex> mutex,
			std::shared_ptr<bool> execute,
			std::shared_ptr<bool> stop,
			std::unique_ptr<std::condition_variable> signalOnChange,
			const std::filesystem::path &script,
			const size_t &outputLengthLimit,
			std::unique_ptr<std::string> &outputTarget
		) : executionSignal_{std::move(signalToExecute)},
			mutex_{std::move(mutex)},
			execute_{std::move(execute)},
			stop_{std::move(stop)},
			signalToMain_{std::move(signalOnChange)},
			script_{script},
			outputLengthLimit_{outputLengthLimit},
			outputString_{std::move(outputTarget)} {};
		/** \brief Move constructor
		 *
		 * \param[in] toMove object to move
		 */
		SignalModule(SignalModule &&toMove) noexcept = default;
		/** \brief Move assignment operator
		 *
		 * \param[in] toMove object to move
		 * \return `SignalModule` object
		 */
		SignalModule& operator=(SignalModule &&toMove) noexcept = default;
		/** \brief Destructor */
		~SignalModule() = default;
		/** Run the module
		 *
		 * Runs the module, refreshing at the specified interval.
		 */
		void operator()() const;
	private:
		/** \brief Condition variable pointer that triggers execution or stop */
		std::unique_ptr<std::condition_variable> executionSignal_;
		/** \brief Mutex shared with spawning thread */
		std::shared_ptr<std::mutex> mutex_;
		/** \brief Execution flag shared with spawning thread */
		std::shared_ptr<bool> execute_;
		/** \brief Stop flag shared with spawning thread */
		std::shared_ptr<bool> stop_;
		/** \brief Condition variable pointer to signal state change to spawning thread */
		std::unique_ptr<std::condition_variable> signalToMain_;
		/** \brief Path to the script */
		std::filesystem::path script_;
		/** \brief Output length limit, in UTF-8 codepoints (characters) */
		size_t outputLengthLimit_ = 0;
		/** \brief Pointer to the output string */
		std::unique_ptr<std::string> outputString_;
	};
}
