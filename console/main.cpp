#include "sme/sme_engine.h"
#include "sme/sme_error.h"
#include "sme/smelib_default.h"
#include "sme/smelib_gs.h"
#include "sme/smelib_math.h"

#include <iostream>
#include <string_view>

// Учебный хост. Counter — объект игры: скрипт видит его как глобал counter
// и вызывает свойства value и метод add. Движок не знает про Counter заранее,
// только про интерфейс IScriptObject.

class Counter : public sme::IScriptObject {
public:
    sme::Value GetProperty(sme::Engine& engine, std::string_view name) const override {
        if (name == "value") {
            return sme::Value::Integer(_value);
        }
        engine.RuntimeError(sme::error::Code::OBJECT_UNKNOWN_PROPERTY,
            "unknown property '" + std::string(name) + "'");
        return sme::Value::Null();
    }

    void SetProperty(sme::Engine& engine, std::string_view name, const sme::Value& incoming) override {
        if (name == "value") {
            _value = std::get<int>(incoming);
            return;
        }
        engine.RuntimeError(sme::error::Code::OBJECT_UNKNOWN_PROPERTY,
            "unknown property '" + std::string(name) + "'");
    }

    sme::Value Call(sme::Engine& engine, std::string_view name, sme::Arguments args) override {
        if (name == "add") {
            if (args.size() != 1) {
				return {};
            }
            _value += std::get<int>(args[0]);
            return sme::Value::Integer(_value);
        }
        engine.RuntimeError(sme::error::Code::OBJECT_UNKNOWN_METHOD,
            "unknown method '" + std::string(name) + "'");
        return sme::Value::Null();
    }

private:
    int _value = 0;
};

int main() {
    // Нативы и объекты привязываются до загрузки. loadSource сам прогоняет
    // препроцессор, лексер, парсер и компилятор.
    sme::Engine engine;
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.Bind("counter", std::make_shared<Counter>());
    const auto error = engine.LoadSource(R"mln(
func main() {
    counter.value = 10;
    counter.add(32);
    return counter.value + abs(-1);
}
)mln");
    if (error) {
        std::cerr << error->Message() << '\n';
        return 1;
    }

    auto callResult = engine.Call("main");
    if (callResult.error) {
        std::cerr << callResult.error->Message() << '\n';
        return 1;
    }
    const sme::Value& result = callResult;
    std::cout << result.ToString() << "\n";
    return std::get<int>(result) == 43 ? 0 : 1;
}
