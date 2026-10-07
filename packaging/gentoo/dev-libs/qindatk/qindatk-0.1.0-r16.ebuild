# SPDX-License-Identifier: GPL-2.0-only
# Copyright 2026 QindaQt contributors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

inherit cmake

DESCRIPTION="Qt6/QML toolkit for dense desktop UIs: flex, grid, docking and theming"
HOMEPAGE="https://github.com/Es00bac/QindaQt"
# Immutable scoped Read SDK source; historical git-r3 recipes remain.
QINDATK_COMMIT="4fb7ae7a3f08140b9cb5085eba22cf5901e2f4ab"
SRC_URI="${PF}.tar.gz"
S="${WORKDIR}/QindaTK-${QINDATK_COMMIT}"
RESTRICT="fetch !test? ( test )"

# LGPL-3+ toolkit, ISC Lucide icons, MIT vendored CLAP plugin headers.
LICENSE="LGPL-3+ ISC MIT"
SLOT="0"
KEYWORDS="~amd64"
IUSE="+alsa +cups examples +ffmpeg +pdf +qindaqt +quick3d test +widgets"
REQUIRED_USE="cups? ( widgets )"

# Qt private API (QuickPrivate) ties the library to the exact Qt build, so
# qtbase and qtdeclarative use slot operators and trigger rebuilds.
# The QindaTK CMake package config asks consumers for libvterm via pkg-config.
# qtbase[ssl]: the mail and calendar protocol client (IMAP, POP3, SMTP,
# CalDAV, iCalendar feeds) speaks TLS only; r6 used it without declaring it.
RDEPEND="
	>=dev-lang/python-3.11
	>=dev-db/sqlite-3:3
	>=dev-libs/libvterm-0.3
	>=dev-qt/qtbase-6.8:6=[concurrent,gui,network,ssl]
	>=dev-qt/qtdeclarative-6.8:6=
	>=dev-qt/qtsvg-6.8:6=
	>=media-libs/lcms-2.14:2
	alsa? ( media-libs/alsa-lib )
	cups? (
		dev-qt/qtbase:6[cups,widgets]
		net-print/cups
	)
	ffmpeg? ( >=media-video/ffmpeg-6:= )
	pdf? ( app-text/poppler:=[qt6] )
	quick3d? ( >=dev-qt/qtquick3d-6.8:6= )
	widgets? ( dev-qt/qtbase:6[widgets] )
"
# AGENT-NOTE: gui-wm/qindaqt-desktop itself depends on qindatk. On a first
# install without the desktop, build with USE=-qindaqt, then rebuild.
DEPEND="${RDEPEND}
	>=dev-qt/qtshadertools-6.8:6
	qindaqt? ( gui-wm/qindaqt-desktop )
"
BDEPEND="
	app-alternatives/ninja
	>=dev-qt/qtshadertools-6.8:6
	virtual/pkgconfig
"

src_configure() {
	local mycmakeargs=(
		-DBUILD_TESTING=$(usex test)
		-DQINDATK_BUILD_EXAMPLES=$(usex examples)
		-DQINDATK_BUILD_TOOLS=ON
		-DQINDATK_QML_INSTALL_DIR="${EPREFIX}/usr/$(get_libdir)/qt6/qml"
		-DQINDATK_ENABLE_ALSA=$(usex alsa)
		-DQINDATK_ENABLE_FFMPEG=$(usex ffmpeg)
		-DQINDATK_ENABLE_POPPLER_QT6=$(usex pdf)
		-DQINDATK_ENABLE_QUICK3D=$(usex quick3d)
		-DQINDATK_ENABLE_SHADER_TOOLS=ON
		-DQINDATK_ENABLE_WIDGETS=$(usex widgets)
		-DQINDATK_ENABLE_NATIVE_PRINT=$(usex cups)
		-DCMAKE_DISABLE_FIND_PACKAGE_Cups=$(usex !cups)
		-DQINDATK_PREVIEW_QINDAQT=$(usex qindaqt)
	)
	cmake_src_configure
}

src_test() {
	# Every test is headless: offscreen platform, software renderer.
	QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software cmake_src_test
}

src_install() {
	cmake_src_install
	if use examples; then
		insinto /usr/share/qindatk
		doins -r "${S}"/examples
	fi
}

pkg_nofetch() {
	einfo "Supply the immutable archive from exact QindaTK commit ${QINDATK_COMMIT}:"
	einfo "git -c tar.umask=022 archive --format=tar --prefix=QindaTK-${QINDATK_COMMIT}/ ${QINDATK_COMMIT} | gzip -n > ${DISTDIR}/${PF}.tar.gz"
}
