# Aptio4-Above4G-MMIO64

DXE driver and Windows CLI research project for enabling Above 4G Decoding on selected AMI Aptio 4 firmware.

## Scope

Above 4G Decoding only. Resizable BAR (ReBAR) and Intel Gen 1 are out of scope. Unknown firmware must remain unsupported and cause no action.

See [docs/spec.md](docs/spec.md) for the research specification and validated firmware-family evidence.

The detector reports signature classifications only. Verify the documented code patterns against the exact BIOS build before any profile-specific action; `PROFILE_GEN4_CANDIDATE` always requires additional `GetAllocAttributes` review.

## Detection Tests

Build with a C99-compatible compiler (GCC or Clang):

```sh
cc -std=c99 -Wall -Wextra -Werror -pedantic detect/detect.c detect/tests/test_detect.c -o detect-tests
./detect-tests
```

Build and run the profile-action mock tests with GCC or Clang:

```sh
cc -std=c99 -Wall -Wextra -Werror -pedantic driver/actions/gen23.c driver/actions/gen5.c driver/actions/gen4.c driver/actions/tests/test_actions.c -o action-tests
./action-tests
```

Build and run the platform-provider helper tests with GCC or Clang. These use a fake FV2 service table and do not require EDK II or firmware:

```sh
cc -std=c99 -Wall -Wextra -Werror -pedantic driver/platform/platform_testable.c driver/platform/tests/test_platform.c -o platform-tests
./platform-tests
```

With MSVC, run from a Developer PowerShell for Visual Studio:

```powershell
cl /nologo /W4 /WX /TC /std:c11 detect\detect.c detect\tests\test_detect.c /Fe:detect-tests.exe
.\detect-tests.exe
cl /nologo /W4 /WX /TC /std:c11 driver\actions\gen23.c driver\actions\gen5.c driver\actions\gen4.c driver\actions\tests\test_actions.c /Fe:action-tests.exe
.\action-tests.exe
```

## EDK II DXE Build (WSL2 Ubuntu)

The X64 DEBUG provider build was verified with EDK II `edk2-stable202608` and the `GCC` toolchain tag. Keep the upstream checkout under the Linux filesystem and expose this repository as a separate package path. On this checkout, BaseTools and MdePkg require their pinned Brotli and MipiSysTLib submodules.

Install prerequisites if missing:

```sh
sudo apt update
sudo apt install -y build-essential uuid-dev nasm iasl git python3 acpica-tools
```

Clone and initialize the pinned dependencies:

```sh
mkdir -p ~/edk2-workspace
git clone https://github.com/tianocore/edk2.git ~/edk2-workspace/edk2
cd ~/edk2-workspace/edk2
git checkout edk2-stable202608
git submodule update --init BaseTools/Source/C/BrotliCompress/brotli MdePkg/Library/MipiSysTLib/mipisyst
export REPO=/mnt/c/Users/kerion/OneDrive/Documents/Aptio4-Above4G-MMIO64
export PACKAGES_PATH="$PWD:$REPO"
source edksetup.sh
make -C BaseTools
```

Build the platform-services provider for X64 DEBUG:

```sh
cd "$REPO"
export WORKSPACE=$REPO
export PACKAGES_PATH="$HOME/edk2-workspace/edk2:$REPO"
build -p driver/Above4GPlatform.dsc -m driver/platform/Above4GPlatform.inf -a X64 -t GCC -b DEBUG
```

The provider output is `Build/Aptio4Above4G/DEBUG_GCC/X64/Aptio4Above4GPlatform.efi` in this repository. The provider and dispatcher are standalone DXE images; they are not integrated into a board firmware volume, and no BIOS image is modified. A successful build does not verify dispatch timing, firmware ABI assumptions, profile behavior, or hardware operation. The Gen 2-3 preflight intentionally rejects activation, and Gen 4 remains blocked by its manual-review byte-patch placeholder.