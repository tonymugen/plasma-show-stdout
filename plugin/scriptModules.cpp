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
#include <functional>
#include <mutex>
#include <utility>

#include "scriptModules.hpp"

using namespace PSSspace;

namespace {
	// Built-in English fallback, used when no Qt/KI18n translator is installed
	// (notably by the Qt-free unit tests). The message ids match those the bridge
	// translator recognizes; the wording here is the msgid the bridge passes to
	// i18n(), so the two stay in sync. An unknown id returns the bare argument.
	std::string defaultTranslate(const std::string &messageId, const std::string &argument) {
		if (messageId == "doesNotExist") {
			return argument + " does not exist";
		}
		if (messageId == "failedToExecute") {
			return "Failed to execute " + argument;
		}
		return argument;
	}

	std::mutex gTranslatorMutex;
	PSSspace::MessageTranslator gTranslator{ defaultTranslate };
}

void PSSspace::setMessageTranslator(MessageTranslator translator) {
	if (!translator) {
		return;
	}
	std::lock_guard<std::mutex> lock(gTranslatorMutex);
	gTranslator = std::move(translator);
}

std::string PSSspace::translateMessage(const std::string &messageId, const std::string &argument) {
	// Copy the translator under the lock, then invoke it outside the lock so a
	// worker thread translating cannot deadlock against the bridge installing one.
	MessageTranslator current;
	{
		std::lock_guard<std::mutex> lock(gTranslatorMutex);
		current = gTranslator;
	}
	return current(messageId, argument);
}

std::string PSSspace::runScript(const std::filesystem::path &script) {
	// errors will be reported via output
	// so the user can see if something went wrong
	if ( !std::filesystem::exists(script) ) {
		return translateMessage("doesNotExist", script.string());
	}

	constexpr size_t bufferSize = 100;
	char buffer[bufferSize];
	std::string output;
	FILE *pipe = popen(script.c_str(), "r");
	if (!pipe) {
		return translateMessage("failedToExecute", script.string());
	}
	while ( !feof(pipe) ) {
		if (fgets(buffer, bufferSize, pipe) != NULL) {
			output += buffer;
		}
	}
	pclose(pipe);
	return output;
}

void PSSspace::truncateUtf8(std::string &text, size_t maxCodepoints) {
	// Fast path: a codepoint is at least one byte, so byte count <= limit implies
	// codepoint count <= limit and no truncation is needed.
	if (text.size() <= maxCodepoints) {
		return;
	}
	size_t byteIndex  = 0;
	size_t codepoints = 0;
	const size_t size = text.size();
	while (byteIndex < size && codepoints < maxCodepoints) {
		const auto lead = static_cast<unsigned char>(text[byteIndex]);
		size_t sequenceLength = 1;
		if ( (lead & 0x80U) == 0x00U ) {        // 0xxxxxxx: ASCII
			sequenceLength = 1;
		} else if ( (lead & 0xE0U) == 0xC0U ) { // 110xxxxx: 2-byte
			sequenceLength = 2;
		} else if ( (lead & 0xF0U) == 0xE0U ) { // 1110xxxx: 3-byte
			sequenceLength = 3;
		} else if ( (lead & 0xF8U) == 0xF0U ) { // 11110xxx: 4-byte
			sequenceLength = 4;
		} else {                              // stray continuation/invalid byte
			sequenceLength = 1;
		}
		byteIndex += sequenceLength;
		++codepoints;
	}
	// byteIndex now sits on a codepoint boundary; only erase if it lies within the
	// string (a final sequence that overruns the end is malformed input we keep).
	if (byteIndex < size) {
		text.erase( text.begin() + static_cast<std::string::difference_type>(byteIndex), text.end() );
	}
}

void TimedModule::operator()() const {
	while (true) {
		std::string rawOutput = runScript(script_);
		truncateUtf8(rawOutput, outputLengthLimit_);
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
		truncateUtf8(rawOutput, outputLengthLimit_);
		lock.lock();
		*outputString_ = std::move(rawOutput);
		signalToMain_->notify_one();
	}
}
