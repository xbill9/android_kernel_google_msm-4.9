#!/bin/bash
# Make the core GApps zip: MindTheGapps with only Play services, the Play Store and what they need,
# signed with the ROM's key so recovery installs it without asking. Usage:
#   core-gapps.sh MindTheGapps-....zip [out.zip]      (default out: <name>-core.zip next to it)
# Dropped: the Google app (Velvet, VelvetTitan), speech services, TalkBack, backup restore, Digital
# Wellbeing, the Google setup wizard (MindTheGapps' own list for phones short on space), Family
# Link, the Android Auto stub, contacts and calendar sync and Exchange mail sync. Everything else
# comes from the Play Store.
# The MindTheGapps installer copies what is in the zip and writes its addon.d list from it.
set -euo pipefail
IN=$(readlink -f "$1")
OUT=$(readlink -f "${2:-${IN%.zip}-core.zip}")
T=${ANDROID_TREE:-$HOME/android/lineage-24.0}
KEY=$T/build/make/target/product/security/testkey
JAVA=$(ls -d "$T"/prebuilts/jdk/jdk21/linux-x86/bin/java)
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT

cp "$IN" "$W/in.zip"
zip -q -d "$W/in.zip" 'META-INF/com/android/otacert' 'META-INF/CERT.*' 'META-INF/MANIFEST.MF' \
  'system/product/priv-app/Velvet/*' 'system/product/priv-app/VelvetTitan/*' \
  'system/product/app/SpeechServicesByGoogle/*' 'system/product/app/talkback/*' \
  'system/product/priv-app/GoogleRestore/*' 'system/product/priv-app/Wellbeing/*' \
  'system/system_ext/priv-app/SetupWizard/*' 'system/product/overlay/GmsSetupWizardOverlay.apk' \
  'system/product/priv-app/FamilyLinkParentalControls/*' 'system/product/priv-app/AndroidAutoStub/*' \
  'system/product/app/GoogleCalendarSyncAdapter/*' 'system/product/app/GoogleContactsSyncAdapter/*' \
  'system/product/app/PrebuiltExchange3Google/*' \
  >/dev/null 2>&1 || true
# -w signs the whole file, the signature recovery checks
"$JAVA" -Djava.library.path="$T/out/host/linux-x86/lib64" -jar "$T/out/host/linux-x86/framework/signapk.jar" \
  -w "$KEY.x509.pem" "$KEY.pk8" "$W/in.zip" "$OUT.part"
mv "$OUT.part" "$OUT"
unzip -l "$OUT" | awk -v o="$OUT" 'END {printf "%s: %d files, %.0f MB unpacked\n", o, $2, $1/1048576}'
