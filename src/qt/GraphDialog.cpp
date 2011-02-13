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

/** @file GraphDialog.cpp
	@brief */

#include <QUiLoader>
#include <QFile>
#include <QVBoxLayout>
#include <QLabel>
#include <QTreeWidget>
#include <QPainter>
#include <sstream>

#ifdef KNET_USE_BOOST
#include <boost/thread/thread.hpp>
#endif

#include "kNet/DebugMemoryLeakCheck.h"
#include "kNet/Network.h"
#include "kNet/qt/GraphDialog.h"
#include "kNet/qt/ui/ui_Graph.h"

namespace kNet
{

/// Specifies in msecs how often we update MessageConnectionDialog.
const int dialogUpdateInterval = 200;

GraphDialog::GraphDialog(QWidget *parent, const char *hierarchyNodeName_)
:QWidget(parent), hierarchyNodeName(hierarchyNodeName_)
{
	dialog = new Ui_GraphDialog;
	dialog->setupUi(this);

	setWindowTitle(hierarchyNodeName.c_str());
}

GraphDialog::~GraphDialog()
{
	delete dialog;
}

void GraphDialog::Update(StatsEventHierarchyNode &node, int timeMSecs)
{
	QPixmap *image = new QPixmap(dialog->graphLabel->width(), dialog->graphLabel->height());
	{
		QPainter painter(image);
		painter.fillRect(0, 0, image->width(), image->height(), QColor(255, 255, 255, 255));


	}

	dialog->graphLabel->setPixmap(*image);
}

} // ~kNet
