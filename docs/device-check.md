# Device verification

CI produces a Release arm64 iOS 15+ app with an empty entitlement dictionary
and an ad-hoc transport signature. Physical-device checks are recorded separately.
The project owner waived physical-device checks for all milestones. This
optional procedure remains available to contributors; it does not block
milestone completion. M0-M3 now pass on an existing jailbroken iPhone; see
[M1](acceptance/m1.md), [M2](acceptance/m2.md) and [M3](acceptance/m3.md)
acceptance. Ordinary stock-device provisioning remains unverified.

1. Obtain `ARTBox.ipa` and `build-info.json` from a passing `ios-build` run.
   Record the source commit and IPA SHA-256 before re-signing.
2. To rebuild M1 on a Mac, run `python3 scripts/build.py ios --with-guest`. Open the generated
   `build/ios/ARTBox.xcodeproj`, select ARTBox, your ordinary team/profile,
   a physical device, and Release. Enable `CODE_SIGNING_ALLOWED=YES` for the
   provisioning build. The bundle ID can be configured with `--bundle-id`.
3. Sign and install through ordinary development or Ad Hoc provisioning.
   Re-sign embedded frameworks with the same team when present. Launch from
   the home screen with the debugger detached. The transport signature alone
   is insufficient for installation. No JIT or private entitlements are needed.
4. Verify that the log console contains `ARTBox ready`. Background and foreground
   once, then terminate and relaunch. Record crashes, blank screens or duplicate
   startup messages on foregrounding. For M1, both Converted and Wrapped should
   also print hello and `exit 0; five syscalls verified`.
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

The diagnostic app writes console output, stdout and stderr to
`Library/Caches/ARTBox/launch.log` in its own data container. Each launch replaces
the previous log. Retrieve it before relaunching when startup exits without a
crash report; the native acceptance harness can call `_Exit` before UIKit displays
its last queued message. Log creation is best effort and needs no entitlement.
Review logs for container paths or other private data before sharing them.

For the integrated M2 build, use `ARTBox-M2-ipa` from a passing `host-tests`
run. It includes four additional signed frameworks and the same native suite
used on macOS. The expected final console message is `M2: suite passed`, after
its JSON result. On a Mac, the equivalent build command is
`python3 scripts/build.py ios --with-guest --m2-evidence PATH_TO_STARTUP_ARTIFACT`;
the artifact must be from the exact checked-out project revision. All embedded
frameworks need ordinary provisioning signatures for installation.

For M3, use `ARTBox-M3-ipa` from a passing `host-tests` run. This is a separate
ART diagnostic build with 15 embedded frameworks; sign all of them and the app
with the same ordinary team/profile. Expected guest output includes
`hello from ARTBox ART`, followed by `ART: DEX and lifecycle checks passed`.
The equivalent Mac build command is
`python3 scripts/build.py ios --m3-evidence PATH_TO_MERGED_ART_ARTIFACTS`.
See [M3 acceptance](acceptance/m3.md) for the required same-run inputs and
automated evidence. On the existing jailbroken iPhone 6s Plus running iOS 15.8.5,
M2 passes at `31c9a4a` and ART passes at `2970c38`, including visible hello,
managed checks, shutdown, background/foreground and cold home-icon relaunch.
No debugger is attached; all installed code images match CI. The acceptance
records retain the earlier reservation and CRC failures and their fixes.
