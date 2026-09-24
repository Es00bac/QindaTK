<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed WP-043 numerical color consumer

This standalone project compiles against staged public QindaTK headers and
links only the installed `QindaTK::qindatk` target. It resolves built-in ICC
profiles, converts sRGB to linear RGB, and checks the returned profile tag,
context generation, exact straight alpha and expected midpoint. The consumer
does not include or link Little CMS directly.

Configure it against a refreshed Qt 6.11 or 6.8 QindaTK stage, then build and
run it with that Qt version on the library path. Direct builds must use the
configured `MAKEOPTS` job and load limits; never override Portage's setting.
