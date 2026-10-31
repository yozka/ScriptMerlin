#include "sme/sme_error.h"

#include <utility>

namespace sme::error
{

	Error::Error(Code code, std::string message, Location location, std::vector<StackFrame> stack)
	:
		_code(code),
		_message(std::move(message)),
		_location(std::move(location)),
		_stack(std::move(stack))
	{
	}


	Code Error::code() const noexcept
	{
		return _code;
	}


	const std::string& Error::message() const noexcept
	{
		return _message;
	}


	const Location& Error::location() const noexcept
	{
		return _location;
	}


	const std::vector<StackFrame>& Error::stack() const noexcept
	{
		return _stack;
	}

}
