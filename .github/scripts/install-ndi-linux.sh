#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${NDI_SDK_URL_LINUX:-}" ]]; then
  echo "NDI_SDK_URL_LINUX is not set" >&2
  exit 1
fi

archive="$RUNNER_TEMP/ndi-sdk-linux"
extract_dir="$RUNNER_TEMP/ndi-sdk-linux-extract"
mkdir -p "$extract_dir"

curl -L --retry 3 --retry-delay 5 "$NDI_SDK_URL_LINUX" -o "$archive"

if file "$archive" | grep -qi 'gzip compressed'; then
  tar -xzf "$archive" -C "$extract_dir"
elif file "$archive" | grep -qi 'bzip2 compressed'; then
  tar -xjf "$archive" -C "$extract_dir"
elif file "$archive" | grep -qi 'Zip archive'; then
  unzip -q "$archive" -d "$extract_dir"
else
  chmod +x "$archive"
  (cd "$extract_dir" && printf 'y\ny\n' | "$archive")
fi

# NDI's Linux download is a .tar.gz containing a self-extracting shell installer.
# Run it non-interactively after unpacking the outer archive.
installer="$(find "$extract_dir" -maxdepth 2 -type f -name 'Install_NDI_SDK*_Linux*.sh' -print -quit || true)"
existing_header="$(find "$extract_dir" -type f -path '*/include/Processing.NDI.Lib.h' -print -quit || true)"
if [[ -n "$installer" && -z "$existing_header" ]]; then
  chmod +x "$installer"
  (cd "$(dirname "$installer")" && printf 'y\ny\n' | PAGER=cat ./"$(basename "$installer")")
fi

sdk_root=""
while IFS= read -r header; do
  candidate="$(dirname "$(dirname "$header")")"
  first_lib="$(find "$candidate" -type f -name 'libndi.so*' -print -quit || true)"
  if [[ -n "$first_lib" ]]; then
    sdk_root="$candidate"
    break
  fi
done < <(find "$extract_dir" /usr/local /opt -type f -path '*/include/Processing.NDI.Lib.h' 2>/dev/null || true)

if [[ -z "$sdk_root" ]]; then
  echo "Could not locate Processing.NDI.Lib.h and libndi.so after extracting Linux NDI SDK" >&2
  find "$extract_dir" -maxdepth 4 -type f | sort >&2 || true
  exit 1
fi

# Normalize into the project-local layout CMake searches first. Preserve symlinks
# so versioned libndi.so chains remain intact.
case "$(uname -m)" in
  x86_64|amd64) ndi_arch_dir="x86_64-linux-gnu" ;;
  aarch64|arm64) ndi_arch_dir="aarch64-rpi4-linux-gnueabi" ;;
  armv7l) ndi_arch_dir="arm-rpi4-linux-gnueabihf" ;;
  *) ndi_arch_dir="" ;;
esac

lib_source_dir=""
if [[ -n "$ndi_arch_dir" && -d "$sdk_root/lib/$ndi_arch_dir" ]]; then
  lib_source_dir="$sdk_root/lib/$ndi_arch_dir"
elif [[ -d "$sdk_root/lib" ]]; then
  lib_source_dir="$sdk_root/lib"
fi

rm -rf ndi
mkdir -p ndi/include ndi/lib
cp -a "$sdk_root/include"/. ndi/include/
if [[ -z "$lib_source_dir" ]]; then
  echo "Could not locate an NDI library directory for $(uname -m) under $sdk_root/lib" >&2
  exit 1
fi
cp -a "$lib_source_dir"/libndi.so* ndi/lib/

if [[ ! -e ndi/lib/libndi.so ]]; then
  first_lib="$(find ndi/lib -type f -name 'libndi.so*' | sort | head -n1)"
  [[ -n "$first_lib" ]] || { echo "No libndi.so found in normalized SDK" >&2; exit 1; }
  ln -s "$(basename "$first_lib")" ndi/lib/libndi.so
fi

echo "NDI SDK normalized to $PWD/ndi"
