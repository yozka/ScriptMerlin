#include "sme/sme_error.h"

#include <utility>

namespace sme::error
{

	Error::Error(::sme::error::Code code, std::string message, ::sme::error::Location location,
		std::vector<StackFrame> stack)
	:
		_code(code),
		_message(std::move(message)),
		_location(std::move(location)),
		_stack(std::move(stack))
	{
	}


	::sme::error::Code Error::Code() const noexcept
	{
		return _code;
	}


	const std::string& Error::Message() const noexcept
	{
		return _message;
	}


	const ::sme::error::Location& Error::Location() const noexcept
	{
		return _location;
	}


	const std::vector<StackFrame>& Error::Stack() const noexcept
	{
		return _stack;
	}

}
