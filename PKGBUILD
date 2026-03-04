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

build() {
    cd "$startdir"
    make clean SGDK_INC=/usr/include SGDK_LIB=/usr/lib LDFLAGS_COMMON=""
    make SGDK_INC=/usr/include SGDK_LIB=/usr/lib LDFLAGS_COMMON=""
}

check() {
    cd "$startdir"
    make test SGDK_INC=/usr/include SGDK_LIB=/usr/lib LDFLAGS_COMMON=""
}

package() {
    cd "$startdir"

    install -Dm755 pam_sgfp.so "$pkgdir/usr/lib/security/pam_sgfp.so"
    install -Dm755 sg_enroll   "$pkgdir/usr/bin/sg_enroll"
    install -dm700             "$pkgdir/etc/security/sg_fingerprints"
}
