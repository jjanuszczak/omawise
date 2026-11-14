pkgname=omawise
pkgver=0.1.0
pkgrel=1
pkgdesc='Keyboard-first Readwise Reader client for Omarchy'
arch=('x86_64')
url='https://readwise.io/read'
license=('MIT')
depends=('qt6-base' 'qt6-declarative' 'qt6-svg' 'qt6-wayland')
optdepends=('npm: install the official @readwise/cli required by Omawise')
makedepends=('base-devel' 'qt6-base' 'qt6-declarative' 'qt6-svg')
source=()
sha256sums=()

# This PKGBUILD builds from the checked-out project source. The tag workflow
# checks the tag against pkgver and packages that exact checkout.
build() {
    mkdir -p "$startdir/build"
    cd "$startdir/build"
    qmake6 "$startdir/omawise.pro"
    make -j"$(nproc)"
}

package() {
    install -Dm755 "$startdir/build/omawise" "$pkgdir/usr/bin/omawise"
    install -Dm644 "$startdir/pkgbuild/omawise.desktop" "$pkgdir/usr/share/applications/omawise.desktop"
    install -Dm644 "$startdir/assets/omawise-mark.svg" "$pkgdir/usr/share/icons/hicolor/scalable/apps/omawise.svg"
    install -Dm644 "$startdir/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
