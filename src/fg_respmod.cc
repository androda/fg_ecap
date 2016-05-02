/*
	Copyright Jacob Carter 2015 - 2016
	Based on the eCAP Adapter Sample found here: http://e-cap.org/Downloads
*/
#include <stdexcept>
#include <exception>
#include <string.h>
#include <string>
#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <fstream>
#include <iostream>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <thread>
#include <chrono>

#include <libecap/common/registry.h>
#include <libecap/common/errors.h>
#include <libecap/common/message.h>
#include <libecap/common/header.h>
#include <libecap/common/names.h>
#include <libecap/common/named_values.h>
#include <libecap/host/host.h>
#include <libecap/adapter/service.h>
#include <libecap/adapter/xaction.h>
#include <libecap/host/xaction.h>

namespace Adapter {

using libecap::size_type;

class Service: public libecap::adapter::Service {
	public:
		// About
		virtual std::string uri() const; // unique across all vendors
		virtual std::string tag() const; // changes with version and config
		virtual void describe(std::ostream &os) const; // free-format info

		// Configuration
		virtual void configure(const libecap::Options &cfg);
		virtual void reconfigure(const libecap::Options &cfg);
		void setOne(const libecap::Name &name, const libecap::Area &valArea);

		// Lifecycle
		virtual void start(); // expect makeXaction() calls
		virtual void stop(); // no more makeXaction() calls until start()
		virtual void retire(); // no more makeXaction() calls

		// Scope (XXX: this may be changed to look at the whole header)
		virtual bool wantsUrl(const char *url) const;

		// Work
		virtual MadeXactionPointer makeXaction(libecap::host::Xaction *hostx);

		std::string ecapguardian_listen_socket;

	protected:
		void set_listen_socket(const std::string &value);
};


// Calls Service::setOne() for each host-provided configuration option.
// See Service::configure().
class Cfgtor: public libecap::NamedValueVisitor {
	public:
		Cfgtor(Service &aSvc): svc(aSvc) {}
		virtual void visit(const libecap::Name &name, const libecap::Area &value) {
			svc.setOne(name, value);
		}
		Service &svc;
};


class Xaction: public libecap::adapter::Xaction {
	public:
		Xaction(libecap::shared_ptr<Service> s, libecap::host::Xaction *x);
		virtual ~Xaction();

		// meta-information for the host transaction
		virtual const libecap::Area option(const libecap::Name &name) const;
		virtual void visitEachOption(libecap::NamedValueVisitor &visitor) const;

		// lifecycle
		virtual void start();
		virtual void stop();

		// adapted body transmission control
		virtual void abDiscard();
		virtual void abMake();
		virtual void abMakeMore();
		virtual void abStopMaking();

		// adapted body content extraction and consumption
		virtual libecap::Area abContent(size_type offset, size_type size);
		virtual void abContentShift(size_type size);

		// virgin body state notification
		virtual void noteVbContentDone(bool atEnd);
		virtual void noteVbContentAvailable();

	protected:
		void adaptContent(std::string &chunk) const; // converts vb to ab
		void stopVb(); // stops receiving vb (if we are receiving it)
		libecap::host::Xaction *lastHostCall(); // clears hostx

		void checkWritten(ssize_t sent, size_type expectedSent, std::string name);

	private:
		size_type readTo = 0;
		libecap::shared_ptr<libecap::Message> sharedPointerToVirginHeaders;
		std::string buffer; // for content adaptation
		std::string previousChunk;
		std::ofstream logFile;

		libecap::shared_ptr<const Service> service; // configuration access
		libecap::host::Xaction *hostx; // Host transaction rep

		int socketHandle;  // the ecapguardian eCAP listener

		typedef enum { opUndecided, opOn, opComplete, opNever } OperationState;
		OperationState receivingVb;
		OperationState sendingAb;

		////  Flags and such for communication with server
                const int BUF_SIZE = 1024;
                const char FLAG_USE_VIRGIN = 'v';
                const char FLAG_MODIFY = 'm';
                const char FLAG_NEEDS_SCAN = 's';
		const char FLAG_BLOCK = 'b';
                const char FLAG_MSG_RECVD = 'r'; // used to signal header/body received to the server
                const std::string FLAG_END = "\n\n\0\0"; //used for headers/body end
                const std::string FLAG_END_REMOVE = "\0\0"; //Remove this from the end of the headers/body
};

static const std::string PACKAGE_NAME = "FilterGizmo RESPMOD ecapguardian";

static const std::string PACKAGE_VERSION = "0.1.0";

static const std::string CfgErrorPrefix =
	"FilterGizmo RESPMOD Adapter: configuration error: ";

static const std::string RunErrorPrefix = "FilterGizmo RESPMOD Adapter: Runtime Error: ";

} // namespace Adapter

std::string Adapter::Service::uri() const {
	return "ecap://filtergizmo.com/ecapguardian/respmod";
}

std::string Adapter::Service::tag() const {
	return PACKAGE_VERSION;
}

void Adapter::Service::describe(std::ostream &os) const {
	os << "A modifying adapter from " << PACKAGE_NAME << " v" << PACKAGE_VERSION;
}

void Adapter::Service::configure(const libecap::Options &cfg) {
	Cfgtor cfgtor(*this);
	cfg.visitEachOption(cfgtor);

	// check for post-configuration errors and inconsistencies
}

void Adapter::Service::reconfigure(const libecap::Options &cfg) {
	ecapguardian_listen_socket.clear();
	configure(cfg);
}

void Adapter::Service::setOne(const libecap::Name &name, const libecap::Area &valArea) {
	const std::string value = valArea.toString();
	if (name == "ecapguardian_listen_socket"){
		set_listen_socket(value);
	} else if (name.assignedHostId()) {
		; // skip options that don't matter
	} else{
		throw libecap::TextException(CfgErrorPrefix +
			"unsupported configuration parameter: " + name.image());
	}
}

void Adapter::Service::set_listen_socket(const std::string &value) {
	if (value.empty()) {
		throw libecap::TextException(CfgErrorPrefix +
			"empty ecapguardian_listen_socket value is not allowed");
	}
	ecapguardian_listen_socket += value;
}

void Adapter::Service::start() {
	libecap::adapter::Service::start();
	// custom code would go here, but this service does not have one
}

void Adapter::Service::stop() {
	// custom code would go here, but this service does not have one
	libecap::adapter::Service::stop();
}

void Adapter::Service::retire() {
	// custom code would go here, but this service does not have one
	libecap::adapter::Service::stop();
}

bool Adapter::Service::wantsUrl(const char *url) const {
	return true; // no-op is applied to all messages
}

Adapter::Service::MadeXactionPointer
Adapter::Service::makeXaction(libecap::host::Xaction *hostx) {
	return Adapter::Service::MadeXactionPointer(
		new Adapter::Xaction(std::tr1::static_pointer_cast<Service>(self), hostx));
}


Adapter::Xaction::Xaction(libecap::shared_ptr<Service> aService,
	libecap::host::Xaction *x):
	service(aService),
	hostx(x),
	receivingVb(opUndecided), sendingAb(opUndecided) {
#ifdef DEBUG
	std::string filename;
	int randomId;
	srand(time(NULL));
	filename += "/tmp/respmodXaction" + std::to_string((rand() % 256)) + ".log";
	logFile.open(filename.c_str(), std::ofstream::out | std::ofstream::app);
	logFile << "RESPMOD Xaction::Xaction" << std::endl;
	logFile.flush();
#endif
        //Initializing the Unix Domain Socket connection
        int socketConnectStatus;
        struct sockaddr_un addr;
        //service->ecapguardian_listen_socket is the socket path string
	socketHandle = socket(AF_UNIX, SOCK_STREAM, 0);
        if (socketHandle == -1) {
		logFile << "RESPMOD Socket() filename " << service->ecapguardian_listen_socket << " errno: " << strerror(errno) << std::endl;
		throw libecap::TextException(RunErrorPrefix + "Failed to get Socket Handle for socket filename '" +
			service->ecapguardian_listen_socket + "'. errno: " + strerror(errno));
        }

        //Set up the sockaddr struct with necessary paths
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, service->ecapguardian_listen_socket.c_str(), sizeof(addr.sun_path)-1);

	logFile << "RESPMOD Xaction::Xaction: Connecting to socket: " << service->ecapguardian_listen_socket.c_str() << std::endl;
        //Make the connection
        socketConnectStatus = connect(socketHandle, (struct sockaddr*)&addr, sizeof(addr));
	logFile << "RESPMOD Xaction::Xaction: after Connect, s=" << socketConnectStatus << std::endl;

        if (socketConnectStatus < 0) {
        	logFile << "RESPMOD Connect errno: " << strerror(errno) << std::endl;
                throw libecap::TextException(RunErrorPrefix + "Failed to Connect to RESPMOD socket. errno: "
			+ strerror(errno));
        }
        //If you got here, you're ready to start writing to the socket
	logFile << std::flush;
}

Adapter::Xaction::~Xaction() {
	if (libecap::host::Xaction *x = hostx) {
#ifdef DEBUG
		logFile << "RESPMOD Xaction : Adaptation Aborted" << std::endl;
#endif
		hostx = 0;
		x->adaptationAborted();
	}
	//Close the socket
        close(socketHandle);
#ifdef DEBUG
	logFile << "RESPMOD Xaction::~Xaction" << std::endl;
	logFile << "==================================================" << std::endl;
	logFile << std::flush;
	logFile.close();
#endif
}

const libecap::Area Adapter::Xaction::option(const libecap::Name &) const {
	return libecap::Area(); // this transaction has no meta-information
}

void Adapter::Xaction::visitEachOption(libecap::NamedValueVisitor &) const {
	// this transaction has no meta-information to pass to the visitor
}

void Adapter::Xaction::start() {
	char buf[BUF_SIZE];
	char c;
	Must(hostx);
	sharedPointerToVirginHeaders = hostx->virgin().clone();
	libecap::shared_ptr<libecap::Message> cause = hostx->cause().clone();
#ifdef DEBUG
	logFile << "RESPMOD Xaction::start" << std::endl;
#endif
	Must(sharedPointerToVirginHeaders != 0);
	Must(cause != 0);

	//
	// Write the request headers to ecapguardian
	// These are necessary for the response scanner plugins in ecapguardian
	ssize_t s = 0;
	// NOTE: ecapguardian REQUIRES 1 character past the final newline, so we just write 'size'
	// instead of 'size - 1' because the last one is a null.  Same below with the response header.
#ifdef DEBUG
	if(cause->header().image().size == 0) {
		logFile << "RESPMOD Xaction::start : empty cause header" << std::endl;
	} else {
		logFile << "RESPMOD Xaction::start : cause header size: " << cause->header().image().size << std::endl;
		logFile << "RESPMOD Xaction::start : cause header:" << std::endl << cause->header().image() << std::endl;
	}
#endif
	s = write(socketHandle, cause->header().image().start, cause->header().image().size);
	checkWritten(s, cause->header().image().size, std::string("cause header"));

	//
	// Write the response headers to ecapguardian
	//
	//ssize_t write(int fd, const void *buf, size_t count);
#ifdef DEBUG
	if(sharedPointerToVirginHeaders->header().image().size == 0) {
		logFile << "RESPMOD Xaction::start : empty response header" << std::endl;
	} else {
		logFile << "RESPMOD Xaction::start : response header size: " << sharedPointerToVirginHeaders->header().image().size << std::endl;
	}
#endif
        s = write(socketHandle, sharedPointerToVirginHeaders->header().image().start, sharedPointerToVirginHeaders->header().image().size);
	checkWritten(s, sharedPointerToVirginHeaders->header().image().size, std::string("response header"));

	// For some reason, the socket is not blocking.  This will do until I figure out why.
	for(int sleepCount = 0; sleepCount < 10; sleepCount++) {
#ifdef DEBUG
		logFile << "RESPMOD Xaction::start : Sleeping #" << sleepCount << std::endl;
#endif
	        s = read(socketHandle, buf, 1);
		if(s > 0) {
			logFile << "RESPMOD Xaction::start : s > 0: " << s << std::endl;
			break;
		} else {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
#ifdef DEBUG
        logFile << "RESPMOD Xaction::start : After receiving response char, s=" << s << std::endl;
#endif
        if(s != 1){
                throw libecap::TextException("After response char, s was " + std::to_string(s));
                exit(-1);
        }

	c = buf[0];
#ifdef DEBUG
	logFile << "RESPMOD Xaction::start : response char was '" << c << "'" << std::endl;
#endif
	if(c == FLAG_USE_VIRGIN) {
#ifdef DEBUG
                logFile << "RESPMOD Xaction::start : skipping content scan after request header check" << std::endl;
#endif
		sendingAb = opNever; // there is nothing to send
                lastHostCall()->useVirgin();
		return;
	}

	//Write back the message received flag
	//buf[0] = FLAG_MSG_RECVD;
	s = write(socketHandle, &FLAG_MSG_RECVD, 1);
#ifdef DEBUG
	logFile << "RESPMOD Xaction::start : wrote FLAG_MSG_RECVD" << std::endl;
#endif
	if (hostx->virgin().body()) {
#ifdef DEBUG
		logFile << "RESPMOD Xaction::start : has VB, requesting it now" << std::endl;
#endif
		receivingVb = opOn;
		hostx->vbMake(); // ask host to supply virgin body
	}

	// As the VB is being made, dump it to ecapguardian in the 'noteVbContentAvailable' calls
	// Do NOT call the 'lastHostCall' at the end of the 'start' method
	// End of the 'start' method is reached long before the last of the VB is delivered
#ifdef DEBUG
	logFile << "RESPMOD Xaction::start : end of method" << std::endl;
	logFile << std::flush;
#endif
}

void Adapter::Xaction::checkWritten(ssize_t sent, size_type expectedSent, std::string name) {
	if(sent == -1){
		std::string error("RESPMOD Xaction::checkWritten : errno on '" + name + "' write. errno: ");
		error.append(strerror(errno));
#ifdef DEBUG
                logFile << error << std::endl;
#endif
                //There was some sort of error
                //We can recover from one of these types of errors
                if(errno){
                        //Not recoverable
                        throw libecap::TextException(error);
                }
        }
        if(sent != expectedSent){
		std::string error(RunErrorPrefix + "Failed RESPMOD '" + name + "' headers write to ecapguardian. Wrote "
                        +  std::to_string(sent) + " instead of " + std::to_string(expectedSent));
#ifdef DEBUG
		logFile << error << std::endl;
#endif
                throw libecap::TextException(error);
        }
}

void Adapter::Xaction::stop() {
	hostx = 0;
	// the caller will delete
#ifdef DEBUG
	logFile << "RESPMOD Xaction::stop" << std::endl;
#endif
}

void Adapter::Xaction::abDiscard()
{
#ifdef DEBUG
	logFile << "RESPMOD Xaction::abDiscard" << std::endl;
#endif
	Must(sendingAb == opUndecided); // have not started yet
	sendingAb = opNever;
	// we do not need more vb if the host is not interested in ab
	stopVb();
}

void Adapter::Xaction::abMake()
{
#ifdef DEBUG
	logFile << "RESPMOD Xaction::abMake" << std::endl;
#endif
	Must(sendingAb == opUndecided); // have not yet started or decided not to send
	Must(hostx->virgin().body()); // that is our only source of ab content

	// we are or were receiving vb
	Must(receivingVb == opOn || receivingVb == opComplete);

	if (!buffer.empty()){
		sendingAb = opOn;
#ifdef DEBUG
		logFile << "RESPMOD Xaction::abMake : buffer not empty" << std::endl;
#endif
		hostx->noteAbContentAvailable();
	}
}

void Adapter::Xaction::abMakeMore() {
#ifdef DEBUG
	logFile << "RESPMOD Xaction::abMakeMore" << std::endl;
#endif
	Must(receivingVb == opOn); // a precondition for receiving more vb
}

void Adapter::Xaction::abStopMaking() {
#ifdef DEBUG
	logFile << "RESPMOD Xaction::abStopMaking" << std::endl;
#endif
	sendingAb = opComplete;
	// we do not need more vb if the host is not interested in more ab
	stopVb();
}


libecap::Area Adapter::Xaction::abContent(size_type offset, size_type size) {
	Must(sendingAb == opOn || sendingAb == opComplete);
#ifdef DEBUG
	logFile << "RESPMOD Xaction::abContent : buffer.size()=" << buffer.size() <<  "| offset=" << offset << ", size=" << size << std::endl;
#endif
	return libecap::Area::FromTempString(buffer.substr(offset, size));
}

void Adapter::Xaction::abContentShift(size_type size) {
#ifdef DEBUG
	logFile << "RESPMOD Xaction::abContentShift : size=" << size << std::endl;
#endif
	Must(sendingAb == opOn || sendingAb == opComplete);
	buffer.erase(0, size);
	if(buffer.size() <= 0) {
		hostx->noteAbContentDone(true);
	}
}

void Adapter::Xaction::noteVbContentDone(bool atEnd) {
#ifdef DEBUG
	logFile << "RESPMOD Xaction::noteVbContentDone : atEnd=" << atEnd << std::endl;
	logFile << std::flush;
#endif
	Must(receivingVb == opOn);
	stopVb();

	// For the moment, just tell the host to use the 'adapted' (cached original) body.
	hostx->useAdapted(sharedPointerToVirginHeaders);

//	if (sendingAb == opOn) {
//		hostx->noteAbContentDone(atEnd);
//		sendingAb = opComplete;
//	}
}

void Adapter::Xaction::noteVbContentAvailable() {
#ifdef DEBUG
	logFile << "RESPMOD Xaction::noteVbContentAvailable" << std::endl;
#endif
	long startFrom = 0;
	Must(receivingVb == opOn);
	const libecap::Area vb = hostx->vbContent(0, libecap::nsize); // get all vb in this chunk
	std::string chunk = vb.toString();
#ifdef DEBUG
	logFile << "RESPMOD Xaction::noteVbContentAvailable : chunk was size: " << chunk.size() << std::endl;
#endif
	buffer += chunk;
	hostx->vbContentShift(vb.size); // 'shift' means 'delete' since we have a copy

	ssize_t s = write(socketHandle, chunk.c_str(), chunk.size());
	checkWritten(s, chunk.size(), "VB Content Chunk");


	// Check for response from ecapguardian here!
	// Then send the response received signal
        // For some reason, the socket is not blocking.  This will do until I figure out why.
/*
        for(int sleepCount = 0; sleepCount < 10; sleepCount++) {
#ifdef DEBUG
                logFile << "RESPMOD Xaction::noteVbContentAvailable : Sleeping #" << sleepCount << std::endl;
#endif
                s = read(socketHandle, buf, 1);
                if(s > 0) {
                        logFile << "RESPMOD Xaction::noteVbContentAvailable : s > 0: " << s << std::endl;
                        break;
                } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
        }
#ifdef DEBUG
        logFile << "RESPMOD Xaction::noteVbContentAvailable : After receiving response char, s=" << s << std::endl;
#endif
        if(s != 1){
                throw libecap::TextException("After response char, s was " + std::to_string(s));
                exit(-1);
        }

        c = buf[0];
#ifdef DEBUG
        logFile << "RESPMOD Xaction::noteVbContentAvailable : response char was '" << c << "'" << std::endl;
#endif
	if(c == FLAG_USE_VIRGIN) {
#ifdef DEBUG
                logFile << "RESPMOD Xaction::noteVbContentAvailable : skipping content scan after request header check" << std::endl;
#endif
                sendingAb = opOn; // start the process of sending VB
        }

        //Write back the message received flag
        //buf[0] = FLAG_MSG_RECVD;
        s = write(socketHandle, &FLAG_MSG_RECVD, 1);
#ifdef DEBUG
        logFile << "RESPMOD Xaction::noteVbContentAvailable : wrote FLAG_MSG_RECVD" << std::endl;
#endif
*/
}

// tells the host that we are not interested in [more] vb
// if the host does not know that already
void Adapter::Xaction::stopVb() {
#ifdef DEBUG
	logFile << "RESPMOD Xaction::stopVb" << std::endl;
#endif
	if (receivingVb == opOn) {
		hostx->vbStopMaking(); // we will not call vbContent() any more
		receivingVb = opComplete;
	} else {
		// we already got the entire body or refused it earlier
		Must(receivingVb != opUndecided);
	}
}

// this method is used to make the last call to hostx transaction
// last call may delete adapter transaction if the host no longer needs it
// TODO: replace with hostx-independent "done" method
libecap::host::Xaction *Adapter::Xaction::lastHostCall() {
#ifdef DEBUG
	logFile << "RESPMOD Xaction::lastHostCall" << std::endl;
#endif
	libecap::host::Xaction *x = hostx;
	Must(x);
	hostx = 0;
	return x;
}

// create the adapter and register with libecap to reach the host application
static const bool Registered =
	libecap::RegisterVersionedService(new Adapter::Service);
