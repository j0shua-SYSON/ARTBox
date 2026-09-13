# Device verification

CI produces a Release arm64 iOS 15+ app with an empty entitlement dictionary
and an ad-hoc transport signature. Physical-device checks are recorded separately.
M0's manual gate was waived by the project owner; its launch and lifecycle
behavior have not been verified on a phone. The procedure remains available
for contributors and later milestone acceptance.

1. Obtain `ARTBox.ipa` and `build-info.json` from a passing `ios-build` run.
   Record the source commit and IPA SHA-256 before re-signing.
2. To rebuild on a Mac, run `python3 scripts/build.py ios`. Open the generated
   `build/ios/ARTBox.xcodeproj`, select ARTBox, your ordinary team/profile,
   a physical device, and Release. Enable `CODE_SIGNING_ALLOWED=YES` for the
   provisioning build. The bundle ID can be configured with `--bundle-id`.
3. Sign and install through ordinary development or Ad Hoc provisioning.
   Re-sign embedded frameworks with the same team when present. Launch from
   the home screen with the debugger detached. The transport signature alone
   is insufficient for installation. No JIT or private entitlements are needed.
4. Verify that the log console contains `ARTBox ready`. Background and foreground
   once, then terminate and relaunch. Record crashes, blank screens or duplicate
   startup messages on foregrounding.
5. Record the evidence in the milestone acceptance document:

```text
Commit and CI run URL:
Original IPA SHA-256:
Device model and iOS version:
Provisioning method (no secrets):
Home-screen launch with debugger detached:
Visible message:
Background / foreground / relaunch result:
Screenshot or observation reference:
Tester and date:
```

Do not commit certificates, profiles, private keys or device identifiers.
