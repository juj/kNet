/* Copyright 2010 Jukka Jylänki

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License. */

/** @file MessageConnectionDialog.cpp
	@brief */

#include <QUiLoader>
#include <QFile>
#include <QVBoxLayout>
#include <QLabel>
#include <QTreeWidget>

#include "kNet/qt/MessageConnectionDialog.h"
#include "kNet/DebugMemoryLeakCheck.h"

namespace kNet
{

const int dialogUpdateInterval = 200;

MessageConnectionDialog::MessageConnectionDialog(QWidget *parent, Ptr(MessageConnection) connection_)
:connection(connection_), QWidget(parent)
{
	QUiLoader loader;
	QFile file("MessageConnectionDialog.ui");
	file.open(QFile::ReadOnly);
	QWidget *myWidget = loader.load(&file, this);
	file.close();
/*
	QVBoxLayout *layout = new QVBoxLayout;
	layout->addWidget(myWidget);
	setLayout(layout);
*/
	updateTimer = new QTimer(this);
	connect(updateTimer, SIGNAL(timeout()), this, SLOT(Update()));
	Update();
}

void MessageConnectionDialog::Update()
{
	if (!connection)
		return;

	QLabel *connectionLine = findChild<QLabel*>("connectionLine");
	if (!connectionLine)
		return;

	connectionLine->setText(connection->ToString().c_str());

	updateTimer->start(dialogUpdateInterval);
}

} // ~kNet
