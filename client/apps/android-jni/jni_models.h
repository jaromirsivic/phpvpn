// JNI bridge of the portable presentation models (plan-android-v001,
// "Přenosné modely"): ContractsModel, the tray ContractsSaveMachine and
// classifySaveResponse, PasswordKeyModel, ServerInfoModel and the
// configuration document check of WizardIpc, so Android keeps the exact
// behaviour of the desktop trays instead of a Kotlin rewrite.
//
// Every model is a native handle (jlong) that the Kotlin wrapper in
// phpvpn.android.bridge.jni owns and releases explicitly; calls on one handle
// are serialized by that wrapper (the models are single owner, UI thread
// objects). Secrets cross as char[] (UTF-16 without conversion on the Kotlin
// side) and every native temporary copy is wiped with secureWipe. Nothing
// crosses as jstring and nothing here logs.
#pragma once

#include <jni.h>

namespace phpvpn {

// RegisterNatives for phpvpn.android.bridge.jni.ModelNatives; called from
// JNI_OnLoad. False with a pending NoClassDefFoundError or NoSuchMethodError.
bool registerModelNatives(JNIEnv* env);

}  // namespace phpvpn
