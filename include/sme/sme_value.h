#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace sme 
{

	class HostObject;
	class Table;
	class Value;

	using Handle = std::shared_ptr<HostObject>;
	using TableHandle = std::shared_ptr<Table>;
	using SharedString = std::shared_ptr<std::string>;
	using SharedValue = std::shared_ptr<Value>;

	// Вид значения. Invalid зарезервирован для пустого shared_ptr.
	enum class Type
	{
		Null,
		Int,
		Float,
		Bool,
		String,
		Table,
		Ref,
		Object,
		Pointer,
		Invalid
	};

	class Table : public std::vector<Value>
	{
	public:
		using std::vector<Value>::vector;
	};

	struct LocalReference
	{
		uint32_t frame = 0;
		size_t frameGeneration = 0;
		uint16_t slot = 0;
		uint32_t identity = 0;
		
		friend bool operator==(const LocalReference&, const LocalReference&) = default;
	};

	struct GlobalReference
	{
		uint32_t slot = 0;
		uint32_t identity = 0;
		
		friend bool operator==(const GlobalReference&, const GlobalReference&) = default;
	};

	struct IndexedReference
	{
		TableHandle container;
		uint32_t index = 0;
		uint32_t identity = 0;
		
		friend bool operator==(const IndexedReference&, const IndexedReference&) = default;
	};

	// Строки и числа хранятся прямо в Value. Таблицы доступны как vector<Value>,
	// а SharedValue разделяет владение отдельным значением между копиями.
	// bool скрипт сам не производит (true/false компилируются в int 0/1),
	// но хост может передать настоящий bool.
	using ValueStorage = std::variant<std::monostate, int, float, bool, Handle, std::nullptr_t, std::string,
	SharedString, TableHandle, SharedValue, LocalReference, GlobalReference, IndexedReference>;

	class Value : public ValueStorage {
	public:
		using Base = ValueStorage;
		using Base::Base;
		using Base::operator=;
		
	public:
		static Value Integer(int value);
		static Value Real(float value);
		static Value Boolean(bool value);
		static Value String(std::string value);
		static Value Null();
		static Value Pointer();
		static Value Shared(Value value);
		
	public:
		bool IsNull() const;
		bool IsReference() const;
		bool IsInteger() const;
		bool IsFloat() const;
		bool IsBool() const;
		bool IsString() const;
		bool IsTable() const;
		bool IsObject() const;
		bool IsPointer() const;
		bool IsInvalid() const;
		std::optional<float> AsNumber() const;
		float AsFloat(const float def = 0.0f) const;
		int AsInteger(const int def = 0) const;
		std::string AsString(const std::string& def = {}) const;
		size_t Length() const;
		
		Type TypeOf() const;
		// Коды typeof сохранены для совместимости с другими скриптами.
		int32_t TypeCode() const;
		std::string_view TypeName() const;
		
		std::string ToString() const;
	};



}

namespace std
{
	template <>
	struct variant_size<sme::Value> : variant_size<sme::Value::Base> {};

	template <size_t Index>
	struct variant_alternative<Index, sme::Value> : variant_alternative<Index, sme::Value::Base> {};
}


// Индекс типа в Value. Число совпадает с порядком в variant выше.
#define VT_NULL 0
#define VT_INT  1
#define VT_FLOAT 2
#define VT_BOOL 3
#define VT_HANDLE 4
#define VT_PTR  5
#define VT_STRING 6
#define VT_SHARED_STRING 7
#define VT_TABLE 8
#define VT_SHARED_VALUE 9
#define VT_LOCAL_REF 10
#define VT_GLOBAL_REF 11
#define VT_INDEXED_REF 12

#define VT_CHECK(index, type) \
	static_assert(std::is_same_v<std::variant_alternative_t<(index), sme::Value>, type>, \
		#index " is not " #type " in Value")

VT_CHECK(VT_NULL, std::monostate);
VT_CHECK(VT_INT, int);
VT_CHECK(VT_FLOAT, float);
VT_CHECK(VT_BOOL, bool);
VT_CHECK(VT_HANDLE, sme::Handle);
VT_CHECK(VT_PTR, std::nullptr_t);
VT_CHECK(VT_STRING, std::string);
VT_CHECK(VT_SHARED_STRING, sme::SharedString);
VT_CHECK(VT_TABLE, sme::TableHandle);
VT_CHECK(VT_SHARED_VALUE, sme::SharedValue);
VT_CHECK(VT_LOCAL_REF, sme::LocalReference);
VT_CHECK(VT_GLOBAL_REF, sme::GlobalReference);
VT_CHECK(VT_INDEXED_REF, sme::IndexedReference);
static_assert(std::variant_size_v<sme::Value> == 13, "Value gained or lost an alternative");
