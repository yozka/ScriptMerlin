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
    sme::Value GetProperty(sme::Engine&, std::string_view name) const override {
        if (name == "value") {
            return sme::Value::Integer(value_);
        }
        throw sme::RError("unknown property '" + std::string(name) + "'");
    }

    void SetProperty(sme::Engine&, std::string_view name, const sme::Value& incoming) override {
        if (name == "value") {
            value_ = std::get<int>(incoming);
            return;
        }
        throw sme::RError("unknown property '" + std::string(name) + "'");
    }

    sme::Value Call(sme::Engine&, std::string_view name, std::span<const sme::Value> args) override {
        if (name == "add") {
            if (args.size() != 1) {
				return {};
            }
            value_ += std::get<int>(args[0]);
            return sme::Value::Integer(value_);
        }
        throw sme::RError("unknown method '" + std::string(name) + "'");
    }

private:
    int value_ = 0;
};

int main() {
    // Нативы и объекты привязываются до загрузки. loadSource сам прогоняет
    // препроцессор, лексер, парсер и компилятор.
    sme::Engine engine;
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.bind("counter", std::make_shared<Counter>());
    engine.loadSource(R"mln(
func main() {
    counter.value = 10;
    counter.add(32);
    return counter.value + abs(-1);
}
)mln");

    const sme::Value result = engine.call("main");
    std::cout << result.ToString() << "\n";
    return std::get<int>(result) == 43 ? 0 : 1;
}
