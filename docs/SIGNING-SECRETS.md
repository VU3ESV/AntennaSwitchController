# Release signing secrets (Antenna Switch Controller)

The release workflow embeds the AntennaSwitch ExtensionKit extension into
`Antenna Switch Controller.app`, signs it with Developer ID, and notarizes the DMG — so an
installed app registers its extension for the Amateur Radio Suite to host (see the suite's
`docs/EXTENSIONKIT.md`).

This only happens when these **GitHub Actions secrets** are set on this repo
(*Settings → Secrets and variables → Actions*). Without them, the release still builds but is
**ad-hoc signed and not notarized** — fine to smoke-test, but its extension won't register on
another Mac. (Only the macOS app is signed; the ESP8266 firmware `.bin` is unaffected.)

## Secrets

| Secret | What it is |
|--------|------------|
| `MACOS_CERT_P12_BASE64` | AntennaSwitch's **Developer ID Application** cert+key exported as a `.p12`, base64-encoded |
| `MACOS_CERT_PASSWORD` | the password set when exporting that `.p12` |
| `KEYCHAIN_PASSWORD` | any value — password for the throwaway CI keychain |
| `NOTARY_APPLE_ID` | Apple ID email used for notarization |
| `NOTARY_TEAM_ID` | the team id (`Y6FT52BKDA`) |
| `NOTARY_PASSWORD` | an **app-specific password** for that Apple ID ([appleid.apple.com](https://appleid.apple.com) → Sign-In & Security → App-Specific Passwords) |

> This repo uses its **own** Developer ID cert (one per app, all under the same team), so revoking
> it won't affect the other apps. In the fresh CI keychain only this cert is present, so signing
> selects it unambiguously by name.

## Exporting the `.p12` (one time, on the Mac that has the cert)

Keychain Access → **login** keychain → **My Certificates** → find AntennaSwitch's
`Developer ID Application` (its private key is named e.g. `ARS ASW`) → right-click →
**Export…** → `.p12`, set a password (that's `MACOS_CERT_PASSWORD`). Then:

```sh
base64 -i AntennaSwitch.p12 | pbcopy   # paste as MACOS_CERT_P12_BASE64
```

(Tip: the AntennaSwitch cert's SHA-1 is `38580843BD108548B1064080B269962D6DFA93CD` if you need to
pick it among several in Keychain Access.)

## Notarization alternative (App Store Connect API key)

Instead of Apple ID + app-specific password you may prefer an ASC API key (no 2FA/expiry issues).
If you want that, say so and the workflow can switch to `--key/--key-id/--issuer` with
`NOTARY_API_KEY` / `NOTARY_API_KEY_ID` / `NOTARY_API_ISSUER` secrets.
