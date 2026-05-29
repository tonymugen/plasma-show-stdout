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
		/** \brief Script runner/Qt object thread interface
		 * 
		 * Translates availability of output data to a Qt signal.
		 */
		void bridgeLoop_();

		/** \brief Text to be displayed */
		QString text_;

		/** \brief Shared `mutex` */
		std::shared_ptr<std::mutex> mutex_;
		/** \brief Script thread termination signal */
		std::shared_ptr<std::condition_variable> stopSignal_;
		/** \brief Termination flag */
		std::shared_ptr<bool> stop_;
		/** \brief CV the worker thread notifies when new output is ready */
		std::condition_variable *signalToMainRaw_{nullptr};
		/** \brief Raw string output from the script */
		std::string *outputRaw_{nullptr};

		/** \brief The script execution thread */
		std::thread workerThread_;
		/** \brief Qt bridge thread */
		std::thread bridgeThread_;
	};

} // namespace PSSspace

class ShowStdoutPlugin final : public QQmlExtensionPlugin {
	// macros that define private declarations
	Q_OBJECT
	Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QQmlExtensionInterface")
public:
	void registerTypes(const char *uri) override;
};
