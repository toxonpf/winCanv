# Maintainer: Codex

pkgname=workspace-templates
pkgver=0.1.0
pkgrel=1
pkgdesc="KDE Plasma 6 Wayland workspace template manager"
arch=('x86_64')
url=""
license=('MIT')
depends=(
  'qt6-base'
  'kwin'
  'plasma-workspace'
)
makedepends=(
  'cmake'
)
optdepends=(
  'kpackage: install and manage the bundled KWin shortcut script'
)
install="$pkgname.install"

prepare() {
  rm -rf "$srcdir/$pkgname-$pkgver"
  mkdir -p "$srcdir/$pkgname-$pkgver"
  cp -a "$startdir"/. "$srcdir/$pkgname-$pkgver"/
  rm -rf \
    "$srcdir/$pkgname-$pkgver/build" \
    "$srcdir/$pkgname-$pkgver/dist" \
    "$srcdir/$pkgname-$pkgver/.venv" \
    "$srcdir/$pkgname-$pkgver"/workspace_templates.egg-info \
    "$srcdir/$pkgname-$pkgver"/.pytest_cache
}

build() {
  cd "$srcdir/$pkgname-$pkgver"
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build build
}

package() {
  cd "$srcdir/$pkgname-$pkgver"

  DESTDIR="$pkgdir" cmake --install build
  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
