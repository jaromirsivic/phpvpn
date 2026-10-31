package phpvpn.android.ui.screens

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import phpvpn.android.bridge.errors.ErrorTexts
import phpvpn.android.errors.ErrorStrings

/** The resource text of an error code; the received description is never shown. */
@Composable
fun errorMessage(code: String?): String = stringResource(ErrorStrings.resource(ErrorTexts.messageKey(code)))

/** The suggested action of a configuration or Wizard error (`WizardIpc::safeError`). */
@Composable
fun errorAction(code: String, field: String? = null): String = stringResource(ErrorStrings.resource(ErrorTexts.wizardActionKey(code, field)))
