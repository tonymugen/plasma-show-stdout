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
 * Implementation of methods to manage named shell script execution.
 */

#include <cstdio>
#include <string>
#include <filesystem>

#include "scriptModules.hpp"

using namespace PSSspace;

std::string PSSspace::runScript(const std::filesystem::path &script) {
	// errors will be reported via output
	// so the user can see if something went wrong
	if ( !std::filesystem::exists(script) ) {
		return script.string() + " does not exist";
	}

	constexpr size_t bufferSize = 100;
	char buffer[bufferSize];
	std::string output;
	FILE *pipe = popen(script.c_str(), "r");
	if (!pipe) {
		return "Failed to execute " + script.string();
	}
	while ( !feof(pipe) ) {
		if (fgets(buffer, bufferSize, pipe) != NULL) {
			output += buffer;
		}
	}
	pclose(pipe);
	return output;
}

void TimedModule::operator()() const {
	while (true) {
		std::string rawOutput = runScript(script_);
		if (rawOutput.size() > outputLengthLimit_) {
			rawOutput.erase( rawOutput.begin() + outputLengthLimit_, rawOutput.end() );
		}
		std::unique_lock<std::mutex> lock(*mutex_);
		*outputString_ = std::move(rawOutput);
		signalToMain_->notify_one();
		if ( stopSignal_->wait_for(lock, refreshInterval_, [this]{ return *stop_; }) ) {
			return;
		}
	}
}

void SignalModule::operator()() const {
	while (true) {
		std::unique_lock<std::mutex> lock(*mutex_);
		executionSignal_->wait(lock, [this]{ return *execute_ || *stop_; });
		if (*stop_) return;
		*execute_ = false;
		lock.unlock();
		std::string rawOutput = runScript(script_);
		if (rawOutput.size() > outputLengthLimit_) {
			rawOutput.erase( rawOutput.begin() + outputLengthLimit_, rawOutput.end() );
		}
		lock.lock();
		*outputString_ = std::move(rawOutput);
		signalToMain_->notify_one();
	}
}
