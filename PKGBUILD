pkgname=rwreader
pkgver=0.1.0
pkgrel=1
pkgdesc='Keyboard-first Readwise Reader client for Omarchy'
arch=('x86_64')
url='https://readwise.io/read'
license=('MIT')
depends=('qt6-base' 'qt6-declarative' 'qt6-wayland')
makedepends=('base-devel' 'qt6-base' 'qt6-declarative')
source=()
sha256sums=()

# This development PKGBUILD builds from the current project checkout. For a
# public package, switch to a versioned release tarball and a verified checksum.
build() {
    cd "$startdir"
    qmake6 "$startdir/rwreader.pro" -o "$srcdir/Makefile"
    make -C "$srcdir" -j"$(nproc)"
}

package() {
    install -Dm755 "$srcdir/rwreader" "$pkgdir/usr/bin/rwreader"
    install -Dm644 "$startdir/pkgbuild/rwreader.desktop" "$pkgdir/usr/share/applications/rwreader.desktop"
    install -Dm644 "$startdir/pkgbuild/rwreader.svg" "$pkgdir/usr/share/icons/hicolor/scalable/apps/rwreader.svg"
    install -Dm644 "$startdir/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
