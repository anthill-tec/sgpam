# Maintainer: Antony John <antojk@gmail.com>
pkgname=sgpam
pkgver=0.3.0
pkgrel=1
pkgdesc="PAM fingerprint authentication module for SecuGen U20 reader"
arch=('x86_64')
url="https://github.com/4property/sgpam"
license=('MIT')
depends=('secugen-fdx-driver' 'pam')
makedepends=('secugen-fdx-sdk' 'criterion' 'libdrm')
optdepends=(
    'greetd: Wayland-native login manager with fingerprint prompt support'
    'greetd-regreet: GTK4 greeter for greetd with theming support'
    'hyprland: Wayland compositor for sgpam session wrappers'
)
install=sgpam.install
source=()

prepare() {
    cp -a "$startdir"/{Makefile,pam_sgfp.c,sg_enroll.c,sg_fingers.h,sg-drm-blank.c,LICENSE,sg_enroll.1,pam_sgfp.8,hyprland.conf,sgpam-start-hyprland,sgpam-start-hyprland-uwsm,hyprland-sgpam.desktop,hyprland-uwsm-sgpam.desktop} "$srcdir/"
    cp -a "$startdir"/tests "$srcdir/"
}

build() {
    cd "$srcdir"
    make clean SGDK_INC=/usr/include SGDK_LIB=/usr/lib LDFLAGS_COMMON=""
    make SGDK_INC=/usr/include SGDK_LIB=/usr/lib LDFLAGS_COMMON="$LDFLAGS"
}

check() {
    cd "$srcdir"
    make test SGDK_INC=/usr/include SGDK_LIB=/usr/lib LDFLAGS_COMMON="$LDFLAGS"
}

package() {
    cd "$srcdir"

    install -Dm755 pam_sgfp.so "$pkgdir/usr/lib/security/pam_sgfp.so"
    install -Dm755 sg_enroll   "$pkgdir/usr/bin/sg_enroll"
    install -Dm755 sg-drm-blank "$pkgdir/usr/bin/sg-drm-blank"

    # Template directory — restrictive permissions (root-only, stores biometric data)
    install -dm700 "$pkgdir/etc/security/sg_fingerprints"

    install -Dm644 sg_enroll.1 "$pkgdir/usr/share/man/man1/sg_enroll.1"
    install -Dm644 pam_sgfp.8  "$pkgdir/usr/share/man/man8/pam_sgfp.8"

    # greetd greeter session config (Hyprland wrapper for regreet)
    install -Dm644 hyprland.conf "$pkgdir/etc/greetd/hyprland.conf"

    # Wayland session wrappers (DRM blanking between greeter and desktop)
    install -Dm755 sgpam-start-hyprland      "$pkgdir/usr/bin/sgpam-start-hyprland"
    install -Dm755 sgpam-start-hyprland-uwsm "$pkgdir/usr/bin/sgpam-start-hyprland-uwsm"
    install -Dm644 hyprland-sgpam.desktop      "$pkgdir/usr/share/wayland-sessions/hyprland-sgpam.desktop"
    install -Dm644 hyprland-uwsm-sgpam.desktop "$pkgdir/usr/share/wayland-sessions/hyprland-uwsm-sgpam.desktop"

    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
