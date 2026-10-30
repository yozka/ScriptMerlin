#include "sme/sme_engine.h"
#include "sme/smelib_default.h"
#include "sme/smelib_gs.h"
#include "sme/smelib_math.h"

#include <iostream>
#include <string_view>

// Учебный хост. Counter — объект игры: скрипт видит его как глобал counter
// и вызывает свойства value и метод add. Движок не знает про Counter заранее,
// только про виртуальные get/set/call.

class Counter : public sme::HostObject {
public:
    sme::Value get(sme::Engine& engine, std::string_view name) override {
        if (name == "value") {
            return sme::integer(value_);
        }
        return HostObject::get(engine, name);
    }

    void set(sme::Engine& engine, std::string_view name, const sme::Value& incoming) override {
        if (name == "value") {
            value_ = std::get<int>(incoming);
            return;
        }
        HostObject::set(engine, name, incoming);
    }

    sme::Value call(sme::Engine& engine, std::string_view name, std::span<const sme::Value> args) override {
        if (name == "add") {
            if (args.size() != 1) {
				return {};
            }
            value_ += std::get<int>(args[0]);
            return sme::integer(value_);
        }
        return HostObject::call(engine, name, args);
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
fn main() {
    counter.value = 10;
    counter.add(32);
    return counter.value + abs(-1);
}
)mln");

    const sme::Value result = engine.call("main");
    std::cout << engine.toString(result) << "\n";
    return std::get<int>(result) == 43 ? 0 : 1;
}
