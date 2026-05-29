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
#include <QDir>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <utility>

#include "plasma-show-stdout.hpp"
#include "scriptModules.hpp"

namespace {
	constexpr std::chrono::duration<uint32_t> kRefreshInterval{60};
	constexpr size_t                          kOutputLimit{300};
}

PSSspace::ScriptOutput::ScriptOutput(QObject *parent)
	: QObject(parent),
	  mutex_( std::make_shared<std::mutex>() ),
	  stopSignal_( std::make_shared<std::condition_variable>() ),
	  stop_( std::make_shared<bool>(false) ) {
	auto signalToMain = std::make_unique<std::condition_variable>();
	signalToMainRaw_  = signalToMain.get();

	auto outputString = std::make_unique<std::string>();
	outputRaw_        = outputString.get();

	const std::filesystem::path scriptPath {
		(QDir::homePath() + "/.scripts/disk").toStdString()
	};

	PSSspace::TimedModule mod(
		kRefreshInterval,
		stopSignal_, mutex_, stop_,
		std::move(signalToMain),
		scriptPath,
		kOutputLimit,
		std::move(outputString)
	);

	workerThread_ = std::thread( std::move(mod) );
	bridgeThread_ = std::thread(&ScriptOutput::bridgeLoop_, this);
}

PSSspace::ScriptOutput::~ScriptOutput() {
	{
		std::lock_guard<std::mutex> lock(*mutex_);
		*stop_ = true;
	}
	if (signalToMainRaw_ != nullptr) {
		signalToMainRaw_->notify_all();
	}
	stopSignal_->notify_all();

	// bridge must exit before the worker tears down the
	// condition variable / output string it references
	if ( bridgeThread_.joinable() ) {
		bridgeThread_.join();
	}
	if ( workerThread_.joinable() ) {
		workerThread_.join();
	}
}

void PSSspace::ScriptOutput::bridgeLoop_() {
	while (true) {
		std::string snapshot;
		{
			std::unique_lock<std::mutex> lock(*mutex_);
			signalToMainRaw_->wait(lock, [this]{
				return *stop_ || !outputRaw_->empty();
			});
			if (*stop_) {
				return;
			}
			snapshot = *outputRaw_;
			outputRaw_->clear();
		}
		QString newText = QString::fromStdString(snapshot);
		QMetaObject::invokeMethod(this, [this, newText] {
			if (text_ == newText) {
				return;
			}
			text_ = newText;
			emit textChanged();
		}, Qt::QueuedConnection);
	}
}

void ShowStdoutPlugin::registerTypes(const char *uri) {
	qmlRegisterType<PSSspace::ScriptOutput>(uri, 1, 0, "ScriptOutput");
}
