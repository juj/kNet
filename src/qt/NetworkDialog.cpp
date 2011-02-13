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

/** @file NetworkDialog.cpp
	@brief */

#include <QFile>
#include <QVBoxLayout>
#include <QLabel>
#include <QTreeWidget>

#ifdef KNET_USE_BOOST
#include <boost/thread/thread.hpp>
#endif

#include "kNet/DebugMemoryLeakCheck.h"
#include "kNet/StatsEventHierarchy.h"
#include "kNet/qt/NetworkDialog.h"
#include "kNet/qt/MessageConnectionDialog.h"
#include "kNet/qt/ui/ui_NetworkDialog.h"

namespace kNet
{

/// Specifies in msecs how often we update NetworkDialog.
const int dialogUpdateInterval = 1000;

NetworkDialog::NetworkDialog(QWidget *parent, Network *network_)
:network(network_), QWidget(parent)
{
	dialog = new Ui_NetworkDialog;
	dialog->setupUi(this);

	QTreeWidget *connectionsTree = findChild<QTreeWidget*>("connectionsTree");
	if (connectionsTree)
		connect(connectionsTree, SIGNAL(itemDoubleClicked(QTreeWidgetItem *, int)), this, SLOT(ItemDoubleClicked(QTreeWidgetItem *)));

	Update();
}

NetworkDialog::~NetworkDialog()
{
	delete dialog;
}

class MessageConnectionTreeItem : public QTreeWidgetItem
{
public:
	MessageConnectionTreeItem(QTreeWidgetItem *parent, Ptr(MessageConnection) connection_)
	:QTreeWidgetItem(parent), connection(connection_)
	{
		if (connection)
			setText(0, connection->ToString().c_str());
	}

	Ptr(MessageConnection) connection;
};

QTreeWidgetItem *NewTreeItemFromString(QTreeWidget *parent, const char *str)
{
	QTreeWidgetItem *item = new QTreeWidgetItem(parent);
	item->setText(0, str);
	return item;
}

void NetworkDialog::Update()
{
	if (!network)
		return;

	QLabel *machineIp = findChild<QLabel*>("machineIP");
	if (!machineIp)
		return;
	QLabel *numRunningThreads = findChild<QLabel*>("numRunningThreads");
	if (!numRunningThreads)
		return;
	QTreeWidget *connectionsTree = findChild<QTreeWidget*>("connectionsTree");
	if (!connectionsTree)
		return;

	machineIp->setText(network->LocalAddress());
	numRunningThreads->setText(QString::number(network->NumWorkerThreads()));

	connectionsTree->clear();

	Ptr(NetworkServer) server = network->GetServer();
	if (server)
	{
		QTreeWidgetItem *serverItem = NewTreeItemFromString(connectionsTree, server->ToString().c_str());
		connectionsTree->addTopLevelItem(serverItem);

		NetworkServer::ConnectionMap connections = server->GetConnections();
		for(NetworkServer::ConnectionMap::iterator iter = connections.begin(); iter != connections.end(); ++iter)
		{
			QTreeWidgetItem *connectionItem = new MessageConnectionTreeItem(serverItem, iter->second);
			serverItem->addChild(connectionItem);
			serverItem->setExpanded(true);
		}
	}

	std::set<MessageConnection*> connections = network->Connections();
	for(std::set<MessageConnection*>::iterator iter = connections.begin(); iter != connections.end(); ++iter)
		if ((*iter)->GetSocket() && (*iter)->GetSocket()->Type() == ClientSocket)
			new MessageConnectionTreeItem(connectionsTree->invisibleRootItem(), *iter);

	PopulateStatsTree();

	QTimer::singleShot(dialogUpdateInterval, this, SLOT(Update()));
}

QTreeWidgetItem *FindChild(QTreeWidgetItem *parent, int column, QString text)
{
	for(int i = 0; i < parent->childCount(); ++i)
	{
		QTreeWidgetItem *child = parent->child(i);
		if (child->text(column) == text)
			return child;
	}
	return 0;
}

void PopulateStatsTreeNode(QTreeWidgetItem *parent, StatsEventHierarchyNode &statsNode, int timeMSecs)
{
	for(StatsEventHierarchyNode::NodeMap::iterator iter = statsNode.children.begin(); iter != statsNode.children.end();
		++iter)
	{
		QTreeWidgetItem *child = FindChild(parent, 0, iter->first.c_str());
		if (!child)
		{
			child = new QTreeWidgetItem(parent);
			child->setText(0, iter->first.c_str());
		}
		StatsEventHierarchyNode &node = iter->second;
		bool hasChildren = node.children.size() > 0;

		int totalCountThisLevel = hasChildren ? node.AccumulateTotalCountThisLevel() : 0;
		int totalCountHierarchy = node.AccumulateTotalCountHierarchy();
		float totalValueThisLevel = hasChildren ? node.AccumulateTotalValueThisLevel() : 0.f;
		float totalValueHierarchy = node.AccumulateTotalValueHierarchy();
		float timeSpan = timeMSecs / 1000.f;

		QString totalCountHierarchyStr = (totalCountHierarchy == 0) ? "-" : QString::number(totalCountHierarchy);
		QString totalValueHierarchyStr = (totalCountHierarchy == 0) ? "-" : QString::number(totalValueHierarchy);

		QString totalCountHierarchyPerTimeStr = (totalCountHierarchy == 0) ? "-" : QString::number(totalCountHierarchy / timeSpan, 'g', 2);
		QString totalValueHierarchyPerTimeStr = (totalCountHierarchy == 0) ? "-" : QString::number(totalValueHierarchy / timeSpan, 'g', 2);

		QString totalValueHierarchyPerCountStr = (totalCountHierarchy == 0) ? "-" : QString::number((float)totalValueHierarchy / totalCountHierarchy, 'g', 2);

		if (hasChildren && totalCountThisLevel > 0)
		{
			QString totalCountThisLevelStr = (totalCountThisLevel == 0) ? "-" : QString::number(totalCountThisLevel);
			QString totalValueThisLevelStr = (totalCountThisLevel == 0) ? "-" : QString::number(totalValueThisLevel);

			QString totalCountThisLevelPerTimeStr = (totalCountThisLevel == 0) ? "-" : QString::number(totalCountThisLevel / timeSpan, 'g', 2);
			QString totalValueThisLevelPerTimeStr = (totalCountThisLevel == 0) ? "-" : QString::number(totalValueThisLevel / timeSpan, 'g', 2);

			QString totalValueThisLevelPerCountStr = (totalCountThisLevel == 0) ? "-" : QString::number((float)totalValueThisLevel / totalCountThisLevel, 'g', 2);

			child->setText(1, QString("%1 (%2)").arg(totalCountHierarchyStr).arg(totalCountThisLevelStr));
			child->setText(2, QString("%1 (%2)").arg(totalValueHierarchyStr).arg(totalValueThisLevelStr));
			child->setText(3, QString("%1 (%2)").arg(totalCountHierarchyPerTimeStr).arg(totalCountThisLevelPerTimeStr));
			child->setText(4, QString("%1 (%2)").arg(totalValueHierarchyPerTimeStr).arg(totalValueThisLevelPerTimeStr));
			child->setText(5, QString("%1 (%2)").arg(totalValueHierarchyPerCountStr).arg(totalValueThisLevelPerCountStr));
		}
		else
		{
			child->setText(1, QString("%1").arg(totalCountHierarchyStr));
			child->setText(2, QString("%1").arg(totalValueHierarchyStr));
			child->setText(3, QString("%1").arg(totalCountHierarchyPerTimeStr));
			child->setText(4, QString("%1").arg(totalValueHierarchyPerTimeStr));
			child->setText(5, QString("%1").arg(totalValueHierarchyPerCountStr));
		}
		child->setText(6, QString("%1").arg(node.LatestValue()));

		PopulateStatsTreeNode(child, node, timeMSecs);
	}
}

void NetworkDialog::PopulateStatsTree()
{
	// Make a deep copy of all profiling data so that we don't stall the network worker threads with a lock.
	StatsEventHierarchyNode statistics;

	int timeMSecs = 60 * 1000;
	if (dialog->comboTimePeriod->currentIndex() == 0) timeMSecs = 5 * 1000;
	else if (dialog->comboTimePeriod->currentIndex() == 1) timeMSecs = 15 * 1000;
	else if (dialog->comboTimePeriod->currentIndex() == 2) timeMSecs = 30 * 1000;

	{
		Lock<StatsEventHierarchyNode> stats = network->Statistics();
		stats->PruneOldEventsHierarchy(timeMSecs);
		statistics = *stats;
	}

	PopulateStatsTreeNode(dialog->treeStats->invisibleRootItem(), statistics, timeMSecs);
	for(int i = 0; i < 7; ++i)
		dialog->treeStats->resizeColumnToContents(i);
}

void NetworkDialog::ItemDoubleClicked(QTreeWidgetItem *item)
{
	MessageConnectionTreeItem *msgItem = dynamic_cast<MessageConnectionTreeItem*>(item);
	if (!msgItem)
		return;

	MessageConnectionDialog *dialog = new MessageConnectionDialog(0, msgItem->connection);
	dialog->show();
	dialog->setAttribute(Qt::WA_DeleteOnClose);
}

} // ~kNet
