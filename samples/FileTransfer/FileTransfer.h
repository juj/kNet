#include <QObject>

#include "kNet.h"

using namespace kNet;

struct Fragment
{
	std::vector<char> data;
	size_t fragmentIndex;
};

class NetworkApp : public QObject, public IMessageHandler, public INetworkServerListener
{
	Q_OBJECT;

private:
	int argc;
	char **argv;

	// Used by the receiver to store partial received data.
	std::map<size_t, Fragment> fragments;

	size_t nextFragment;
	size_t totalFragments;
	size_t bytesReceived;
	std::string filename;
	std::ofstream out;
	tick_t transferStartTick;

	PolledTimer statsPrintTimer;
	static const int printIntervalMSecs = 4000;

	FILE *handle;

	Ptr(MessageConnection) connection;
	int bytesSent;
	int fileSize;
private slots:
	void ReceiverMainLoopIteration();
	void SenderMainLoopIteration();

public:
	Network network;

	NetworkServer *server;

	NetworkApp(int argc_, char **argv_);
	~NetworkApp();

	/// Called to notify the listener that a new connection has been established.
	void NewConnectionEstablished(MessageConnection *connection);

	void HandleMessage(MessageConnection *source, message_id_t id, const char *data, size_t numBytes);
	void WriteFinishedFragments();
	void RunReceiver(unsigned short port, SocketTransportLayer transport);
	void RunSender(const char *address, unsigned short port, SocketTransportLayer transport, const char *filename);
};
