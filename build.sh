#!/bin/bash
# =============================================================================
# Armada Cross-Platform Build Script
# =============================================================================
# This script builds the project for the current platform with all dependencies
# bundled. No external library installation is required.
#
# Usage:
#   ./build.sh [options]
#
# Options:
#   --release       Build in release mode (default)
#   --debug         Build in debug mode
#   --clean         Clean build directory before building
#   --package       Create distributable package after building
#   --help          Show this help message
# =============================================================================

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default options
BUILD_TYPE="Release"
CLEAN_BUILD=false
CREATE_PACKAGE=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --package)
            CREATE_PACKAGE=true
            shift
            ;;
        --help)
            head -25 "$0" | tail -20
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

# Detect platform
detect_platform() {
    case "$(uname -s)" in
        Darwin*)
            PLATFORM="macOS"
            ;;
        Linux*)
            PLATFORM="Linux"
            ;;
        MINGW*|MSYS*|CYGWIN*)
            PLATFORM="Windows"
            ;;
        *)
            PLATFORM="Unknown"
            ;;
    esac
    echo -e "${BLUE}Detected platform: ${PLATFORM}${NC}"
}

# Check dependencies
check_dependencies() {
    echo -e "${BLUE}Checking build dependencies...${NC}"
    
    # Check for CMake
    if ! command -v cmake &> /dev/null; then
        echo -e "${RED}CMake is not installed. Please install CMake 3.20 or higher.${NC}"
        if [[ "$PLATFORM" == "macOS" ]]; then
            echo "  Install with: brew install cmake"
        elif [[ "$PLATFORM" == "Linux" ]]; then
            echo "  Install with: sudo apt install cmake (Debian/Ubuntu)"
            echo "              or: sudo dnf install cmake (Fedora)"
        fi
        exit 1
    fi
    
    # Check CMake version
    CMAKE_VERSION=$(cmake --version | head -1 | cut -d' ' -f3)
    echo -e "${GREEN}Found CMake version: ${CMAKE_VERSION}${NC}"
    
    # Check for C/C++ compiler
    if [[ "$PLATFORM" == "macOS" ]]; then
        if ! command -v clang &> /dev/null; then
            echo -e "${RED}Clang is not installed. Please install Xcode Command Line Tools.${NC}"
            echo "  Install with: xcode-select --install"
            exit 1
        fi
        echo -e "${GREEN}Found compiler: $(clang --version | head -1)${NC}"
    elif [[ "$PLATFORM" == "Linux" ]]; then
        if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
            echo -e "${RED}No C++ compiler found. Please install GCC or Clang.${NC}"
            echo "  Install with: sudo apt install build-essential (Debian/Ubuntu)"
            exit 1
        fi
        if command -v g++ &> /dev/null; then
            echo -e "${GREEN}Found compiler: $(g++ --version | head -1)${NC}"
        else
            echo -e "${GREEN}Found compiler: $(clang++ --version | head -1)${NC}"
        fi
    fi
    
    # Check for Git (needed for FetchContent)
    if ! command -v git &> /dev/null; then
        echo -e "${RED}Git is not installed. Git is required to fetch dependencies.${NC}"
        exit 1
    fi
    echo -e "${GREEN}Found Git: $(git --version)${NC}"
}

# Get script directory (where CMakeLists.txt is)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
DIST_DIR="${SCRIPT_DIR}/dist"

# Main build function
build() {
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  Building Armada for ${PLATFORM}${NC}"
    echo -e "${BLUE}  Build Type: ${BUILD_TYPE}${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""
    
    # Clean if requested
    if [[ "$CLEAN_BUILD" == true ]]; then
        echo -e "${YELLOW}Cleaning build directory...${NC}"
        rm -rf "${BUILD_DIR}"
    fi
    
    # Create build directory
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    
    # Configure
    echo -e "${BLUE}Configuring with CMake...${NC}"
    cmake .. \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DSTATIC_BUILD=ON
    
    # Build
    echo -e "${BLUE}Building...${NC}"
    cmake --build . --config "${BUILD_TYPE}" -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    
    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}  Build completed successfully!${NC}"
    echo -e "${GREEN}========================================${NC}"
    
    # Show output location
    if [[ "$PLATFORM" == "Windows" ]]; then
        echo -e "${GREEN}Executable: ${BUILD_DIR}/${BUILD_TYPE}/armada.exe${NC}"
    else
        echo -e "${GREEN}Executable: ${BUILD_DIR}/armada${NC}"
    fi
    
    # Create package if requested
    if [[ "$CREATE_PACKAGE" == true ]]; then
        create_package
    fi
}

# Create distribution package
create_package() {
    echo ""
    echo -e "${BLUE}Creating distribution package...${NC}"
    
    mkdir -p "${DIST_DIR}"
    
    # Use CPack
    cd "${BUILD_DIR}"
    cpack -C "${BUILD_TYPE}"
    
    # Move packages to dist directory
    mv -f *.zip "${DIST_DIR}/" 2>/dev/null || true
    mv -f *.tar.gz "${DIST_DIR}/" 2>/dev/null || true
    mv -f *.dmg "${DIST_DIR}/" 2>/dev/null || true
    
    echo -e "${GREEN}Package created in: ${DIST_DIR}${NC}"
    ls -la "${DIST_DIR}"
}

# Run
detect_platform
check_dependencies
build

echo ""
echo -e "${GREEN}Done!${NC}"
