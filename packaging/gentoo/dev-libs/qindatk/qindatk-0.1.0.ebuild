# Copyright 2026 QindaTK contributors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

# AGENT-NOTE: like the other QindaQt overlay packages this pins a commit of
# the local development checkout until the repository is hosted; then
# replace git-r3 with a SRC_URI tarball. The version here, the CMake
# project VERSION and the git tag move together.
inherit cmake git-r3

DESCRIPTION="QindaTK - Qt6/QML toolkit for dense, CSS-grade desktop layouts (Flex, Grid, docking, theme)"
HOMEPAGE="https://github.com/Es00bac/QindaQt"
# The bare repository both checkouts push to, then either machine's working
# tree: git-r3 takes the first URI that exists, so the same ebuild builds on
# the desktop (where the bare repo and ~/work_SPaC3 live) and on the laptop
# (where only ~/work_space does). EGIT_COMMIT is what makes the source
# identical either way.
EGIT_REPO_URI="file:///home/cabewse/git/QindaTK.git
	file:///home/cabewse/work_SPaC3/QindaTK
	file:///home/cabewse/work_space/QindaTK"
# AGENT-NOTE: an immutable pin, not a branch - a package built twice must
# be the same package. Replace with the tag's commit when v0.1.0 is tagged.
EGIT_COMMIT="807ad562fc86b022fe702a3847eb954c72f4cb31"

LICENSE="LGPL-3+ ISC"
SLOT="0"
KEYWORDS="~amd64"
IUSE="examples test"
RESTRICT="!test? ( test )"

RDEPEND="
	>=dev-qt/qtbase-6.11:6=[gui]
	>=dev-qt/qtdeclarative-6.11:6=
	>=dev-qt/qtsvg-6.11:6=
"
DEPEND="${RDEPEND}"
BDEPEND="
	app-alternatives/ninja
"

src_configure() {
	local mycmakeargs=(
		-DBUILD_TESTING=$(usex test)
		-DQINDATK_BUILD_EXAMPLES=$(usex examples)
		-DQINDATK_BUILD_TOOLS=ON
		-DQINDATK_QML_INSTALL_DIR="${EPREFIX}/usr/$(get_libdir)/qt6/qml"
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
