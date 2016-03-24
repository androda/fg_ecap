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

		// custom method dealing with sending VB content to ecapguardian
		void sendVbContent(std::string &chunk) const;
		void checkWritten(ssize_t s, size_type sent);

	private:
		std::string previousChunk;
		std::ofstream logFile;

		libecap::shared_ptr<const Service> service; // configuration access
		libecap::host::Xaction *hostx; // Host transaction rep

		std::string buffer; // for content adaptation
		int socketHandle;  // the ecapguardian eCAP listener

		typedef enum { opUndecided, opOn, opComplete, opNever } OperationState;
		OperationState receivingVb;
		OperationState sendingAb;
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
	filename += "/tmp/respmodXaction" + std::to_string(randomId) + ".log";
	logFile.open(filename.c_str(), std::ofstream::out | std::ofstream::app);
	logFile << "RESPMOD Xaction::Xaction" << std::endl;
	logFile.flush();
#endif
#ifdef SOCKET
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

logFile << "RESPMOD Xaction::Xaction: Connecting to socket" << std::endl;
        //Make the connection
        socketConnectStatus = connect(socketHandle, (struct sockaddr*)&addr, sizeof(addr));
logFile << "RESPMOD Xaction::Xaction: after Connect, s=" << s << std::endl;

        if (socketConnectStatus < 0) {
        	logFile << "RESPMOD Connect errno: " << strerror(errno) << std::endl;
                throw libecap::TextException(RunErrorPrefix + "Failed to Connect to RESPMOD socket. errno: "
			+ strerror(errno));
        }
        //If you got here, you're ready to start writing to the socket
#endif
}

Adapter::Xaction::~Xaction() {
	if (libecap::host::Xaction *x = hostx) {
#ifdef DEBUG
		logFile << "RESPMOD Xaction : Adaptation Aborted" << std::endl;
#endif
		hostx = 0;
		x->adaptationAborted();
	}
#ifdef SOCKET
	//Close the socket
        close(socketHandle);
#endif
#ifdef DEBUG
	logFile << "RESPMOD Xaction::~Xaction" << std::endl;
	logFile << "==================================================" << std::endl;
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
	Must(hostx);
#ifdef DEBUG
	logFile << "RESPMOD Xaction::start" << std::endl;
	logFile << "RESPMOD Xaction::start : Header String:" << std::endl
                << hostx->virgin().header().image().toString() << std::endl;
#endif
	libecap::shared_ptr<libecap::Message> cause = hostx->cause().clone();
	libecap::shared_ptr<libecap::Message> adapted = hostx->virgin().clone();
	Must(adapted != 0);
	Must(cause != 0);
	if (!adapted->body()) {
#ifdef DEBUG
		logFile << "RESPMOD Xaction::start : no response body - skipping scan" << std::endl;
#endif
		sendingAb = opNever; // there is nothing to send
		lastHostCall()->useVirgin();
		return;
	}

#ifdef SOCKET
	//
	// Write the request headers to ecapguardian
	// These are necessary for the response scanners in ecapguardian
	ssize_t s = 0;
	s = write(socketHandle, cause->header().image().start, cause->header().image().size);
	checkWritten(s, cause->header().image().size, std::string("cause"));
	//
	// Write the response headers to ecapguardian
	//
	//ssize_t write(int fd, const void *buf, size_t count);
        s = write(socketHandle, adapted->header().image().start, adapted->header().image().size);
	checkWritten(s, adapted->header().image().size, std::string("response"));
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
#endif
}

void Adapter::Xaction::checkWritten(ssize_t sent, size_type expectedSent, std::string name) {
	if(sent == -1){
		std::string error("RESPMOD Xaction::checkWritten : errno on '" + name + "' header write. errno: ");
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

	sendingAb = opOn;
	if (!buffer.empty()){
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

	//
	// TODO: This likely needs to change
	//	* Doesn't matter if the host has VB - the only AB here will be the
	//		block page
	//
	hostx->vbMakeMore();
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
	logFile << "RESPMOD Xaction::abContent : offset=" << offset << ", size=" << size << std::endl;
#endif
	std::string contentBuffer = buffer.substr(offset, size);
//	logFile << "RESPMOD Xaction::abContent actual content: " <<std::endl << contentBuffer << std::endl;
	return libecap::Area::FromTempString(contentBuffer);
}

void Adapter::Xaction::abContentShift(size_type size) {
#ifdef DEBUG
	logFile << "RESPMOD Xaction::abContentShift : size=" << size << std::endl;
#endif
	Must(sendingAb == opOn || sendingAb == opComplete);
	buffer.erase(0, size);
}

void Adapter::Xaction::noteVbContentDone(bool atEnd) {
#ifdef DEBUG
	logFile << "RESPMOD Xaction::noteVbContentDone : atEnd=" << atEnd << std::endl;
#endif
	Must(receivingVb == opOn);
	stopVb();

	// For the moment, just tell the host to use the original body.
	// Need to make sure I handle this case - easy to lose it
//	if(atEnd) {
	lastHostCall()->useVirgin();
//	}

	if (sendingAb == opOn) {
		hostx->noteAbContentDone(atEnd);
		sendingAb = opComplete;
	}
}

void Adapter::Xaction::noteVbContentAvailable() {
	Must(receivingVb == opOn);
#ifdef DEBUG
	logFile << "RESPMOD Xaction::noteVbContentAvailable" << std::endl;
#endif
	const libecap::Area vb = hostx->vbContent(0, libecap::nsize); // get all vb in this chunk
	std::string chunk = vb.toString(); // expensive, but simple

	//libecap::nsize above means 'until the end of the string'

	logFile << "RESPMOD Xaction::noteVbContentAvailable : Chunk size: " << chunk.size() << std::endl;

	if(!previousChunk.empty()){
		// Compare the two - my guess is that the previousChunk will match the current chunk up until the length difference
		logFile << "RESPMOD Xaction::noteVbContentAvailable : Previous Chunk size: " << previousChunk.size() << std::endl;
		for(int i = 0; i < previousChunk.size(); i++){
			if(previousChunk[i] != chunk[i]){
				logFile << "Previous Chunk mismatch at " << i << std::endl;
			}
		}
	}
	previousChunk = chunk;
	// We need the host to keep the original VB - don't tell it to throw anything away
	// Is the only other option for the adapter to maintain the chunks and send them back
	//	if the virgin body is OK?
	//hostx->vbContentShift(vb.size); // we have a copy; do not need vb any more

	//sendVbContent(chunk);

	if (sendingAb == opOn){
#ifdef DEBUG
		logFile << "RESPMOD Xaction::noteVbContentAvailable : noting abContentAvailable" <<std::endl;
#endif
		hostx->noteAbContentAvailable();
	}
}

void Adapter::Xaction::sendVbContent(std::string& chunk) const {
	//
	// This is where you send the VB to ecapguardian (likely in chunks of ~8k)
	// CAUTION: The chunks of data that come through here likely DO NOT line up with
	//	'chunked' transfer encoding boundaries!  Extra logic is needed on the
	//	ecapguardian side to resolve the chunking boundaries if http 1.1 is 
	//	to be supported.
	//

	//
	// DO NOT buffer the VB content that comes down the pipe
	//	* Pass it to ecapguardian and discard
	//	* We are either simply allowing the original response (no buffering necessary)
	//		OR sending back a blockpage with its own buffer.  Don't waste memory.
	//
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
