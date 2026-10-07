#!/usr/bin/env bash
# ==============================================================================
# API-cli Dependency Installation Script for Debian & Ubuntu
# ==============================================================================
# Installs compilers, build systems, Boost libraries, OpenSSL, and utilities
# required to build and run the API-cli framework and modules (C++23/C++26).
# ==============================================================================

set -euo pipefail

# Text formatting
if [[ -t 1 ]]; then
    BOLD=$'\e[1m'
    DIM=$'\e[2m'
    RED=$'\e[1;31m'
    GREEN=$'\e[1;32m'
    YELLOW=$'\e[1;33m'
    BLUE=$'\e[1;34m'
    CYAN=$'\e[1;36m'
    RESET=$'\e[0m'
else
    BOLD=""
    DIM=""
    RED=""
    GREEN=""
    YELLOW=""
    BLUE=""
    CYAN=""
    RESET=""
fi

log_info() {
    printf "${BLUE}[INFO]${RESET} %s\n" "$*"
}

log_success() {
    printf "${GREEN}[SUCCESS]${RESET} %s\n" "$*"
}

log_warn() {
    printf "${YELLOW}[WARNING]${RESET} %s\n" "$*"
}

log_error() {
    printf "${RED}[ERROR]${RESET} %s\n" "$*" >&2
}

# Default configuration flags
ASSUME_YES=false
INSTALL_DOCS=true
INSTALL_OLLAMA=false
DRY_RUN=false
FORCE=false

print_usage() {
    cat <<EOF
${BOLD}API-cli Dependency Installer${RESET}
Automates system library and toolchain installation for Debian and Ubuntu.

${BOLD}USAGE:${RESET}
    $0 [OPTIONS]

${BOLD}OPTIONS:${RESET}
    -y, --yes           Assume 'yes' to all package manager prompts (non-interactive)
    --with-docs         Install Doxygen and Graphviz for documentation (default)
    --no-docs           Skip Doxygen and Graphviz installation
    --with-ollama       Install Ollama local daemon via official install script
    --dry-run           Simulate actions without modifying system packages
    --force             Proceed even if distribution detection is not recognized as Debian/Ubuntu
    -h, --help          Show this help message and exit

${BOLD}SUPPORTED DISTRIBUTIONS:${RESET}
    - Ubuntu 24.04 LTS (Noble Numbat) or newer
    - Ubuntu 22.04 LTS (Jammy Jellyfish) [installs GCC 14 / modern toolchain]
    - Debian 13 (Trixie) or newer
    - Debian 12 (Bookworm)
    - Compatible Debian/Ubuntu derivatives (Linux Mint, Pop!_OS, Elementary OS)
EOF
}

# Parse command line options
while [[ $# -gt 0 ]]; do
    case "$1" in
        -y|--yes)
            ASSUME_YES=true
            shift
            ;;
        --with-docs)
            INSTALL_DOCS=true
            shift
            ;;
        --no-docs)
            INSTALL_DOCS=false
            shift
            ;;
        --with-ollama)
            INSTALL_OLLAMA=true
            shift
            ;;
        --dry-run)
            DRY_RUN=true
            shift
            ;;
        --force)
            FORCE=true
            shift
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            print_usage
            exit 1
            ;;
    esac
done

# Non-interactive environment detection
if [[ -n "${DEBIAN_FRONTEND:-}" && "${DEBIAN_FRONTEND}" == "noninteractive" ]] || [[ -n "${CI:-}" ]]; then
    ASSUME_YES=true
fi

# Print banner
cat << "EOF"
  _____  _____       ____   ___  
 / _ \ \/ / _ \ ___ / ___| / _ \ 
/ /_\ \  / /_\ /___| |    | | | |
/ /_\\ \/ / /_\\    | |___ | |_| |
\____/_/\_\____/     \____(_)___/ 

API-cli Dependency Installer (Debian & Ubuntu)
======================================================
EOF

# Step 1: Detect Operating System
log_info "Detecting operating system..."
if [[ ! -f /etc/os-release ]]; then
    if [[ "$FORCE" == false ]]; then
        log_error "/etc/os-release not found. This script requires a Debian or Ubuntu system."
        exit 1
    else
        log_warn "Proceeding due to --force flag despite missing /etc/os-release."
        OS_ID="unknown"
        OS_LIKE="debian"
        OS_PRETTY="Unknown Linux (Forced)"
        OS_VERSION_ID="unknown"
    fi
else
    # shellcheck disable=SC1091
    source /etc/os-release
    OS_ID="${ID:-unknown}"
    OS_LIKE="${ID_LIKE:-}"
    OS_PRETTY="${PRETTY_NAME:-$OS_ID}"
    OS_VERSION_ID="${VERSION_ID:-}"
fi

log_info "Detected OS: ${BOLD}${OS_PRETTY}${RESET} (ID: ${OS_ID}, ID_LIKE: ${OS_LIKE:-none})"

IS_DEBIAN_OR_UBUNTU=false
if [[ "$OS_ID" == "debian" || "$OS_ID" == "ubuntu" ]]; then
    IS_DEBIAN_OR_UBUNTU=true
elif [[ "$OS_LIKE" =~ (debian|ubuntu) ]]; then
    IS_DEBIAN_OR_UBUNTU=true
fi

if [[ "$IS_DEBIAN_OR_UBUNTU" == false && "$FORCE" == false ]]; then
    log_error "This script is designed for Debian, Ubuntu, or their derivatives."
    log_error "Use --force if you wish to attempt installation on this system anyway."
    exit 1
fi

# Step 2: Privilege Determination
SUDO=""
if [[ $EUID -ne 0 ]]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
        log_info "Using 'sudo' for administrative package commands."
    else
        log_error "This script requires root privileges or 'sudo' to install packages."
        log_error "Please run as root or install sudo: apt-get install -y sudo"
        exit 1
    fi
else
    log_info "Running with direct root privileges."
fi

# Step 3: Define Required Packages
# Core build tools
BUILD_PACKAGES=(
    build-essential
    cmake
    ninja-build
    git
    pkg-config
    curl
    wget
    ca-certificates
    python3
)

# Boost development packages required by CMakeLists.txt
BOOST_PACKAGES=(
    libboost-dev
    libboost-log-dev
    libboost-thread-dev
    libboost-system-dev
    libboost-filesystem-dev
)

# OpenSSL required by mod-jwt & HTTPS Beast client
SSL_PACKAGES=(
    libssl-dev
)

# Optional documentation generation packages
DOC_PACKAGES=(
    doxygen
    graphviz
)

# Aggregate all packages to install
TARGET_PACKAGES=("${BUILD_PACKAGES[@]}" "${BOOST_PACKAGES[@]}" "${SSL_PACKAGES[@]}")

if [[ "$INSTALL_DOCS" == true ]]; then
    TARGET_PACKAGES+=("${DOC_PACKAGES[@]}")
fi

# Step 4: Toolchain & GCC Version Check (C++23 / C++26 standard)
log_info "Evaluating C++ toolchain availability..."

EXTRA_SETUP_CMDS=()
GCC_UPGRADE_PACKAGES=()

# Ubuntu 22.04 LTS (Jammy) default is GCC 11 & CMake 3.22
# C++23/C++26 features require GCC >= 13 (GCC 14 recommended), CMake >= 3.25
if [[ "$OS_ID" == "ubuntu" && "$OS_VERSION_ID" =~ ^22\. ]]; then
    log_warn "Ubuntu 22.04 default GCC is 11 (requires GCC 14) and CMake is 3.22 (requires >= 3.25)."
    log_info "Configuring Ubuntu Toolchain PPA for GCC 14..."
    EXTRA_SETUP_CMDS+=("apt-get install -y software-properties-common gpg wget")
    EXTRA_SETUP_CMDS+=("add-apt-repository -y ppa:ubuntu-toolchain-r/test")
    EXTRA_SETUP_CMDS+=("wget -qO - https://apt.kitware.com/keys/kitware-archive-latest.asc | gpg --dearmor -o /etc/apt/trusted.gpg.d/kitware.gpg")
    EXTRA_SETUP_CMDS+=("apt-add-repository -y 'deb https://apt.kitware.com/ubuntu/ jammy main'")
    GCC_UPGRADE_PACKAGES+=(gcc-14 g++-14)
elif [[ "$OS_ID" == "debian" && "$OS_VERSION_ID" == "12" ]]; then
    log_info "Debian 12 defaults to GCC 12. If C++26/C++23 features require GCC 14, install from Debian backports or Clang 19."
fi

# Step 5: Execute Package Installation
APT_FLAGS=()
if [[ "$ASSUME_YES" == true ]]; then
    APT_FLAGS+=("-y")
    export DEBIAN_FRONTEND=noninteractive
fi

run_admin() {
    local cmd=("$@")
    if [[ "$DRY_RUN" == true ]]; then
        if [[ -n "$SUDO" ]]; then
            printf "${CYAN}[DRY-RUN]${RESET} %s %s\n" "$SUDO" "${cmd[*]}"
        else
            printf "${CYAN}[DRY-RUN]${RESET} %s\n" "${cmd[*]}"
        fi
    else
        if [[ -n "$SUDO" ]]; then
            $SUDO "${cmd[@]}"
        else
            "${cmd[@]}"
        fi
    fi
}

log_info "Updating APT package repository index..."
run_admin apt-get update

if [[ ${#EXTRA_SETUP_CMDS[@]} -gt 0 ]]; then
    for cmd_str in "${EXTRA_SETUP_CMDS[@]}"; do
        log_info "Executing setup command: ${cmd_str}"
        if [[ "$DRY_RUN" == true ]]; then
            printf "${CYAN}[DRY-RUN]${RESET} %s %s\n" "${SUDO:+$SUDO }" "$cmd_str"
        else
            if [[ -n "$SUDO" ]]; then
                $SUDO bash -c "$cmd_str"
            else
                bash -c "$cmd_str"
            fi
        fi
    done
    log_info "Refreshing package index after repository addition..."
    run_admin apt-get update
fi

if [[ ${#GCC_UPGRADE_PACKAGES[@]} -gt 0 ]]; then
    TARGET_PACKAGES+=("${GCC_UPGRADE_PACKAGES[@]}")
fi

log_info "Installing required packages: ${TARGET_PACKAGES[*]}"
run_admin apt-get install "${APT_FLAGS[@]}" "${TARGET_PACKAGES[@]}"

# Set up GCC alternatives if g++-14 was explicitly installed
if command -v g++-14 >/dev/null 2>&1; then
    CURRENT_GXX=$(command -v g++ || true)
    if [[ -z "$CURRENT_GXX" ]] || ! g++ -dumpversion | grep -qE "^(14|15)"; then
        log_info "Configuring g++-14 as default C++ compiler..."
        run_admin update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 \
            --slave /usr/bin/g++ g++ /usr/bin/g++-14
    fi
fi

# Step 6: Optional Ollama Installation
if [[ "$INSTALL_OLLAMA" == true ]]; then
    log_info "Checking Ollama installation..."
    if command -v ollama >/dev/null 2>&1; then
        log_success "Ollama is already installed: $(ollama --version)"
    else
        log_info "Installing Ollama daemon via official installer..."
        if [[ "$DRY_RUN" == true ]]; then
            printf "${CYAN}[DRY-RUN]${RESET} curl -fsSL https://ollama.com/install.sh | sh\n"
        else
            curl -fsSL https://ollama.com/install.sh | sh
            log_success "Ollama successfully installed."
        fi
    fi
fi

# Step 7: System Verification
log_info "Verifying installed libraries and toolchains..."

check_status() {
    local label="$1"
    local check_cmd="$2"
    if eval "$check_cmd" >/dev/null 2>&1; then
        printf "  %-30s ${GREEN}✓ Available${RESET}\n" "$label"
    else
        printf "  %-30s ${RED}✗ Not found${RESET}\n" "$label"
    fi
}

echo ""
printf "${BOLD}Verification Summary:${RESET}\n"
printf "%s\n" "------------------------------------------------------"
check_status "C++ Compiler (g++)" "command -v g++"
check_status "C Compiler (gcc)" "command -v gcc"
check_status "CMake Build System" "command -v cmake"
check_status "Ninja Build Tool" "command -v ninja"
check_status "Git Version Control" "command -v git"
check_status "Pkg-Config" "command -v pkg-config || command -v pkgconf"
check_status "Boost Headers" "test -f /usr/include/boost/version.hpp"
check_status "Boost Log Dev" "ldconfig -p | grep -q libboost_log || ls /usr/lib/*/*boost_log*.so >/dev/null 2>&1"
check_status "Boost Thread Dev" "ldconfig -p | grep -q libboost_thread || ls /usr/lib/*/*boost_thread*.so >/dev/null 2>&1"
check_status "OpenSSL Headers" "test -f /usr/include/openssl/ssl.h"
check_status "Python 3" "command -v python3"

if [[ "$INSTALL_DOCS" == true ]]; then
    check_status "Doxygen" "command -v doxygen"
    check_status "Graphviz (dot)" "command -v dot"
fi

if [[ "$INSTALL_OLLAMA" == true ]] || command -v ollama >/dev/null 2>&1; then
    check_status "Ollama CLI" "command -v ollama"
fi

printf "%s\n" "------------------------------------------------------"

# Toolchain version details
if command -v g++ >/dev/null 2>&1; then
    log_info "Active C++ Compiler: ${BOLD}$(g++ --version | head -n 1)${RESET}"
fi
if command -v cmake >/dev/null 2>&1; then
    log_info "Active CMake: ${BOLD}$(cmake --version | head -n 1)${RESET}"
fi
if test -f /usr/include/boost/version.hpp; then
    BOOST_VER=$(grep "#define BOOST_LIB_VERSION" /usr/include/boost/version.hpp | awk -F'"' '{print $2}' || true)
    log_info "Active Boost: ${BOLD}Boost ${BOOST_VER}${RESET}"
fi

echo ""
log_success "All required dependencies are satisfied!"
echo ""
cat << "EOF"
To build the API framework:
  1. Configure CMake:
     cmake -B build -DCMAKE_BUILD_TYPE=Release

  2. Compile targets:
     cmake --build build -j$(nproc)

  3. Run API-cli:
     ./build/API-cli            # Interactive TUI dashboard
     ./build/API-cli --headless # Daemon mode

  4. Run Ollama integration tests:
     python3 scripts/test_ollama_integration.py
EOF
