#pragma once

#include "sme/sme_engine.h"

namespace sme::library
{

	// Нативы len/str/tab/type, строки, таблицы, typeof/sizeof, call и println.
	void InstallDefault(Engine& engine);

}
