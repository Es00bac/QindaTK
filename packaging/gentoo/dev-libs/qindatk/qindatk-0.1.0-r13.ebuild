# SPDX-License-Identifier: GPL-2.0-only
# Copyright 2026 QindaQt contributors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

inherit cmake git-r3

DESCRIPTION="Qt6/QML toolkit for dense desktop UIs: flex, grid, docking and theming"
HOMEPAGE="https://github.com/Es00bac/QindaQt"
# The hub on qinda is read first; the laptop keeps a bare mirror at the same
# path, fast-forwarded from qinda before an emerge.
EGIT_REPO_URI="file:///home/cabewse/git/QindaTK.git
	file:///home/cabewse/work_SPaC3/QindaTK
	file:///home/cabewse/work_space/QindaTK"
# r8 (QindaTK 77d01dab, D-304): StylusHandler inks with one finger when a
# consumer opts TouchScreen in, and Viewport pinch-zooms and pans with two
# fingers (touchpad pinch too) — QindaNote touch input.
# r9 (D-305): the Ribbon squeezes group by group from the right instead of
# dropping every group to icon-only when a tab is slightly too wide.
# r10: center compact NavigationRail icons in the button hit target.
# r11: reserve room beside overlay scrollbars in dialog form bodies.
# r12: scroll narrow data tables horizontally at their minimum column widths.
# r13 (D-306): live semantic agent providers, private Unix RPC and CLI/MCP.
EGIT_COMMIT="1d14371469f6684005846b74db306c0201bd1d6e"

# LGPL-3+ toolkit, ISC Lucide icons, MIT vendored CLAP plugin headers.
LICENSE="LGPL-3+ ISC MIT"
SLOT="0"
KEYWORDS="~amd64"
IUSE="+alsa +cups examples +ffmpeg +pdf +qindaqt +quick3d test +widgets"
REQUIRED_USE="cups? ( widgets )"
RESTRICT="!test? ( test )"

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
