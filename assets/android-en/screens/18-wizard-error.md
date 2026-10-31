# 18 · Verification error

[Open mockup](../18-wizard-error.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/18-wizard-error.png)

## Existing client

A secret mismatch can be associated with the secret field. A transport timeout is a different failure and must not be interpreted as an incorrect secret.

## Proposed Android behavior

Keep the user on the relevant setup step, preserve editable input and provide a route to correct it. Show HTTP details only when captured data is available.

## Mockup limitations and pitfalls

The mismatch and HTTP example are independent. The sample secret is intentionally fake, and no validation is performed.

## Sources

- `client/apps/tray/wizard_window.cpp:489–733`

## Related scenarios

[Server secret](14-wizard-secret.md) · [HTTP response](30-http-response.md)
