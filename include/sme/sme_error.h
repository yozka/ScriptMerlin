#pragma once

#include <cstddef>
#include <stdexcept>


#include <optional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sme::error
{
	enum class Code : int
	{
		UNKNOWN = 0,

		// Общие ошибки встроенных/native-функций.
		FUNCTION_ARGUMENTS = 100,
		VM_INTEGER_OVERFLOW = 101,
		VM_EXECUTION_FAILURE = 102,

		// Препроцессор: подключение файлов, директивы и макросы.
		PREPROCESSING_FAILURE = 1000,
		PREPROCESSING_FILE_SYSTEM_EMPTY = 1001,
		PREPROCESSING_FILE_OPEN_FAILED = 1002,
		PREPROCESSING_INCLUDE_NOT_FOUND = 1003,
		PREPROCESSING_INCLUDE_NOT_AVAILABLE = 1004,
		PREPROCESSING_UNKNOWN_DIRECTIVE = 1005,
		PREPROCESSING_INCLUDE_PATH_NOT_QUOTED = 1006,
		PREPROCESSING_UNTERMINATED_INCLUDE_PATH = 1007,
		PREPROCESSING_DEFINE_NAME_MISSING = 1008,
		PREPROCESSING_INVALID_DEFINE = 1009,
		PREPROCESSING_INTEGER_OUT_OF_RANGE = 1010,
		PREPROCESSING_MACRO_EXPANSION_LIMIT = 1012,
		PREPROCESSING_UNTERMINATED_COMMENT = 1013,

		// Лексер: недопустимые символы и некорректные литералы.
		LEXING_FAILURE = 2000,
		LEXING_UNTERMINATED_COMMENT = 2001,
		LEXING_UNEXPECTED_CHARACTER = 2002,
		LEXING_HEX_LITERAL_WITHOUT_DIGITS = 2003,
		LEXING_BINARY_LITERAL_WITHOUT_DIGITS = 2004,
		LEXING_INTEGER_OUT_OF_RANGE = 2005,
		LEXING_NUMERIC_LITERAL_OUT_OF_RANGE = 2006,
		LEXING_EXPONENT_WITHOUT_DIGITS = 2007,
		LEXING_UNTERMINATED_CHARACTER = 2008,
		LEXING_UNTERMINATED_STRING = 2009,

		// Парсер: некорректная структура исходной программы.
		PARSE_SYNTAX = 3000,
		PARSE_EXPECTED_FUNCTION_OR_DECLARATION = 3001,
		PARSE_EXPECTED_TOKEN = 3002,
		PARSE_EXPECTED_END_OF_STATEMENT = 3003,
		PARSE_GLOBAL_INITIALIZER_UNSUPPORTED = 3004,
		PARSE_NO_PROGRESS = 3005,
		PARSE_INVALID_ASSIGNMENT_TARGET = 3006,
		PARSE_INTEGER_OUT_OF_RANGE = 3007,
		PARSE_EXPECTED_EXPRESSION = 3008,

		// Компилятор: ограничения байткода и ошибки семантической проверки.
		COMPILATION_FAILURE = 4000,
		COMPILATION_DUPLICATE_VARIABLE = 4001,
		COMPILATION_TOO_MANY_LOCAL_VARIABLES = 4002,
		COMPILATION_TOO_MANY_REGISTERS = 4003,
		COMPILATION_TOO_MANY_CONSTANTS = 4004,
		COMPILATION_FUNCTION_TOO_LARGE = 4005,
		COMPILATION_INTERNAL_EXPRESSION = 4006,
		COMPILATION_FUNCTION_MUST_BE_CALLED = 4007,
		COMPILATION_EXPRESSION_NOT_CALLABLE = 4008,
		COMPILATION_UNKNOWN_VARIABLE = 4009,
		COMPILATION_INVALID_ASSIGNMENT_TARGET = 4010,
		COMPILATION_INTERNAL_ASSIGNMENT = 4011,
		COMPILATION_ADDRESS_EXPECTS_VARIABLE = 4012,
		COMPILATION_BREAK_OUTSIDE_LOOP = 4013,
		COMPILATION_CONTINUE_OUTSIDE_LOOP = 4014,
		COMPILATION_DUPLICATE_LABEL = 4015,
		COMPILATION_UNKNOWN_LABEL = 4016,
		COMPILATION_INTERNAL_OPERATOR = 4017,
		COMPILATION_DUPLICATE_FUNCTION = 4018,

		// VM: ошибки скриптового выполнения и повреждённого байткода/состояния.
		VM_DIVISION_BY_ZERO = 5000,
		VM_SHIFT_OUT_OF_RANGE = 5001,
		VM_INTERNAL_OPERATOR_FAULT = 5002,
		VM_INTERNAL_CONSTANT_FAULT = 5003,
		VM_COMPARISON_EXPECTS_NUMBERS = 5004,
		VM_NEGATION_EXPECTS_NUMBER = 5005,
		VM_BITWISE_NOT_EXPECTS_INT = 5006,
		VM_OPERATOR_EXPECTS_NUMBERS = 5007,
		VM_BITWISE_OPERATOR_EXPECTS_INTS = 5008,
		VM_UNKNOWN_FUNCTION = 5009,
		VM_CALL_DEPTH_EXCEEDED = 5010,
		VM_NOTHING_TO_RESUME = 5011,
		VM_VALUE_NOT_REFERENCE = 5012,
		VM_DANGLING_REFERENCE = 5013,
		VM_CALL_STACK_UNDERFLOW = 5014,
		VM_FUNCTION_RAN_OUT_OF_INSTRUCTIONS = 5015,
		VM_INTERNAL_GLOBAL_FAULT = 5016,
		VM_INTERNAL_ARGUMENT_FAULT = 5017,
		VM_INTERNAL_NATIVE_FAULT = 5018,
		VM_INTERNAL_FUNCTION_FAULT = 5019,
		VM_RECURSION_DEPTH_EXCEEDED = 5020,
		VM_INDEX_MUST_BE_NON_NEGATIVE_INT = 5021,
		VM_STRING_INDEX_OUT_OF_RANGE = 5022,
		VM_VALUE_NOT_INDEXABLE = 5023,
		VM_TABLE_INDEX_OUT_OF_RANGE = 5024,
		VM_CYCLIC_TABLE_OWNERSHIP = 5025,
		VM_VALUE_HAS_NO_PROPERTIES = 5026,
		VM_ADDRESS_EXPECTS_TABLE_ELEMENT = 5027,
		VM_GLOBAL_INDEX_OUT_OF_RANGE = 5028,
		VM_CHARACTER_INDEX_EXPECTS_STRING = 5029,
		VM_UNKNOWN_TYPE = 5030,
		VM_UNKNOWN_INSTRUCTION = 5031,
		VM_REFERENCE_IDENTITY_EXHAUSTED = 5032,

		// Ошибки публичного API Engine.
		ENGINE_NATIVE_EMPTY = 6000,
		ENGINE_NATIVE_ALREADY_REGISTERED = 6001,
		ENGINE_BIND_EXPECTS_OBJECT = 6002,
		ENGINE_GLOBAL_SHADOWS_FUNCTION = 6003,
		ENGINE_LOAD_DURING_ACTIVE_CALL = 6004,
		ENGINE_UNKNOWN_FUNCTION = 6005,
		ENGINE_NOT_STOPPED = 6006,
		ENGINE_FACTORY_EMPTY = 6007,
		ENGINE_FUNCTION_INDEX_OUT_OF_RANGE = 6008,
		ENGINE_TABRESIZE_EXPECTS_TABLE = 6009,

		// Ошибки вызовов свойств и методов, задаваемых пользовательским объектом.
		OBJECT_UNKNOWN_PROPERTY = 7000,
		OBJECT_UNKNOWN_METHOD = 7001
	};


	struct Location
	{
		std::string source;
		std::string function;
		int line = 0;
		std::optional<std::size_t> instruction;
	};


	struct StackFrame
	{
		std::string source;
		std::string function;
		int line = 0;
		std::optional<std::size_t> instruction;
	};


	class Error
	{
	public:
		using Opt = std::optional<Error>;

	public:
		Error(Code code, std::string message, Location location = {},
			  std::vector<StackFrame> stack = {});
		
		Code code() const noexcept;
		const std::string& message() const noexcept;
		const Location& location() const noexcept;
		const std::vector<StackFrame>& stack() const noexcept;
		
	private:
		Code _code;
		std::string _message;
		Location _location;
		std::vector<StackFrame> _stack;
	};


	template<typename T>
	struct Result : public T
	{
		using Base = T;

		Error::Opt error;

		Result() = default;
		Result(T value, Error::Opt resultError = {})
			: T(std::move(value))
			, error(std::move(resultError))
		{
		}
	};


	class IHandler
	{
	public:
		using Ptr = std::shared_ptr<IHandler>;
		
	public:
		virtual ~IHandler() = default;
		virtual void Handle(const Error& error) noexcept = 0;
	};


}




namespace sme
{
// DEPRICATED все это нужно удалить
// Ошибка языка: разбор, компиляция или выполнение.
// Номер строки дописывается к тексту, чтобы хост видел место в скрипте.
// line == 0 значит «строка неизвестна» (ошибка на стороне хоста, не в исходнике).
class RError : public std::runtime_error {
public:
    RError(std::string message, int line = 0)
        : std::runtime_error(decorate(message, line))
        , _line(line) {}

    int line() const { return _line; }

private:
    int _line;

    static std::string decorate(const std::string& message, int line) {
        if (line <= 0) {
            return message;
        }
        return message + " (line " + std::to_string(line) + ")";
    }
};

}  // пространство имён sme
