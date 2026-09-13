#include "core/config/config_defaults.h"

namespace phpvpn {
std::string makeDefaultEditorDocument() { return std::string(kDefaultConfigDocument); }
WizardDocument makeNewWizardDocument() {
    WizardDocument document;
    document.fields.serverUrl = kWizardPlaceholderUrl;
    document.fields.serverSecret = kDefaultServerSecret;
    document.context.httpHeaders = kDefaultHttpHeaders;
    document.context.vpnCidr = *Ipv4Cidr::parse(kDefaultVpnCidr);
    document.dom = JsonValue::makeObject({{"masterSetup", JsonValue::makeObject({
        {"serverSecret", JsonValue::makeString(document.fields.serverSecret)},
        {"serverUrl", JsonValue::makeString(document.fields.serverUrl)},
        {"httpHeaders", JsonValue::makeString(kDefaultHttpHeaders)},
        {"trustAnyTlsCertificate", JsonValue::makeBool(true)},
        {"vpnCidr", JsonValue::makeString(kDefaultVpnCidr)},
        {"useDefaultServerSecretInWizard", JsonValue::makeBool(document.fields.useDefaultServerSecretInWizard)},
        {"autoDetectIpAddressInWizard", JsonValue::makeBool(document.fields.autoDetectIpAddressInWizard)},
        {"useDefaultAccessKeyInWizard", JsonValue::makeBool(document.fields.useDefaultAccessKeyInWizard)}
    })}});
    return document;
}
}
