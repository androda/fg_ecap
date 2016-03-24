/*
	Copyright Jacob Carter 2015 - 2016
	Based on the eCAP Adapter Sample found here: http://e-cap.org/Downloads
*/
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
		virtual std::string uri() const; // needs to be unique
		virtual std::string tag() const; // changes with version and config
		virtual void describe(std::ostream &os) const; // custom info

		// Configuration
		virtual void configure(const libecap::Options &cfg);
		virtual void reconfigure(const libecap::Options &cfg);
		void setOne(const libecap::Name &name, const libecap::Area &valArea);

		// Lifecycle
		virtual void start(); // calls to makeXaction will be made
		virtual void stop(); // stops calls to makeXaction until another start
		virtual void retire(); // service death

		// Scope
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

		virtual void start();
		virtual void stop();

		virtual void abDiscard();
		virtual void abMake();
		virtual void abMakeMore();
		virtual void abStopMaking();

		virtual libecap::Area abContent(size_type offset, size_type size);
		virtual void abContentShift(size_type size);

		virtual void noteVbContentDone(bool atEnd);
		virtual void noteVbContentAvailable();

	protected:
		void stopVb(); // tells host we don't need more VB
		libecap::host::Xaction *lastHostCall(); // eCAP should have a better
			//method for taking care of this

	private:
#ifdef DEBUG
		std::ofstream logFile;
#endif
		libecap::shared_ptr<const Service> service;
		libecap::host::Xaction *hostx;

		std::string buffer; // for original request body content
		std::string e2buffer; // for blockpage
		int socketHandle;  // the ecapguardian eCAP listener

		typedef enum { opUndecided, opWaiting, opOn, opComplete, opNever } OperationState;
		OperationState receivingVb;
		OperationState sendingAb;

		//// Used for determining which body buffer to use
		//// Either the original body (useVirgin) or the modified one
		bool blocked = false;

		////  Flags and such for communication with server
		const int BUF_SIZE = 512;
		const char FLAG_USE_VIRGIN = 'v';
		const char FLAG_MODIFY = 'm';
		const char FLAG_BLOCK = 'b';
		const char FLAG_MSG_RECVD = 'r'; // used to signal header/body received to the server
                const std::string FLAG_END = "\n\n\0\0"; //used for headers/body end
		const std::string FLAG_END_REMOVE = "\0\0"; //Remove this from the end of the headers/body
};

static const std::string PACKAGE_NAME = "FilterGizmo";

static const std::string PACKAGE_VERSION = "0.1.0";

static const std::string CfgErrorPrefix =
	"FilterGizmo REQMOD Adapter: configuration error: ";

static const std::string RunErrorPrefix = "FilterGizmo REQMOD Adapter: Runtime Error: ";

} // namespace Adapter

std::string Adapter::Service::uri() const {
	return "ecap://filtergizmo.com/ecapguardian/reqmod";
}

std::string Adapter::Service::tag() const {
	return PACKAGE_VERSION;
}

void Adapter::Service::describe(std::ostream &os) const {
	os << "REQMOD content filtering by " << PACKAGE_NAME << " v" << PACKAGE_VERSION;;
}

void Adapter::Service::configure(const libecap::Options &cfg) {
	Cfgtor cfgtor(*this);
	cfg.visitEachOption(cfgtor);

	// check for post-configuration errors and inconsistencies

	if (ecapguardian_listen_socket.empty()) {
		throw libecap::TextException(CfgErrorPrefix +
			"ecapguardian_listen_socket value is not set");
	}
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
	// no necessary custom code here
}

void Adapter::Service::stop() {
	libecap::adapter::Service::stop();
	// no necessary custom code here
}

void Adapter::Service::retire() {
	libecap::adapter::Service::stop();
	// no necessary custom code here
}

bool Adapter::Service::wantsUrl(const char *url) const {
	return true;
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
	int bytes;
	int s;
	struct sockaddr_un addr;
#ifdef DEBUG
	std::string filename;
        int randomId;
        srand(time(NULL));
        filename += "/tmp/reqmodXaction" + std::to_string(randomId) + ".log";
        logFile.open(filename.c_str(), std::ofstream::out | std::ofstream::app);
        logFile << "REQMOD Xaction::Xaction" << std::endl;
	logFile << "REQMOD Xaction::Xaction : eCAP Adapter socket path: '" << service->ecapguardian_listen_socket << "'" << std::endl;
        logFile.flush();
#endif
	//Initializing the Unix Domain Socket connection
	//service->ecapguardian_listen_socket is the socket path string
	if ( (socketHandle = socket(AF_UNIX, SOCK_STREAM, 0) ) == -1) {
		throw libecap::TextException(RunErrorPrefix + "Failed to get Socket Handle for socket filename '" + 
			service->ecapguardian_listen_socket + "'. errno: " + strerror(errno));
	}

	//Set up the sockaddr struct with necessary paths
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, service->ecapguardian_listen_socket.c_str(), sizeof(addr.sun_path)-1);

        //logFile << "eCAP adapter: Connecting to socket" << std::endl;
	//Make the connection
	s = connect(socketHandle, (struct sockaddr*)&addr, sizeof(addr));
        //logFile << "eCAP adapter: after Connect, s=" << s << std::endl;

	if (s < 0) {
	//	logFile << "eCAP errno: " << strerror(errno) << std::endl;
		throw libecap::TextException(RunErrorPrefix + "Failed to Connect. errno: " + strerror(errno));
	}
	//If you got here, you're ready to start writing to the socket
}

Adapter::Xaction::~Xaction() {
	if (libecap::host::Xaction *x = hostx) {
		hostx = 0;
		x->adaptationAborted();
	}

	//Close the socket
	close(socketHandle);
#ifdef DEBUG
	logFile << "REQMOD Xaction::~Xaction" << std::endl;
	logFile << "=================================================" << std::endl;
	logFile.close();
#endif
}

const libecap::Area Adapter::Xaction::option(const libecap::Name &) const {
	return libecap::Area(); // no option/meta information
}

void Adapter::Xaction::visitEachOption(libecap::NamedValueVisitor &) const {
	// negatory on the meta-information
}

void Adapter::Xaction::start() {
	Must(hostx);
	int s;
	size_t t;
	char buf[BUF_SIZE];
	char c;
	std::string modifiedHeader;
	//This adapter will only ever receive REQMOD requests (yay for configuration options)
	//Dump the request headers over to ecapguardian
	//(Request headers will ALWAYS exist - the request body might not)
	/*
		These are the cases to deal with:
		1: Request made with no body
			a: Request is acceptable (no blocking or modification needed): useVirgin
			b: Request is NOT acceptable (needs modification)
				i: Read in modified headers and use them instead of the original
			c: Request is denied
				i: Read in response headers
				ii: Read in response body
				iii: Use eCAP's "request satisfaction" to respond with the block page
		2: Request made with body
			a: Request is acceptable: useVirgin
			b: Request is NOT acceptable (needs mod)
				i: Read in modified headers and use them instead of the original
				ii: Just re-use the pointer for the request body - ecapguardian doesn't check request bodies
	*/
	//The below 'hostx->virgin()' is a Message
	if (hostx->virgin().body()) {
#ifdef DEBUG
		logFile << "REQMOD Xaction::start : has VB" << std::endl;
#endif
		receivingVb = opOn;
		sendingAb = opWaiting;
		hostx->vbMake(); // tell the host to give us the virgin request body
	} else {
		// no virgin body?  Don't need it
		receivingVb = opNever;
		sendingAb = opWaiting;
#ifdef DEBUG
		logFile << "REQMOD Xaction::start : no VB" << std::endl;
#endif
	}

	//Make a clone of the request message (in case we need to modify it)
	libecap::shared_ptr<libecap::Message> adapted = hostx->virgin().clone();
	Must(adapted != 0);
	//Dump the request header over to ecapguardian
	//ssize_t write(int fd, const void *buf, size_t count);
	s = write(socketHandle, adapted->header().image().start, adapted->header().image().size);
#ifdef DEBUG
        logFile << "REQMOD Xaction::start : Original Request Header:" << std::endl
            << adapted->header().image().toString().c_str() << std::endl;
#endif
        if(s == -1){
#ifdef DEBUG
		logFile << "REQMOD Xaction::start : errno on header write. errno:" << strerror(errno) << std::endl;
#endif
		//There was some sort of error
		//We can recover from one of these types of errors
		if(errno){
			//Not recoverable
			throw libecap::TextException(RunErrorPrefix + "Failed to write header to ecapguardian. errno: " + strerror(errno));
		}
	}
	if(s != adapted->header().image().size){
		throw libecap::TextException(RunErrorPrefix + "Failed to write REQMOD headers to ecapguardian. Wrote " + std::to_string(s) 
			+ " instead of " + std::to_string(adapted->header().image().size));
	}
        //The two nulls at the end are no longer necessary
        //The ecapguardian system knows to stop reading the header at double newlines

#ifdef DEBUG
	logFile << "REQMOD Xaction::start : Dumped endHeader signal" << std::endl;
#endif
	//Make a BLOCKING read call, so that this adapter does not proceed
	//until the request is fulfilled
	s = read(socketHandle, buf, 1);
#ifdef DEBUG
	logFile << "REQMOD Xaction::start : After receiving response char, s=" << s << std::endl;
#endif
    	if(s != 1){
		throw libecap::TextException("After response char, s was " + std::to_string(s));
		exit(-1);
    	}
#ifdef DEBUG
	logFile << "REQMOD Xaction::start : About to do char comparisons" << std::endl;
#endif
	c = buf[0];
#ifdef DEBUG
	logFile << "REQMOD Xaction::start : Got char: " << c << std::endl;
#endif
	if(c == FLAG_USE_VIRGIN){
		//Tell the host to use the virgin request and move on with your life
		blocked = false;
#ifdef DEBUG
		logFile << "REQMOD Xaction::start : read 'v' from ecapguardian" << std::endl;
#endif
		lastHostCall()->useVirgin();
		return;
	} else if(c == FLAG_MODIFY){
		//Tell the host to use the modified request (modified header, anyway)
		blocked = false;
		libecap::Area headers;
#ifdef DEBUG
		logFile << "REQMOD Xaction::start : read 'm' from ecapguardian" << std::endl;
#endif
		//ecapguardian has modified the request
		//Read in the modified request header, and tell the host to use the adapted message
		//Don't lose track of the response body!
		do{
#ifdef DEBUG
			logFile << "REQMOD Xaction::start : Header Input Do loop" << std::endl;
#endif
			s = read(socketHandle, buf, BUF_SIZE);
#ifdef DEBUG
			logFile << "REQMOD Xaction::start : Read " << s << " header bytes." << std::endl;
                        logFile << "REQMOD Xaction::start : Read char: " << buf[0] << std::endl;
#endif
			modifiedHeader.append(buf, s);
			if(modifiedHeader.rfind(FLAG_END) != std::string::npos){
#ifdef DEBUG
				logFile << "REQMOD Xaction::start : End Header signal received - removing it from the header" << std::endl;
#endif
				//Rip out the last three chars: \n\0\0
				t = modifiedHeader.rfind(FLAG_END_REMOVE);
				if(t != std::string::npos){
					modifiedHeader.replace(t, FLAG_END_REMOVE.length(), "");
				}
				s = 0;
			}
		} while(s > 0);
                s = write(socketHandle, &FLAG_MSG_RECVD, 1);
#ifdef DEBUG
		logFile << "REQMOD Xaction::start : Header read in: " << std::endl << modifiedHeader.c_str() << std::endl;
#endif
		headers = libecap::Area::FromTempString(modifiedHeader);
#ifdef DEBUG
		logFile << "REQMOD Xaction::start : Header Area created" << std::endl;
#endif
		adapted->header().parse(headers);
#ifdef DEBUG
		logFile << "REQMOD Xaction::start : Header parsed in" << std::endl;
#endif
		hostx->useAdapted(adapted);
		return;
	} else if(c == FLAG_BLOCK){
		libecap::shared_ptr<libecap::Message> ptr;
		libecap::Area headers;
		libecap::Area responseBody;
		blocked = true;
#ifdef DEBUG
		logFile << "REQMOD Xaction::start : read 'b' from ecapguardian" << std::endl;
#endif
		//Only one other possibility here - a blocked request
		//modifiedHeader
		//Now, read back the block page headers
		do{
#ifdef DEBUG
			logFile << "REQMOD Xaction::start : Header Input Do loop" << std::endl;
#endif
			s = read(socketHandle, buf, BUF_SIZE);
#ifdef DEBUG
			logFile << "REQMOD Xaction::start : Read " << s << " header bytes." <<std::endl;
#endif
			modifiedHeader.append(buf, s);
			if(modifiedHeader.rfind(FLAG_END) != std::string::npos){
#ifdef DEBUG
				logFile << "REQMOD Xaction::start : End Header signal received - removing it from the header" << std::endl;
#endif
				//Rip out the last three chars: \n\0\0
				t = modifiedHeader.rfind(FLAG_END_REMOVE);
				if(t != std::string::npos){
					modifiedHeader.replace(t, FLAG_END_REMOVE.length(), "");
				}
				s = 0;
			}
		} while(s > 0);
#ifdef DEBUG
		logFile << "REQMOD Xaction::start : Header read in: " << std::endl << modifiedHeader.c_str() << std::endl;
#endif
		//Next, send the 'headers received' signal to the server
		s = write(socketHandle, &FLAG_MSG_RECVD, 1);
		//Now, read in the block page
		do{
#ifdef DEBUG
			logFile << "REQMOD Xaction::start : Blockpage Do Loop" << std::endl;
#endif
			s = read(socketHandle, buf, BUF_SIZE);
#ifdef DEBUG
			logFile << "REQMOD Xaction::start : Read " << s << " blockpage bytes" << std::endl;
#endif
			e2buffer.append(buf, s);
			if(e2buffer.rfind(FLAG_END) != std::string::npos){
#ifdef DEBUG
				logFile << "REQMOD Xaction::start : End Body signal received - removing it from the body" << std::endl;
#endif
				//Rip out the last three chars of the flag
				t = e2buffer.rfind(FLAG_END_REMOVE);
				if(t != std::string::npos){
					e2buffer.replace(t, FLAG_END_REMOVE.length(), "");
				}
				s = 0;
			}
		} while(s > 0);

		//Now, send the 'block page received' signal
		//The server will close the connection, and we close our end in the destructor
		s = write(socketHandle, &FLAG_MSG_RECVD, 1);
		//Now the funky part - make adapted headers and tell host to use adapted
		//This "libecap::MyHost().newResponse();" is found in registry.h
#ifdef DEBUG
		logFile << "REQMOD Xaction::start : Sent MSG_RECVD flag" << std::endl;
#endif
		ptr = libecap::MyHost().newResponse();
#ifdef DEBUG
		logFile << "REQMOD Xaction::start : Made new response message" << std::endl;
#endif
		headers = libecap::Area::FromTempString(modifiedHeader);
#ifdef DEBUG
		logFile << "REQMOD Xaction::start : Made Headers 'Area'" << std::endl;
#endif
		ptr->header().parse(headers);
#ifdef DEBUG
		logFile << "REQMOD Xaction::start : Parsed headers into request satisfaction message" << std::endl;
#endif
		ptr->addBody();  // This is just a flag saying that the message has a body.
				// The body is pulled via abMake() and abContent()
#ifdef DEBUG
		logFile << "REQMOD Xaction::start : Added body flag to request satisfaction message" << std::endl;
#endif
		//Need to use the correct message pointer - duh
		hostx->useAdapted(ptr);
		//I think the boolean parameter here tells the host whether they've
		//pulled in all of the AB content yet.  In this case, the AB is done -
		//But they're not at the end of the buffer yet.
		hostx->noteAbContentDone(false);
		return;
	} else{
		//What's this?
		throw libecap::TextException(RunErrorPrefix + "ecapguardian returned '" + buf[0] + "' which is not in the supported option set ('v','m','b')");
	}
	/*
		This is where everything happens in the REQMOD adapter.
		0: This adapter sends a 'q' to ecapguardian (signaling re'q'mod)
		1: The header is piped over to the ecapguardian server.
		2: The ecapguardian server scans and possibly modifies the header.
		3: If the ecapguardian server returns a "v" character first,
			tell the host to use the virgin header and move on.
		4: If ecapguardian responds with a 'b' then this request will trigger the blocked page.
		5: Otherwise, the ecapguardian server will return an "m" character.
			If that happens discard the "m", read in the new header,
			 and tell the host to use that instead.
		***Still need to figure out how to read in the header here.
			Not sure what I can use to create a header.
			
			1. Adding a body to a new adapted request is essentially the same as adding a body 
			to a new adapted response. After creating a new request using Host::newRequest() 
			and before calling host::xaction::useAdapted(), you need to call Message::addBody() 
			method to add an [empty] body to the newly created request message. Then you do 
			exactly what the code mentioned in #2 below does.
			
			***I get it
				The Message::addBody() method adds an *empty* Body to the response
					***(the Message is NOT where you put the response body data!)
				You need to set the Message::Body::bodySize using:
					BodySize(size_type size) where ()typedef uint64_t size_type)
					AKA: size_type is a uint64_t
				Once you have set the BodySize AND have all the response body data available
					in a buffer, you can call the host::Xaction::noteAbContentDone() method
					and the Host will call for adapter::Xaction::abContent() and
					adapter::Xaction::abContentShift() until all of the body buffer data has been consumed
					
			***Intercepting a request and responding to it instead of allowing the remote host to do so
				is called 'Request Satisfaction' and one way to do this is described here:
				https://answers.launchpad.net/ecap/+question/186565
				
			
			I presume I would use Host::newResponse which returns a Message
			Use this method to clone the existing message
				// clones the header and body presence; does not copy the body itself
				virtual shared_ptr<Message> clone() const = 0;
			Set the body pointer of the new request to be the body of the old request
			
			
	Notes:
		*Message::firstLine() returns a RequestLine object for requests but a StatusLine object for responses.
		*To get the raw header string, call the Header's '.image().toString()' method.
			*The code documentation says this is an expensive call, but it's necessary
		*You can ALSO (and maybe preferably) use the Header's public:
			const char *start;
			size_type size;
			*Just use these as the parameters for your write call:
				write(socketHandle, header.start, header.size)
		*A Header has a 'parse(const Area &buf)' method which will read in a new header (I think)
			*To create the Area you need, use:
			static Area FromTempBuffer(const char *aStart, size_type aSize);
			static Area FromTempString(const std::string &tmp);
		*So, create the Header, create the Area from the ecapguardian-provided header string/char, and parse it.
			*Note that the parse method throws an exception on failure
	*/
}

void Adapter::Xaction::stop() {
	hostx = 0;
	// the caller will delete
#ifdef DEBUG
	logFile << "REQMOD Xaction::stop" << std::endl;
#endif
}

void Adapter::Xaction::abDiscard()
{
#ifdef DEBUG
	logFile << "REQMOD Xaction::abDiscard" << std::endl;
#endif
	Must(sendingAb == opUndecided); // have not started yet
	sendingAb = opNever;
	// we do not need more vb if the host is not interested in ab
	stopVb();
}

void Adapter::Xaction::abMake()
{
#ifdef DEBUG
	logFile << "REQMOD Xaction::abMake" << std::endl;
#endif
	Must(sendingAb == opUndecided || sendingAb == opWaiting); // have not yet started or decided not to send
	if(blocked){
		sendingAb = opOn;
#ifdef DEBUG
		logFile << "REQMOD Xaction::abMake : Request Page Blocked - responding with blockpage" << std::endl;
#endif
		hostx->noteAbContentAvailable();
		return;
	}

	Must(hostx->virgin().body()); // that is our only source of ab content
	// we are or were receiving vb
	Must(receivingVb == opOn || receivingVb == opComplete);
	if (receivingVb == opComplete){
		sendingAb = opOn;
#ifdef DEBUG
		logFile << "REQMOD Xaction::abMake : buffer not empty" << std::endl;
#endif
		hostx->noteAbContentAvailable();
	}
}

void Adapter::Xaction::abMakeMore()
{
#ifdef DEBUG
	logFile << "REQMOD Xaction::abMakeMore" << std::endl;
#endif
	Must(receivingVb == opOn); // a precondition for receiving more vb
	if(!blocked){
		hostx->vbMakeMore();
	}
}

void Adapter::Xaction::abStopMaking()
{
#ifdef DEBUG
	logFile << "REQMOD Xaction::abStopMaking" << std::endl;
#endif
	sendingAb = opComplete;
	// we do not need more vb if the host is not interested in more ab
	stopVb();
}


libecap::Area Adapter::Xaction::abContent(size_type offset, size_type size) {
	Must(sendingAb == opOn || sendingAb == opComplete);
	std::string contentBuffer;
#ifdef DEBUG
	logFile << "REQMOD Xaction::abContent : offset=" << offset << ", size=" << size << std::endl;
#endif
	if(blocked){
		contentBuffer = e2buffer.substr(offset, size);
/*
#ifdef DEBUG
		logFile << "eCAP Request Satisfaction (Responding without calling destination server) Body: "
			<< std::endl << contentBuffer << std::endl;
#endif
*/
	} else{
		contentBuffer = buffer.substr(offset, size);
#ifdef DEBUG
		logFile << "REQMOD Xaction::abContent virgin request body : " << std::endl
			<< contentBuffer << std::endl;
#endif
	}
	return libecap::Area::FromTempString(contentBuffer);
}

void Adapter::Xaction::abContentShift(size_type size) {
#ifdef DEBUG
	logFile << "REQMOD Xaction::abContentShift : size=" << size << std::endl;
#endif
	Must(sendingAb == opOn || sendingAb == opComplete);

	if(blocked){
#ifdef DEBUG
		logFile << "REQMOD Xaction::abContentShift, erasing 'size' from blockpage buffer" << std::endl;
#endif
		e2buffer.erase(0, size);
	} else{
#ifdef DEBUG
		logFile << "REQMOD Xaction::abContentShift, erasing 'size' from virgin body buffer" << std::endl;
#endif
		buffer.erase(0, size);
	}
}

void Adapter::Xaction::noteVbContentDone(bool atEnd)
{
#ifdef DEBUG
	logFile << "REQMOD Xaction::noteVbContentDone : atEnd=" << atEnd << std::endl;
#endif
	Must(receivingVb == opOn);
	stopVb();
	if (sendingAb == opOn) {
		hostx->noteAbContentDone(atEnd);
		sendingAb = opOn;
	}
}

void Adapter::Xaction::noteVbContentAvailable()
{
	Must(receivingVb == opOn);
#ifdef DEBUG
	logFile << "REQMOD Xaction::noteVbContentAvailable" << std::endl;
#endif
	const libecap::Area vb = hostx->vbContent(0, libecap::nsize); // grabs as much VB content as is available
	std::string chunk = vb.toString(); // API says this is expensive - but it's necessary
	hostx->vbContentShift(vb.size); // 'shift' means 'delete' since we have a copy
	buffer += chunk; // don't just throw away what we got

	if (sendingAb == opOn){
#ifdef DEBUG
		logFile << "REQMOD Xaction::noteVbContentAvailable : noting abContentAvailable" <<std::endl;
#endif
		hostx->noteAbContentAvailable();
	}
}

// tells the host that we are not interested in [more] vb
// if the host does not know that already
void Adapter::Xaction::stopVb() {
#ifdef DEBUG
	logFile << "REQMOD Xaction::stopVb" << std::endl;
#endif
	if (receivingVb == opOn) {
		hostx->vbStopMaking();
		receivingVb = opComplete;
	} else {
		// already have the whole VB, or never needed it anyway
		Must(receivingVb != opUndecided);
	}
}

// this method is used to make the last call to hostx transaction
// last call may delete adapter transaction if the host no longer needs it
// TODO: replace with hostx-independent "done" method
libecap::host::Xaction *Adapter::Xaction::lastHostCall() {
#ifdef DEBUG
	logFile << "Xaction::lastHostCall" << std::endl;
#endif
	libecap::host::Xaction *x = hostx;
	Must(x);
	hostx = 0;
	return x;
}

// create the adapter and register with libecap to reach the host application
static const bool Registered =
	libecap::RegisterVersionedService(new Adapter::Service);
