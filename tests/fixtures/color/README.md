<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Small ICC fixtures

These files were generated with `python generate.py` using Little CMS 2.19.
They contain standard mathematical color definitions, no third-party device
measurements. The generator and fixtures are distributed under LGPL-3.0-or-later;
the profiles' generated copyright tag says “No copyright, use freely”.
The generator fixes header dates to 2026-01-01. Regeneration with a different
LCMS version may change serialization; tests read the committed bytes.

| File | Definition | SHA-256 |
| --- | --- | --- |
| `srgb.icc` | LCMS standard sRGB matrix/TRC | `81d313ce695fe3d921806c24018e87561b90127a69057797cfe20f9b508359b8` |
| `linear-srgb.icc` | D65/sRGB primaries with gamma 1 | `b9926fc5879ae1b05cd0895c1ec9c7b630b9bde71f6c49f094a266aecc183546` |
| `gray.icc` | D65 gray, gamma 1; unsupported-model fixture | `d6928b60da0dba51cd6d5fddcd6f5c70d2bf6f906b58dd1e13ee3286011f3a01` |

Expected RGB conversions come from the independent piecewise sRGB equations
in tst_color_engine.cpp, not LCMS output. For example, encoded `0.5` decodes
to `0.21404114048223255`; linear `0.5` encodes to `0.7353569830524495`.
ICC's signed 16.16 serialization quantizes the sRGB curve coefficients to
`g=157286/65536`, `a=62119/65536`, `b=3417/65536`, `c=5072/65536`,
`d=2651/65536`. The stored curve is `(a*x+b)^g` for `x >= d`, otherwise
`c*x`. Tests evaluate that equation and its inverse independently, with
absolute tolerance `2e-6` for Float32/matrix processing. They separately check
the ideal sRGB 0.5 conversions within `1e-5`. On LCMS 2.19, serialized-profile
0.5 conversions are `0.2140451968` and `0.7353545427`; treating the unquantized
ideal curve as an exact ICC oracle would be incorrect. Malformed fixtures are derived in memory by truncating
the file or corrupting its header/tag offsets. These fixtures do not establish
LUT gamut mapping, proofing, nonrelative intents or CMYK behavior.
