//	Copyright (c) 2018 Chet Simpson & The Giblet Initiative
//	
//	This file is subject to the terms and conditions defined in
//	file 'LICENSE.MD', which is part of this source code package.
//
#include <Protocols/YMsg/Server/Processors/Mock/ServiceOperation.h>
#include <Protocols/YMsg/Server/Builders/TypingNotification.h>


namespace Giblet { namespace Protocols { namespace YMsg { namespace Server { namespace Processors { namespace Mock
{

	void ServiceOperation::Process(session_type& session, const header_type& header, payload_type& payload)
	{
		((void)header);

		static const string_type typingOperation("TYPING");

		//	FIXME: Make sure attribute is 22/0x16
		if (payload.operation == typingOperation)
		{
			const auto action(static_cast<Builders::TypingNotification::status_type>(std::stoi(std::string(payload.action))));

			Builders::TypingNotification builder;
			builder.Build(session, payload.contactId, payload.clientId, action);
			builder.Send(session);
		}
	}

}}}}}}
