#!/bin/bash
#
# build-release.sh
# 完整的构建、签名、公证、打包流程
#
# 使用方法:
#   export APPLE_ID="you@example.com"
#   export APPLE_SPECIFIC_PASSWORD="xxxx-xxxx-xxxx-xxxx"
#   export APPLE_TEAM_ID="ABC1234567"
#   ./scripts/build-release.sh
#
# 注意: 版本号会自动从 project.pbxproj 读取
# 如需更新版本，请先运行:
#   ./scripts/increment-version.sh  # 递增版本号
#   ./scripts/increment-build.sh    # 递增构建号
#

set -e  # 遇到错误立即退出

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印函数
info() {
    echo -e "${BLUE}ℹ️  $1${NC}"
}

success() {
    echo -e "${GREEN}✅ $1${NC}"
}

error() {
    echo -e "${RED}❌ $1${NC}"
    exit 1
}

warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PBXPROJ_PATH="$PROJECT_ROOT/TypeTide.xcodeproj/project.pbxproj"

# 检查 project.pbxproj 是否存在
if [ ! -f "$PBXPROJ_PATH" ]; then
    error "project.pbxproj not found at $PBXPROJ_PATH"
fi

# 从 project.pbxproj 读取版本号
VERSION=$(grep "MARKETING_VERSION = " "$PBXPROJ_PATH" | head -1 | sed 's/.*= \(.*\);/\1/')
BUILD_NUMBER=$(grep "CURRENT_PROJECT_VERSION = " "$PBXPROJ_PATH" | head -1 | sed 's/.*= \([0-9]*\);/\1/')

if [ -z "$VERSION" ]; then
    error "MARKETING_VERSION not found in project.pbxproj"
fi

if [ -z "$BUILD_NUMBER" ]; then
    error "CURRENT_PROJECT_VERSION not found in project.pbxproj"
fi

BUILD_DIR="$PROJECT_ROOT/build"
ARCHIVE_PATH="$BUILD_DIR/TypeTide.xcarchive"
EXPORT_DIR="$BUILD_DIR/export"
APP_NAME="TypeTide"
APP_PATH="$EXPORT_DIR/$APP_NAME.app"

info "Building TypeTide version $VERSION (build $BUILD_NUMBER)"
info "Project root: $PROJECT_ROOT"

# 发布凭据只从调用方导出的环境变量读取，不加载仓库内的密钥文件。
if [ -z "${APPLE_ID:-}" ]; then
    error "APPLE_ID is not exported"
fi

if [ -z "${APPLE_SPECIFIC_PASSWORD:-}" ]; then
    error "APPLE_SPECIFIC_PASSWORD is not exported"
fi

if [ -z "${APPLE_TEAM_ID:-}" ]; then
    error "APPLE_TEAM_ID is not exported"
fi

# 按 Team ID 自动选择 Developer ID Application 证书，避免额外的证书名配置。
DEVELOPER_ID_APPLICATION="$(security find-identity -v -p codesigning |
    sed -n "s/.*\"\\(Developer ID Application:.*(${APPLE_TEAM_ID})\\)\".*/\\1/p" |
    head -1)"
if [ -z "$DEVELOPER_ID_APPLICATION" ]; then
    error "No valid Developer ID Application certificate found for team $APPLE_TEAM_ID"
fi
success "Using signing identity: $DEVELOPER_ID_APPLICATION"

# 步骤 1: 清理旧的构建文件
info "Step 1/6: Cleaning build directory..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$EXPORT_DIR"
success "Build directory cleaned"

# 步骤 2: 构建 Archive
info "Step 2/6: Building archive..."
cd "$PROJECT_ROOT"

xcodebuild archive \
    -scheme TypeTide \
    -configuration Release \
    -archivePath "$ARCHIVE_PATH" \
    -destination "generic/platform=macOS" \
    CODE_SIGN_STYLE=Manual \
    CODE_SIGN_IDENTITY="$DEVELOPER_ID_APPLICATION" \
    DEVELOPMENT_TEAM="$APPLE_TEAM_ID" \
    || error "Archive build failed"

success "Archive created at $ARCHIVE_PATH"

# 步骤 3: 导出 App
info "Step 3/6: Exporting application..."

# 检查 ExportOptions.plist 是否存在
EXPORT_OPTIONS="$PROJECT_ROOT/ExportOptions.plist"
if [ ! -f "$EXPORT_OPTIONS" ]; then
    error "ExportOptions.plist not found at $EXPORT_OPTIONS"
fi

# Export with the same Developer ID identity used for the archive.
RESOLVED_EXPORT_OPTIONS="$BUILD_DIR/ExportOptions.plist"
cp "$EXPORT_OPTIONS" "$RESOLVED_EXPORT_OPTIONS"
/usr/libexec/PlistBuddy -c "Add :teamID string $APPLE_TEAM_ID" "$RESOLVED_EXPORT_OPTIONS"

xcodebuild -exportArchive \
    -archivePath "$ARCHIVE_PATH" \
    -exportPath "$EXPORT_DIR" \
    -exportOptionsPlist "$RESOLVED_EXPORT_OPTIONS" \
    || error "Export failed"

success "Application exported to $EXPORT_DIR"

# 步骤 4: 签名
info "Step 4/6: Signing application..."

codesign --deep --force --verify --verbose \
    --sign "$DEVELOPER_ID_APPLICATION" \
    --options runtime \
    --timestamp \
    "$APP_PATH" || error "Code signing failed"

# 验证签名
codesign --verify --deep --strict --verbose=2 "$APP_PATH" || error "Code signature verification failed"
spctl --assess --type execute --verbose=4 "$APP_PATH" || warning "Gatekeeper assessment shows warnings (this is expected before notarization)"

success "Application signed successfully"

# 步骤 5: 创建 DMG
info "Step 5/6: Creating DMG..."

DMG_NAME="$APP_NAME-$VERSION.dmg"
DMG_PATH="$BUILD_DIR/$DMG_NAME"

# Build without Finder/AppleScript so packaging also works in headless sessions.
DMG_STAGING="$BUILD_DIR/dmg-staging"
mkdir -p "$DMG_STAGING"
ditto "$APP_PATH" "$DMG_STAGING/$APP_NAME.app"
ln -s /Applications "$DMG_STAGING/Applications"
hdiutil create -volname "$APP_NAME" -srcfolder "$DMG_STAGING" \
    -ov -format UDZO "$DMG_PATH" || error "DMG creation failed"

codesign --force --sign "$DEVELOPER_ID_APPLICATION" --timestamp "$DMG_PATH" \
    || error "DMG signing failed"
success "DMG created at $DMG_PATH"

# 步骤 6: 公证
info "Step 6/6: Notarizing application..."

# 上传公证
info "Uploading to Apple for notarization (this may take a few minutes)..."

NOTARIZE_OUTPUT=$(xcrun notarytool submit "$DMG_PATH" \
    --apple-id "$APPLE_ID" \
    --team-id "$APPLE_TEAM_ID" \
    --password "$APPLE_SPECIFIC_PASSWORD" \
    --wait 2>&1)

echo "$NOTARIZE_OUTPUT"

# 检查公证是否成功
if echo "$NOTARIZE_OUTPUT" | grep -q "status: Accepted"; then
    success "Notarization succeeded"

    # 装订票据
    info "Stapling notarization ticket..."
    xcrun stapler staple "$DMG_PATH" || error "Stapling failed"

    # 验证装订
    xcrun stapler validate "$DMG_PATH" || error "Staple validation failed"

    success "Ticket stapled successfully"
else
    error "Notarization failed. Check the output above for details."
fi

# 最终验证
info "Performing final verification..."

# 验证 DMG 的公证票据
info "Verifying notarization ticket on DMG..."
if xcrun stapler validate "$DMG_PATH" 2>&1 | grep -q "is already validated"; then
    success "DMG notarization ticket is valid"
else
    xcrun stapler validate "$DMG_PATH"
fi

# 验证 .app 签名（已在步骤 4 中完成，这里再次确认）
info "Verifying app signature..."
codesign --verify --deep --strict "$APP_PATH" && success "App signature is valid"

# 注意：spctl 对 DMG 文件的验证不适用
# DMG 是一个容器格式，真正需要验证的是其中的 .app
# 已通过公证和装订的 DMG 在用户下载后会被 Gatekeeper 自动验证


echo "Open release site"
open https://github.com/everettjf/typetide/releases

# 完成
echo ""
success "🎉 Release build completed successfully!"
echo ""
info "Release package: $DMG_PATH"
info "Size: $(du -h "$DMG_PATH" | cut -f1)"
info "Version: $VERSION"
echo ""
info "Next steps:"
echo "  1. Test the DMG on a clean Mac"
echo "  2. Create a GitHub release (tag: v$VERSION)"
echo "  3. Upload $DMG_NAME to the release"
echo "  4. Update release notes"
echo ""

# 可选：自动打开 Finder
if command -v open &> /dev/null; then
    open "$BUILD_DIR"
fi


rm -rf "$EXPORT_DIR"
