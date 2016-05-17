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
#include <sstream>
#include <sys/un.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
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

std::ostream& logStart(std::ostream& output) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return output << getpid() << "," << tv.tv_sec << "." << tv.tv_usec << ",";
}

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
		bool debug = false;
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

		bool debug = false;
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
	} else if(name == "debug") {
		debug = true;
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
	debug = service->debug;
	if(debug) {
		std::string filename;
		int randomId;
		srand(time(NULL));
		filename += "/tmp/respmodXaction" + std::to_string((rand() % 256)) + ".log";
		logFile.open(filename.c_str(), std::ofstream::out | std::ofstream::app);
		logFile << logStart << "RESPMOD Xaction::Xaction" << std::endl;
		logFile.flush();
	}
        //Initializing the Unix Domain Socket connection
        int socketConnectStatus;
        struct sockaddr_un addr;
        //service->ecapguardian_listen_socket is the socket path string
	//The line below turns on blocking mode for the socket - not sure if I want that.
	//socketHandle = socket(AF_UNIX, SOCK_STREAM & ~O_NONBLOCK, 0);
	socketHandle = socket(AF_UNIX, SOCK_STREAM, 0);
	if (socketHandle == -1) {
		if(debug) {
			logFile << logStart << "RESPMOD Socket() filename " << service->ecapguardian_listen_socket << " errno: " << strerror(errno) << std::endl;
		}
		throw libecap::TextException(RunErrorPrefix + "Failed to get Socket Handle for socket filename '" +
			service->ecapguardian_listen_socket + "'. errno: " + strerror(errno));
        }

        //Set up the sockaddr struct with necessary paths
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, service->ecapguardian_listen_socket.c_str(), sizeof(addr.sun_path)-1);

	if(debug) {
		logFile << logStart << "RESPMOD Xaction::Xaction: Connecting to socket: " << service->ecapguardian_listen_socket.c_str() << std::endl;
	}
        //Make the connection
        socketConnectStatus = connect(socketHandle, (struct sockaddr*)&addr, sizeof(addr));
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::Xaction: after Connect, s=" << socketConnectStatus << std::endl;
	}
        if (socketConnectStatus < 0) {
		if(debug) {
        		logFile << logStart << "RESPMOD Connect errno: " << strerror(errno) << std::endl;
		}
                throw libecap::TextException(RunErrorPrefix + "Failed to Connect to RESPMOD socket. errno: "
			+ strerror(errno));
        }
        //If you got here, you're ready to start writing to the socket
}

Adapter::Xaction::~Xaction() {
	if (libecap::host::Xaction *x = hostx) {
		if(debug) {
			logFile << logStart << "RESPMOD Xaction : Adaptation Aborted" << std::endl;
		}
		hostx = 0;
		x->adaptationAborted();
	}
	//Close the socket
        close(socketHandle);
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::~Xaction" << std::endl;
		logFile << logStart << "==================================================" << std::endl;
		logFile.close();
	}
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
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::start" << std::endl;
	}
	Must(sharedPointerToVirginHeaders != 0);
	Must(cause != 0);

	//
	// Write the request headers to ecapguardian
	// These are necessary for the response scanner plugins in ecapguardian
	ssize_t s = 0;
	// NOTE: ecapguardian REQUIRES 1 character past the final newline, so we just write 'size'
	// instead of 'size - 1' because the last one is a null.  Same below with the response header.
	if(debug) {
		if(cause->header().image().size == 0) {
			logFile << logStart << "RESPMOD Xaction::start : empty cause header" << std::endl;
		} else {
			logFile << logStart << "RESPMOD Xaction::start : cause header size: " << cause->header().image().size << std::endl;
			logFile << logStart << "RESPMOD Xaction::start : cause header:" << std::endl << cause->header().image() << std::endl;
		}
	}
	s = write(socketHandle, cause->header().image().start, cause->header().image().size);
	checkWritten(s, cause->header().image().size, std::string("cause header"));

	//
	// Write the response headers to ecapguardian
	//
	//ssize_t write(int fd, const void *buf, size_t count);
	if(debug) {
		if(sharedPointerToVirginHeaders->header().image().size == 0) {
			logFile << logStart << "RESPMOD Xaction::start : empty response header" << std::endl;
		} else {
			logFile << logStart << "RESPMOD Xaction::start : response header size: " << sharedPointerToVirginHeaders->header().image().size << std::endl;
		}
        }
	s = write(socketHandle, sharedPointerToVirginHeaders->header().image().start, sharedPointerToVirginHeaders->header().image().size);
	checkWritten(s, sharedPointerToVirginHeaders->header().image().size, std::string("response header"));
        s = read(socketHandle, buf, 1);
	if(debug) {
	        logFile << logStart << "RESPMOD Xaction::start : After receiving response char, s=" << s << std::endl;
        }
	if(s != 1){
                throw libecap::TextException("After response char, s was " + std::to_string(s));
                exit(-1);
        }

	c = buf[0];
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::start : response char was '" << c << "'" << std::endl;
	}
	if(c == FLAG_USE_VIRGIN || c == FLAG_NEEDS_SCAN) {
		//Write back the message received flag
		s = write(socketHandle, &FLAG_MSG_RECVD, 1);
		if(debug) {
			logFile << logStart << "RESPMOD Xaction::start : wrote FLAG_MSG_RECVD" << std::endl;
		}
	} else {
		std::string error("RESPMOD Xaction::start : did not receive proper response flag.  Received '");
		error.append(c, 1);
		error.append("' insted of expected 'v' or 's'");
		throw libecap::TextException(error);
	}
	if(c == FLAG_USE_VIRGIN) {
		if(debug) {
                	logFile << logStart << "RESPMOD Xaction::start : skipping content scan after request header check" << std::endl;
		}
		sendingAb = opNever; // there is nothing to send
                lastHostCall()->useVirgin();
		return;
	}

	if (hostx->virgin().body()) {
		if(debug) {
			logFile << logStart << "RESPMOD Xaction::start : has VB, requesting it now" << std::endl;
		}
		receivingVb = opOn;
		hostx->vbMake(); // ask host to supply virgin body
	}

	// As the VB is being made, dump it to ecapguardian in the 'noteVbContentAvailable' calls
	// Do NOT call the 'lastHostCall' at the end of the 'start' method
	// End of the 'start' method is reached long before the last of the VB is delivered
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::start : end of method" << std::endl;
	}
}

void Adapter::Xaction::checkWritten(ssize_t sent, size_type expectedSent, std::string name) {
	if(sent == -1){
		std::string error("RESPMOD Xaction::checkWritten : errno on '" + name + "' write. errno: ");
		error.append(strerror(errno));
		if(debug) {
                	logFile << logStart << error << std::endl;
		}
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
		if(debug) {
			logFile << logStart << error << std::endl;
		}
                throw libecap::TextException(error);
        }
}

void Adapter::Xaction::stop() {
	hostx = 0;
	// the caller will delete
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::stop" << std::endl;
	}
}

void Adapter::Xaction::abDiscard()
{
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::abDiscard" << std::endl;
	}
	Must(sendingAb == opUndecided); // have not started yet
	sendingAb = opNever;
	// we do not need more vb if the host is not interested in ab
	stopVb();
}

void Adapter::Xaction::abMake()
{
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::abMake" << std::endl;
	}
	Must(sendingAb == opUndecided); // have not yet started or decided not to send
	Must(hostx->virgin().body()); // that is our only source of ab content

	// we are or were receiving vb
	Must(receivingVb == opOn || receivingVb == opComplete);

	if (!buffer.empty()){
		sendingAb = opOn;
		if(debug) {
			logFile << logStart << "RESPMOD Xaction::abMake : buffer not empty" << std::endl;
		}
		hostx->noteAbContentAvailable();
	}
}

void Adapter::Xaction::abMakeMore() {
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::abMakeMore" << std::endl;
	}
	Must(receivingVb == opOn); // a precondition for receiving more vb
}

void Adapter::Xaction::abStopMaking() {
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::abStopMaking" << std::endl;
	}
	sendingAb = opComplete;
	// we do not need more vb if the host is not interested in more ab
	stopVb();
}


libecap::Area Adapter::Xaction::abContent(size_type offset, size_type size) {
	Must(sendingAb == opOn || sendingAb == opComplete);
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::abContent : buffer.size()=" << buffer.size() <<  "| offset=" << offset << ", size=" << size << std::endl;
	}
	return libecap::Area::FromTempString(buffer.substr(offset, size));
}

void Adapter::Xaction::abContentShift(size_type size) {
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::abContentShift : size=" << size << std::endl;
	}
	Must(sendingAb == opOn || sendingAb == opComplete);
	buffer.erase(0, size);
	if(buffer.size() <= 0) {
		hostx->noteAbContentDone(true);
	}
}

void Adapter::Xaction::noteVbContentDone(bool atEnd) {
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::noteVbContentDone : atEnd=" << atEnd << std::endl;
	}
	Must(receivingVb == opOn);
	stopVb();
	size_t t;
	ssize_t s = 0;
	char buf[BUF_SIZE];
	char c;
	if(debug) {
        	logFile << logStart << "RESPMOD Xaction::noteVbContentDone : After writing response body to ecapguardian" << std::endl;
	}
	s = read(socketHandle, buf, 1);
	if(debug) {
	        logFile << logStart << "RESPMOD Xaction::noteVbContentDone : After receiving response char, s=" << s << std::endl;
	}
        if(s != 1){
		if(debug) {
			logFile << logStart << "RESPMOD Xaction::noteVbContentDone : ERROR: After receiving response char, s=" << s << std::endl;
		}
                throw libecap::TextException("After response char, s was " + std::to_string(s));
                exit(-1);
        }

	c = buf[0];
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::noteVbContentDone : response char was '" << c << "'" << std::endl;
	}
	if(c == FLAG_USE_VIRGIN || c == FLAG_MODIFY) {
		s = write(socketHandle, &FLAG_MSG_RECVD, 1);
		if(debug) {
	                logFile << logStart << "RESPMOD Xaction::start : wrote FLAG_MSG_RECVD" << std::endl;
		}
        } else {
                std::string error("RESPMOD Xaction::start : did not receive proper response flag.  Received '");
                error.append(c, 1);
                error.append("' insted of expected 'v' or 's'");
                throw libecap::TextException(error);
        }
	if(c == FLAG_USE_VIRGIN) {
		if(debug) {
	                logFile << logStart << "RESPMOD Xaction::noteVbContentDone : Telling host to use original cached response body" << std::endl;
		}
		hostx->useAdapted(sharedPointerToVirginHeaders);
	}
	if(c == FLAG_MODIFY) {  // Modify as in block or re-write
		libecap::shared_ptr<libecap::Message> ptr;
		libecap::Area headers;
		libecap::Area responseBody;
		std::string modifiedHeader;
		if(debug) {
			logFile << logStart << "REQMOD Xaction::noteVbContentDone : modifying response (blocked or modified)" << std::endl;
		}
		do{
			if(debug) {
				logFile << logStart << "REQMOD Xaction::noteVbContentDone : Blockpage Header Input Do loop" << std::endl;
			}
			s = read(socketHandle, buf, BUF_SIZE);
			if(debug) {
				logFile << logStart << "REQMOD Xaction::noteVbContentDone : Read " << s << " header bytes." <<std::endl;
			}
			modifiedHeader.append(buf, s);
			if(modifiedHeader.rfind(FLAG_END) != std::string::npos){
				if(debug) {
					logFile << logStart << "REQMOD Xaction::noteVbContentDone : End Header signal received - removing it from the header" << std::endl;
				}
				//Rip out the last three chars: \n\0\0
				t = modifiedHeader.rfind(FLAG_END_REMOVE);
				if(t != std::string::npos){
					modifiedHeader.replace(t, FLAG_END_REMOVE.length(), "");
				}
				s = 0;
			}
		} while(s > 0);
		if(debug) {
			logFile << logStart << "REQMOD Xaction::noteVbContentDone : Modified Header read in: " << std::endl << modifiedHeader.c_str() << std::endl;
		}
		//Next, send the 'headers received' signal to the server
		s = write(socketHandle, &FLAG_MSG_RECVD, 1);
		//Now, read in the modified response body
		buffer.clear();
		do{
			if(debug) {
				logFile << logStart << "REQMOD Xaction::noteVbContentDone : Modified Page Do Loop" << std::endl;
			}
			s = read(socketHandle, buf, BUF_SIZE);
			if(debug) {
				logFile << logStart << "REQMOD Xaction::noteVbContentDone : Read " << s << " modified page bytes" << std::endl;
			}
			buffer.append(buf, s);
			if(buffer.rfind(FLAG_END) != std::string::npos){
				if(debug) {
					logFile << logStart << "REQMOD Xaction::noteVbContentDone : End Body signal received - removing it from the body" << std::endl;
				}
				//Rip out the last three chars of the flag
				t = buffer.rfind(FLAG_END_REMOVE);
				if(t != std::string::npos){
					buffer.replace(t, FLAG_END_REMOVE.length(), "");
				}
				s = 0;
			}
		} while(s > 0);
		//Tell the server that we've received the modified response body
		s = write(socketHandle, &FLAG_MSG_RECVD, 1);
		//Now the funky part - make adapted headers and tell host to use adapted
		//This "libecap::MyHost().newResponse();" is found in registry.h
		if(debug) {
			logFile << logStart << "REQMOD Xaction::noteVbContentDone : Sent MSG_RECVD flag" << std::endl;
		}
		ptr = libecap::MyHost().newResponse();
		if(debug) {
			logFile << logStart << "REQMOD Xaction::noteVbContentDone : Made new response message" << std::endl;
		}
		headers = libecap::Area::FromTempString(modifiedHeader);
		if(debug) {
			logFile << logStart << "REQMOD Xaction::noteVbContentDone : Made Headers 'Area'" << std::endl;
		}
		ptr->header().parse(headers);
		if(debug) {
			logFile << logStart << "REQMOD Xaction::noteVbContentDone : Parsed headers into request satisfaction message" << std::endl;
		}
		ptr->addBody();  // This is just a flag saying that the message has a body.
				// The body is pulled via abMake() and abContent()
		if(debug) {
			logFile << logStart << "REQMOD Xaction::noteVbContentDone : Added body flag to request satisfaction message" << std::endl;
		}
		//Need to use the correct message pointer - duh
		hostx->useAdapted(ptr);
		hostx->noteAbContentDone(true);
	}
}

void Adapter::Xaction::noteVbContentAvailable() {
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::noteVbContentAvailable" << std::endl;
	}
	long startFrom = 0;
	Must(receivingVb == opOn);
	const libecap::Area vb = hostx->vbContent(0, libecap::nsize); // get all vb in this chunk
	std::string chunk = vb.toString();
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::noteVbContentAvailable : chunk was size: " << chunk.size() << std::endl;
	}
	buffer += chunk;
	hostx->vbContentShift(vb.size); // 'shift' means 'delete' since we have a copy

	size_t vb_chunk_written = 0;
        do {
                ssize_t s = write(socketHandle, chunk.c_str() + vb_chunk_written, chunk.size() - vb_chunk_written);
                vb_chunk_written = vb_chunk_written + s;
		if(debug) {
		        logFile << logStart << "RESPMOD Xaction::noteVbContentAvailable : Wrote " << s << " bytes out of " << chunk.size() << " bytes total in chunk, "
        	                 << vb_chunk_written << " written in total" << std::endl;
		}
        } while (vb_chunk_written < chunk.size());
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::noteVbContentAvailable : Finished writing this chunk" << std::endl;
	}
}

// tells the host that we are not interested in [more] vb
// if the host does not know that already
void Adapter::Xaction::stopVb() {
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::stopVb" << std::endl;
	}
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
	if(debug) {
		logFile << logStart << "RESPMOD Xaction::lastHostCall" << std::endl;
	}
	libecap::host::Xaction *x = hostx;
	Must(x);
	hostx = 0;
	return x;
}

// create the adapter and register with libecap to reach the host application
static const bool Registered =
	libecap::RegisterVersionedService(new Adapter::Service);
