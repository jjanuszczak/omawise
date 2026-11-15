pkgname=omawise
pkgver=0.1.3
pkgrel=1
pkgdesc='Keyboard-first Readwise Reader client for Omarchy'
arch=('x86_64')
url='https://readwise.io/read'
license=('MIT')
options=('!debug')
depends=('qt6-base' 'qt6-declarative' 'qt6-svg' 'qt6-wayland' 'libsecret')
optdepends=('gnome-keyring: Secret Service provider for secure Readwise token storage'
            'npm: install the official @readwise/cli required for hybrid search and annotation operations')
makedepends=('base-devel' 'qt6-base' 'qt6-declarative' 'qt6-svg' 'libsecret')
source=()
sha256sums=()

_projectdir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

prepare() {
    mkdir -p "$srcdir/omawise"
    cp -a "$_projectdir/omawise.pro" "$srcdir/omawise/"
    cp -a "$_projectdir/src" "$srcdir/omawise/"
    cp -a "$_projectdir/assets" "$srcdir/omawise/"
    cp -a "$_projectdir/config" "$srcdir/omawise/"
    cp -a "$_projectdir/skills" "$srcdir/omawise/"
    cp -a "$_projectdir/pkgbuild" "$srcdir/omawise/"
    cp -a "$_projectdir/LICENSE" "$srcdir/omawise/"
}

build() {
    cd "$srcdir/omawise"
    mkdir -p build
    cd build
    qmake6 ../omawise.pro
    make -j"$(nproc)"
}

package() {
    install -Dm755 "$srcdir/omawise/build/omawise" "$pkgdir/usr/bin/omawise"
    install -Dm644 "$srcdir/omawise/pkgbuild/omawise.desktop" "$pkgdir/usr/share/applications/omawise.desktop"
    install -Dm644 "$srcdir/omawise/assets/omawise-mark.svg" "$pkgdir/usr/share/icons/hicolor/scalable/apps/omawise.svg"
    install -Dm644 "$srcdir/omawise/config/reader.toml" "$pkgdir/usr/share/omawise/examples/reader.toml"
    install -Dm644 "$srcdir/omawise/skills/omawise-reader-settings/SKILL.md" "$pkgdir/usr/share/omawise/skills/omawise-reader-settings/SKILL.md"
    install -Dm644 "$srcdir/omawise/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
