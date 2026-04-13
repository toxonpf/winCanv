# Maintainer: Codex

pkgname=workspace-templates
pkgver=0.1.0
pkgrel=1
pkgdesc="KDE Plasma 6 Wayland workspace template manager"
arch=('any')
url=""
license=('MIT')
depends=(
  'python'
  'python-pyqt6'
  'python-dbus'
  'python-gobject'
  'kwin'
  'plasma-workspace'
  'qt6-base'
)
makedepends=(
  'python-build'
  'python-installer'
  'python-setuptools'
  'python-wheel'
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
    "$srcdir/$pkgname-$pkgver"/.pytest_cache \
    "$srcdir/$pkgname-$pkgver"/__pycache__ \
    "$srcdir/$pkgname-$pkgver"/workspace_templates/__pycache__ \
    "$srcdir/$pkgname-$pkgver"/tests/__pycache__
}

build() {
  cd "$srcdir/$pkgname-$pkgver"
  python -m build --wheel --no-isolation
}

package() {
  cd "$srcdir/$pkgname-$pkgver"

  python -m installer --destdir="$pkgdir" dist/*.whl

  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
  install -Dm644 workspace_templates/assets/io.github.toxonpf.workspacetemplates.desktop \
    "$pkgdir/usr/share/applications/io.github.toxonpf.workspacetemplates.desktop"

  mkdir -p "$pkgdir/usr/share/kwin/scripts/workspacetemplates-shortcut"
  cp -r workspace_templates/assets/kwin-shortcut/. \
    "$pkgdir/usr/share/kwin/scripts/workspacetemplates-shortcut/"
}
