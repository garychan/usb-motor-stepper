#include "MessageInfo.hpp"
#include "messages.h"

namespace ums {

class PongMsgInfo : public MessageInfo
{
public:
	PongMsgInfo() {
		name_ = "Pong";
		id_ = PongMsg_ID;
		size_ = PongMsg_LENGTH;

		addToRegistry();
	}

	std::string translate(const buffer_t &b) const
	{	std::stringstream ss;
		const PongMsg *pm = reinterpret_cast<const PongMsg*>(&b[0]);
		ss << "Pong: version is " << (int)(pm->majorVersion) << "." << (int)(pm->minorVersion);
		return ss.str();
	}

};

PongMsgInfo thePong;

}