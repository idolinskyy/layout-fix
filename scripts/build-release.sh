#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
version_override="${VERSION:-}"
version=""
architecture="$(uname -m)"
build_dir="${BUILD_DIR:-$root_dir/build/release}"
dist_dir="${DIST_DIR:-$root_dir/dist}"
install_prefix="/usr"
qt_prefix="${QT_PREFIX:-}"
jobs="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2')}"

for dictionary in en_US uk_UA ru_RU; do
  for extension in dic aff; do
    dictionary_file="/usr/share/hunspell/$dictionary.$extension"
    if [[ ! -f "$dictionary_file" ]]; then
      printf 'Missing Hunspell dictionary: %s\n' "$dictionary_file" >&2
      exit 1
    fi
  done
done

cmake_args=(
  -S "$root_dir"
  -B "$build_dir"
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_INSTALL_PREFIX="$install_prefix"
  -DCMAKE_INSTALL_LIBDIR=lib
  -DCMAKE_INSTALL_SYSCONFDIR=/etc
  -DLAYOUT_FIX_REQUIRE_HUNSPELL=ON
  -DLAYOUT_FIX_REQUIRE_QT_TRAY=ON
)
if [[ -n "$version_override" ]]; then
  cmake_args+=("-DVERSION=$version_override")
fi

if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
  cmake_args+=("-DCMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH")
elif [[ -n "$qt_prefix" && -f "$qt_prefix/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
  cmake_args+=("-DCMAKE_PREFIX_PATH=$qt_prefix")
fi

cmake "${cmake_args[@]}"
version="$(<"$build_dir/layout-fix-version.txt")"
if [[ -z "$version" || ! "$version" =~ ^[A-Za-z0-9][A-Za-z0-9.+~:-]*$ ]]; then
  printf 'Invalid or missing VERSION in CMake cache: %s\n' "$version" >&2
  exit 2
fi
cmake --build "$build_dir" --parallel "$jobs"
ctest --test-dir "$build_dir" --output-on-failure

stage_dir="$(mktemp -d)"
trap 'rm -rf "$stage_dir"' EXIT
DESTDIR="$stage_dir" cmake --install "$build_dir" --prefix "$install_prefix"

docs_dir="$stage_dir$install_prefix/share/layout-fix"
mkdir -p "$docs_dir"
cp "$root_dir/README.md" "$docs_dir/README.md"
cat > "$docs_dir/RUNTIME-DEPENDENCIES.txt" <<'EOF'
This build targets Linux/X11 with Cinnamon.
It requires Qt 6 Widgets (system libqt6widgets6t64/libqt6widgets6 and qt6-qpa-plugins),
Hunspell runtime and en_US, uk_UA, and ru_RU dictionaries, plus the X11, XTest,
xkbcommon, GLib, and AT-SPI (libatspi2.0-0) runtime libraries.
Password-field detection uses the session accessibility bus. The daemon still
starts when that bus is absent; correction in password fields stays active then.
If built against a custom Qt installation outside the system library path,
set QT_PREFIX to its gcc_64 directory before starting layout-fix.
EOF

mkdir -p "$dist_dir"
artifact="$dist_dir/layout-fix-$version-linux-$architecture.tar.gz"
tar_entries=(usr)
if [[ -d "$stage_dir/etc" ]]; then
  tar_entries+=(etc)
fi
tar -C "$stage_dir" --sort=name --mtime='@0' --owner=0 --group=0 \
  --numeric-owner -cf - "${tar_entries[@]}" | gzip -n > "$artifact"
sha256sum "$artifact" > "$artifact.sha256"

deb_architecture="$(dpkg --print-architecture)"
deb_artifact="$dist_dir/layout-fix_${version}_${deb_architecture}.deb"
cpack --config "$build_dir/CPackConfig.cmake" -G DEB -B "$dist_dir" \
  -D "CPACK_PACKAGE_FILE_NAME=layout-fix_${version}_${deb_architecture}" \
  -D "CPACK_DEBIAN_PACKAGE_ARCHITECTURE=$deb_architecture"
sha256sum "$deb_artifact" > "$deb_artifact.sha256"

printf 'Release archive: %s\n' "$artifact"
printf 'SHA-256: %s\n' "$(cut -d ' ' -f 1 "$artifact.sha256")"
printf 'Debian package: %s\n' "$deb_artifact"
printf 'SHA-256: %s\n' "$(cut -d ' ' -f 1 "$deb_artifact.sha256")"