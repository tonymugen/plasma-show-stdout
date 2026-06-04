#!/bin/sh
#
# Extract translatable strings into a gettext .pot template.
#
# Follows the KDE l10n convention: when run by KDE's "scripty" the environment
# supplies $XGETTEXT and $podir. When run by hand it falls back to a local
# `xgettext` and a ./po directory, so contributors can regenerate the template
# without the full KDE tooling:
#
#     ./Messages.sh
#
# The result is po/<domain>.pot. Merge it into existing translations with
# `msgmerge`, and compile a language with `msgfmt`.

set -eu

DOMAIN="plasma_applet_com.github.tonymugen.plasma-show-stdout"
BASEDIR="$(cd "$(dirname "$0")" && pwd)"
XGETTEXT="${XGETTEXT:-xgettext}"
podir="${podir:-$BASEDIR/po}"

mkdir -p "$podir"

# Run from the project root so the .pot records project-relative file locations.
cd "$BASEDIR"

# i18n() lives in QML (parsed as C++) and, for the backend error strings, in the
# C++ bridge. The keyword list covers the KI18n i18n* family.
# shellcheck disable=SC2046
"$XGETTEXT" \
	--from-code=UTF-8 \
	--language=C++ \
	--kde \
	--package-name="$DOMAIN" \
	--msgid-bugs-address="https://github.com/tonymugen/plasma-show-stdout/issues" \
	-ci18n \
	-ki18n:1 -ki18nc:1c,2 -ki18np:1,2 -ki18ncp:1c,2,3 \
	-ki18nd:2 -ki18ndc:2c,3 -ki18ndp:2,3 -ki18ndcp:2c,3,4 \
	-kki18n:1 -kki18nc:1c,2 -kki18np:1,2 -kki18ncp:1c,2,3 \
	-kI18N_NOOP:1 -kI18NC_NOOP:1c,2 \
	-o "$podir/$DOMAIN.pot" \
	$(find package plugin \
		-name '*.qml' -o -name '*.cpp' | LC_ALL=C sort)

echo "Wrote $podir/$DOMAIN.pot"
