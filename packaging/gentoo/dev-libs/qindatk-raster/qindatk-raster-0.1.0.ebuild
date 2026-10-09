# Copyright 2026 QindaTK contributors
# SPDX-License-Identifier: GPL-2.0-only
EAPI=8

inherit git-r3 multilib

DESCRIPTION="Native raster viewport and document-coordinate stylus input for QindaTK"
HOMEPAGE="https://github.com/Es00bac/QindaTK"
EGIT_REPO_URI="file:///home/cabewse/git/QindaTK.git
    file:///home/cabewse/work_SPaC3/QindaTK
    file:///home/cabewse/work_space/QindaTK"
EGIT_COMMIT="2f88e364aa63f0004ce21f2bac48f65a717b848c"
LICENSE="LGPL-3+"
SLOT="0"
KEYWORDS="~amd64"
IUSE="test"
RESTRICT="!test? ( test )"
RDEPEND=">=dev-libs/qindatk-0.1.0"
DEPEND="${RDEPEND}"
BDEPEND="test? ( dev-qt/qtdeclarative:6 )"

src_test() {
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
        /usr/$(get_libdir)/qt6/bin/qmltestrunner -input tests/raster -import modules \
        || die "Raster view tests failed"
}

src_install() {
    insinto "/usr/$(get_libdir)/qt6/qml/QindaTK/Raster"
    doins modules/QindaTK/Raster/{qmldir,*.qml}
    dodoc docs/raster.md
}
