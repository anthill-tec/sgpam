# Maintainer: Antony John <antojk@gmail.com>
pkgname=sgpam
pkgver=0.1.0
pkgrel=1
pkgdesc="PAM fingerprint authentication module for SecuGen U20 reader"
arch=('x86_64')
url="https://github.com/4property/sgpam"
license=('MIT')
depends=('secugen-fdx-driver' 'pam')
makedepends=('secugen-fdx-sdk' 'criterion')
install=sgpam.install
source=()

prepare() {
    cp -a "$startdir"/{Makefile,pam_sgfp.c,sg_enroll.c,sg_fingers.h,LICENSE,sg_enroll.1,pam_sgfp.8} "$srcdir/"
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

    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
