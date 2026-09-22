#pragma once
#include "core/config/config_defaults.h"

namespace phpvpn::test {
// Explicit legacy sample for tests of existing contracts. Production defaults are empty.
inline std::string oneContractDocument() {
    auto dom = Json::parse(makeDefaultEditorDocument()).value();
    for (auto& [name, value] : dom.objectValue()) if (name == "communicationContracts")
        value = JsonValue::makeObject({{"10.99.99.3", JsonValue::makeObject({{"payloadKey",
            JsonValue::makeString("918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d")}})}});
    return Json::serializePretty(dom);
}
}
