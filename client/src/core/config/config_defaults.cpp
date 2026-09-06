#include "core/config/config_defaults.h"

namespace phpvpn {
std::string makeDefaultEditorDocument() { return std::string(kDefaultConfigDocument); }
WizardDocument makeNewWizardDocument() {
    WizardDocument document;
    document.fields.serverUrl = kWizardPlaceholderUrl;
    document.fields.serverSecret = kDefaultServerSecret;
    document.context.vpnCidr = *Ipv4Cidr::parse(kDefaultVpnCidr);
    document.dom = JsonValue::makeObject({{"masterSetup", JsonValue::makeObject({
        {"serverUrl", JsonValue::makeString(document.fields.serverUrl)},
        {"trustAnyTlsCertificate", JsonValue::makeBool(true)},
        {"serverSecret", JsonValue::makeString(document.fields.serverSecret)},
        {"vpnCidr", JsonValue::makeString(kDefaultVpnCidr)}
    })}});
    return document;
}
}
