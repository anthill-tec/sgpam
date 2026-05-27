# Maintainer: Antony John <antojk@gmail.com>
pkgname=sgpam
pkgver=1.1.0
pkgrel=1
pkgdesc="SecuGen U20 fingerprint authentication for sudo, TTY and greeter login (greetd/ReGreet)"
arch=('x86_64')
url="https://github.com/anthill-tec/sgpam"
license=('MIT')
# Runtime: driver (libsgfplib.so), pam (libpam), gcc-libs (libstdc++).
# libdrm is build-only here: sg-drm-blank and the DRM tests are built/checked
# but the DRM-blanking binary is NOT shipped in this release (pending more
# testing of the --hold variant). The SDK is build-only too.
depends=('secugen-fdx-driver' 'pam' 'gcc-libs')
makedepends=('secugen-fdx-sdk' 'criterion' 'libdrm')
optdepends=(
    'greetd: Wayland-native login manager with fingerprint prompt support'
    'greetd-regreet: GTK4 greeter for greetd with theming support'
    'hyprland: Wayland compositor for the greetd/ReGreet greeter'
)
install=sgpam.install
source=()

prepare() {
    cp -a "$startdir"/{Makefile,pam_sgfp.c,sg_enroll.c,sg_fingers.h,sg-drm-blank.c,LICENSE,sg_enroll.1,pam_sgfp.8,hyprland.conf} "$srcdir/"
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

    # Template directory — restrictive permissions (root-only, stores biometric data)
    install -dm700 "$pkgdir/etc/security/sg_fingerprints"

    install -Dm644 sg_enroll.1 "$pkgdir/usr/share/man/man1/sg_enroll.1"
    install -Dm644 pam_sgfp.8  "$pkgdir/usr/share/man/man8/pam_sgfp.8"

    # greetd greeter config — runs ReGreet (the fingerprint login screen) under
    # Hyprland. NOT the DRM-blanking wrappers, which are deferred to a later
    # release (sg-drm-blank + the session .desktop entries are not shipped yet).
    install -Dm644 hyprland.conf "$pkgdir/etc/greetd/hyprland.conf"

    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
