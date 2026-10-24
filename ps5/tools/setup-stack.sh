#!/usr/bin/env bash
# Luigi's Mansion for the PlayStation 5 - the PS5 build stack beside this repository.
#
#   ps5/tools/setup-stack.sh        (Ubuntu 24.04 or WSL2; safe to run again)
#
# Lays out the folder this repository is in the way the build expects:
#
#   ../PS5_PayloadSDK   Mihawk's payload SDK fork (ps5/tools/setup-sdk.sh installs its pin)
#   ../PS5_Vulkan       Mihawk's RADV port: the link recipe, the CRT, the AGC stubs and
#                       ps5-native-tool (built here)
#
# and installs the host packages and LLVM 22 (apt.llvm.org): RADV built with an older clang
# does not link into a title.
#
# RADV itself (PS5_Vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a) and the runtime
# module (PS5_Vulkan/runtime/libc.prx) come from PS5_Vulkan's own build (its README: tools/build-radv.sh
# from PS5_Mesa, make libc). Already have them, e.g. from a PS5 title builder's kit? Set
#   RADV_ARCHIVE=/path/to/libvulkan_radeon.ps5.a  LIBC_PRX=/path/to/libc.prx
# and they are copied into place (libc.prx is checked against PS5_Vulkan's published SHA-256).
#
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
stack=$(dirname "$root")
export GIT_TERMINAL_PROMPT=0

# Host packages
need=()
for package in build-essential cmake ninja-build python3 python3-pil python3-numpy python3-pyelftools \
        git curl wget rsync zip unzip xz-utils pkg-config zlib1g-dev fonts-dejavu-core meson \
        mingw-w64 lsb-release software-properties-common gnupg; do
    dpkg -s "$package" > /dev/null 2>&1 || need+=("$package")
done
if ((${#need[@]})); then
    sudo DEBIAN_FRONTEND=noninteractive apt-get update -qq
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${need[@]}" > /dev/null
fi
# LLVM 22
if [[ ! -x /usr/lib/llvm-22/bin/clang ]]; then
    wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh
    sudo bash /tmp/llvm.sh 22 all > /dev/null
fi
for tool in clang clang++ ld.lld llvm-ar llvm-nm llvm-ranlib llvm-objcopy llvm-strip llvm-symbolizer llvm-config lld; do
    [[ -e /usr/local/bin/$tool ]] || sudo ln -sf "/usr/lib/llvm-22/bin/$tool" "/usr/local/bin/$tool"
done
clang --version | head -1
# Rust, for nodtool (the disc converter; ps5/tools/prepare-game.sh and the builder's kit)
if [[ ! -x $HOME/.cargo/bin/cargo ]]; then
    curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal > /dev/null
fi

# Mihawk's stack
clone() {
    local name=$1
    [[ -d $stack/$name/.git ]] || git -C "$stack" clone -q --filter=blob:none "https://github.com/mihawk-99/$name.git"
}
clone PS5_PayloadSDK
clone PS5_Vulkan
vulkan=$stack/PS5_Vulkan
native=$vulkan/tooling/native
if [[ ! -x $vulkan/build/host/ps5-native-tool ]]; then
    mkdir -p "$vulkan/build/host"
    clang++ -std=c++20 -O2 "$native/native_app_builder.cpp" "$native/self_container.cpp" \
        "$native/elf_object.cpp" "$native/sce_module_writer.cpp" -lz -o "$vulkan/build/host/ps5-native-tool"
fi
if [[ -n ${LIBC_PRX:-} ]]; then
    cp -u "$LIBC_PRX" "$vulkan/runtime/libc.prx"
fi
if [[ -f $vulkan/runtime/libc.prx ]]; then
    (cd "$vulkan/runtime" && sha256sum --check --strict --quiet libc.prx.sha256)
else
    echo "setup-stack.sh: no PS5_Vulkan/runtime/libc.prx yet (make -C ../PS5_Vulkan libc, or LIBC_PRX=)" >&2
fi
radv=$vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a
if [[ -n ${RADV_ARCHIVE:-} ]]; then
    mkdir -p "$(dirname "$radv")"
    cp -u "$RADV_ARCHIVE" "$radv"
fi
[[ -f $radv ]] || echo "setup-stack.sh: no RADV archive at $radv yet (PS5_Vulkan's tools/build-radv.sh, or RADV_ARCHIVE=)" >&2

sdk=$(bash "$root/ps5/tools/setup-sdk.sh")
echo "sdk: $sdk"
bash "$root/ps5/tools/setup-source.sh"
echo "stack ready beside $root"
