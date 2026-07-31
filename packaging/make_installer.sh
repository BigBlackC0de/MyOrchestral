#!/usr/bin/env bash
#
# Builds MyOrchestral for macOS and packages it as a .dmg containing a .pkg
# installer.
#
# Why a .pkg inside a .dmg, rather than drag-and-drop:
# a plugin has to land in two different system folders (Components for the AU,
# VST3 for the VST3), and Logic only sees an Audio Unit that is in one of the
# two Components directories. A drag-and-drop DMG cannot do that; an installer
# can, and it is what every commercial plugin ships.
#
# Usage:
#     ./packaging/make_installer.sh                 # unsigned, for your own Mac
#     SIGN_ID="Developer ID Application: NAME (TEAM)" \
#     INSTALLER_ID="Developer ID Installer: NAME (TEAM)" \
#     ./packaging/make_installer.sh                 # signed, for distribution
#
# Notarisation (needed only if the DMG will be downloaded on another Mac):
#     NOTARY_PROFILE=my-profile ./packaging/make_installer.sh
#
# See docs/05-installeur-macos.md.

set -euo pipefail

# --------------------------------------------------------------------------
# Configuration
# --------------------------------------------------------------------------
PRODUCT_NAME="MyOrchestral"
BUNDLE_PREFIX="com.myorchestral"
VERSION="${VERSION:-0.1.0}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-release"
STAGE_DIR="${ROOT_DIR}/build-package"
ARTEFACTS="${BUILD_DIR}/plugin/${PRODUCT_NAME}_artefacts/Release"

SIGN_ID="${SIGN_ID:-}"
INSTALLER_ID="${INSTALLER_ID:-}"
NOTARY_PROFILE="${NOTARY_PROFILE:-}"

if [[ "$(uname)" != "Darwin" ]]; then
    echo "error: this script builds a macOS installer and must run on macOS." >&2
    exit 1
fi

echo "==> MyOrchestral ${VERSION} — macOS installer"

# --------------------------------------------------------------------------
# 1. Build, universal
# --------------------------------------------------------------------------
echo "==> Building (arm64 + x86_64)"

cmake -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
      -DMOE_BUILD_TESTS=OFF \
      -DMOE_BUILD_TOOLS=OFF

cmake --build "${BUILD_DIR}" --config Release --parallel

AU_BUNDLE="${ARTEFACTS}/AU/${PRODUCT_NAME}.component"
VST3_BUNDLE="${ARTEFACTS}/VST3/${PRODUCT_NAME}.vst3"
APP_BUNDLE="${ARTEFACTS}/Standalone/${PRODUCT_NAME}.app"

for bundle in "${AU_BUNDLE}" "${VST3_BUNDLE}"; do
    if [[ ! -d "${bundle}" ]]; then
        echo "error: expected build output missing: ${bundle}" >&2
        exit 1
    fi
done

# Fail loudly rather than shipping an Intel-only or arm-only binary: a plugin
# that silently refuses to load on the other architecture is a miserable bug to
# diagnose from a user's report.
echo "==> Verifying the binaries are universal"
for bundle in "${AU_BUNDLE}" "${VST3_BUNDLE}"; do
    binary="${bundle}/Contents/MacOS/${PRODUCT_NAME}"
    archs="$(lipo -archs "${binary}")"
    echo "    $(basename "${bundle}"): ${archs}"
    [[ "${archs}" == *arm64* ]]  || { echo "error: missing arm64 in ${bundle}" >&2; exit 1; }
    [[ "${archs}" == *x86_64* ]] || { echo "error: missing x86_64 in ${bundle}" >&2; exit 1; }
done

# --------------------------------------------------------------------------
# 2. Code signing (optional)
# --------------------------------------------------------------------------
if [[ -n "${SIGN_ID}" ]]; then
    echo "==> Signing with: ${SIGN_ID}"
    for bundle in "${AU_BUNDLE}" "${VST3_BUNDLE}" "${APP_BUNDLE}"; do
        [[ -d "${bundle}" ]] || continue
        codesign --force --deep --options runtime --timestamp \
                 --sign "${SIGN_ID}" "${bundle}"
        codesign --verify --strict --verbose=2 "${bundle}"
    done
else
    echo "==> Not signing (SIGN_ID unset)."
    echo "    Fine for your own Mac. A DMG that travels to another machine"
    echo "    needs signing and notarisation, or Gatekeeper will block it."
fi

# --------------------------------------------------------------------------
# 3. Component packages
# --------------------------------------------------------------------------
echo "==> Building component packages"

rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}/pkgs" "${STAGE_DIR}/root-au" "${STAGE_DIR}/root-vst3" "${STAGE_DIR}/dmg"

cp -R "${AU_BUNDLE}"   "${STAGE_DIR}/root-au/"
cp -R "${VST3_BUNDLE}" "${STAGE_DIR}/root-vst3/"

# System-wide install paths. Logic scans both /Library and ~/Library for
# Components; /Library is the one that works for every user account, at the
# cost of an admin prompt during install.
pkgbuild --root "${STAGE_DIR}/root-au" \
         --identifier "${BUNDLE_PREFIX}.au" \
         --version "${VERSION}" \
         --install-location "/Library/Audio/Plug-Ins/Components" \
         "${STAGE_DIR}/pkgs/${PRODUCT_NAME}-AU.pkg"

pkgbuild --root "${STAGE_DIR}/root-vst3" \
         --identifier "${BUNDLE_PREFIX}.vst3" \
         --version "${VERSION}" \
         --install-location "/Library/Audio/Plug-Ins/VST3" \
         "${STAGE_DIR}/pkgs/${PRODUCT_NAME}-VST3.pkg"

# --------------------------------------------------------------------------
# 4. Distribution package
# --------------------------------------------------------------------------
echo "==> Building the distribution package"

cat > "${STAGE_DIR}/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>${PRODUCT_NAME} ${VERSION}</title>
    <organization>${BUNDLE_PREFIX}</organization>
    <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>

    <welcome    file="welcome.html"    mime-type="text/html"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>

    <!-- macOS 12 is the deployment target; refuse to install below it rather
         than let the plugin fail to load with no explanation. -->
    <volume-check>
        <allowed-os-versions><os-version min="12.0"/></allowed-os-versions>
    </volume-check>

    <choices-outline>
        <line choice="au"/>
        <line choice="vst3"/>
    </choices-outline>

    <choice id="au" title="Audio Unit (Logic Pro, GarageBand)" visible="true">
        <pkg-ref id="${BUNDLE_PREFIX}.au"/>
    </choice>
    <choice id="vst3" title="VST3 (Ableton Live, Cubase, Reaper, Studio One)" visible="true">
        <pkg-ref id="${BUNDLE_PREFIX}.vst3"/>
    </choice>

    <pkg-ref id="${BUNDLE_PREFIX}.au"   version="${VERSION}">${PRODUCT_NAME}-AU.pkg</pkg-ref>
    <pkg-ref id="${BUNDLE_PREFIX}.vst3" version="${VERSION}">${PRODUCT_NAME}-VST3.pkg</pkg-ref>
</installer-gui-script>
XML

cat > "${STAGE_DIR}/welcome.html" <<'HTML'
<html><body style="font-family:-apple-system;font-size:13px">
<p>This installs the <b>MyOrchestral</b> symphonic sampler.</p>
<p>Choose the formats you need:</p>
<ul>
  <li><b>Audio Unit</b> — Logic Pro, GarageBand, MainStage</li>
  <li><b>VST3</b> — Ableton Live, Cubase, Reaper, Studio One, Bitwig</li>
</ul>
<p>Both are installed system-wide, so administrator authorisation is
required.</p>
<p><b>The plugin does not include sample banks.</b> It plays SFZ libraries,
which are installed separately. See the project documentation.</p>
</body></html>
HTML

cat > "${STAGE_DIR}/conclusion.html" <<'HTML'
<html><body style="font-family:-apple-system;font-size:13px">
<p>Installed.</p>
<p><b>Logic Pro</b> validates new Audio Units at launch, so quit and reopen
Logic. MyOrchestral then appears under
<i>Instrument &rarr; AU Instruments &rarr; MyOrchestral</i>.</p>
<p>If it does not appear, open Terminal and run:</p>
<p style="font-family:Menlo,monospace;font-size:11px">
auval -v aumu Morc Myor</p>
<p>That prints exactly why it was rejected.</p>
<p>Next step: load an SFZ bank with the <i>Load bank...</i> button.</p>
</body></html>
HTML

PKG_PATH="${STAGE_DIR}/dmg/${PRODUCT_NAME}-${VERSION}.pkg"

if [[ -n "${INSTALLER_ID}" ]]; then
    productbuild --distribution "${STAGE_DIR}/distribution.xml" \
                 --package-path "${STAGE_DIR}/pkgs" \
                 --resources "${STAGE_DIR}" \
                 --sign "${INSTALLER_ID}" \
                 "${PKG_PATH}"
else
    productbuild --distribution "${STAGE_DIR}/distribution.xml" \
                 --package-path "${STAGE_DIR}/pkgs" \
                 --resources "${STAGE_DIR}" \
                 "${PKG_PATH}"
fi

# --------------------------------------------------------------------------
# 5. Notarisation (optional)
# --------------------------------------------------------------------------
if [[ -n "${NOTARY_PROFILE}" ]]; then
    echo "==> Notarising (this takes a few minutes)"
    xcrun notarytool submit "${PKG_PATH}" --keychain-profile "${NOTARY_PROFILE}" --wait
    xcrun stapler staple "${PKG_PATH}"
else
    echo "==> Not notarising (NOTARY_PROFILE unset)."
fi

# --------------------------------------------------------------------------
# 6. Disk image
# --------------------------------------------------------------------------
echo "==> Building the disk image"

cp "${ROOT_DIR}/docs/00-audit-samples.md" "${STAGE_DIR}/dmg/READ ME - samples.md" 2>/dev/null || true

DMG_PATH="${ROOT_DIR}/${PRODUCT_NAME}-${VERSION}.dmg"
rm -f "${DMG_PATH}"

hdiutil create -volname "${PRODUCT_NAME} ${VERSION}" \
               -srcfolder "${STAGE_DIR}/dmg" \
               -ov -format UDZO \
               "${DMG_PATH}"

if [[ -n "${SIGN_ID}" ]]; then
    codesign --force --sign "${SIGN_ID}" "${DMG_PATH}"
fi

echo
echo "==> Done: ${DMG_PATH}"
echo
if [[ -z "${SIGN_ID}" || -z "${NOTARY_PROFILE}" ]]; then
    echo "    This DMG is not signed and notarised. It installs fine on this Mac."
    echo "    On another Mac it will be quarantined by Gatekeeper — the user would"
    echo "    have to right-click > Open, or run:"
    echo "        xattr -dr com.apple.quarantine /path/to/${PRODUCT_NAME}-${VERSION}.dmg"
    echo "    For real distribution, set SIGN_ID, INSTALLER_ID and NOTARY_PROFILE."
fi
