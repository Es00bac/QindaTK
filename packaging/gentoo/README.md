<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Packaging the numerical color backend

A new ebuild revision pinned to a commit containing the WP-043 color engine
must add `>=media-libs/lcms-2.14:2` to `RDEPEND` (and inherited `DEPEND`), and
`virtual/pkgconfig` to `BDEPEND`. CMake finds `lcms2 >= 2.14` through pkg-config
and links it privately into the shared qindatk library. No LCMS development
headers leak into the installed public API. The tested backend is LCMS 2.19.

The existing 0.1.0 and r2 ebuilds remain immutable source pins predating this
backend; this source change does not repin or deploy those historical packages.
The next packaging revision must include these dependencies when updating its
source pin. Use the configured Portage MAKEOPTS unchanged.
