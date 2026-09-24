#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Generate small project-owned ICC fixtures with Little CMS, not QindaTK.

Requires Python ctypes and liblcms2. Does not compute expected conversions.
Header dates are fixed for reproducible bytes on the same LCMS version.
"""
import ctypes as C
import ctypes.util
from pathlib import Path
import struct


class XyY(C.Structure):
    _fields_ = [("x", C.c_double), ("y", C.c_double), ("Y", C.c_double)]


class Triple(C.Structure):
    _fields_ = [("red", XyY), ("green", XyY), ("blue", XyY)]


lib = C.CDLL(ctypes.util.find_library("lcms2"))
lib.cmsCreate_sRGBProfile.restype = C.c_void_p
lib.cmsBuildGamma.argtypes = [C.c_void_p, C.c_double]
lib.cmsBuildGamma.restype = C.c_void_p
lib.cmsCreateRGBProfile.argtypes = [C.POINTER(XyY), C.POINTER(Triple), C.POINTER(C.c_void_p)]
lib.cmsCreateRGBProfile.restype = C.c_void_p
lib.cmsCreateGrayProfile.argtypes = [C.POINTER(XyY), C.c_void_p]
lib.cmsCreateGrayProfile.restype = C.c_void_p
lib.cmsSaveProfileToMem.argtypes = [C.c_void_p, C.c_void_p, C.POINTER(C.c_uint32)]
lib.cmsCloseProfile.argtypes = [C.c_void_p]
lib.cmsFreeToneCurve.argtypes = [C.c_void_p]
gamma = lib.cmsBuildGamma(None, 1.0)
white = XyY(0.3127, 0.3290, 1)
primaries = Triple(XyY(.64, .33, 1), XyY(.30, .60, 1), XyY(.15, .06, 1))
profiles = {
    "srgb.icc": lib.cmsCreate_sRGBProfile(),
    "linear-srgb.icc": lib.cmsCreateRGBProfile(C.byref(white), C.byref(primaries),
                                             (C.c_void_p * 3)(gamma, gamma, gamma)),
    "gray.icc": lib.cmsCreateGrayProfile(C.byref(white), gamma),
}
for name, profile in profiles.items():
    assert profile
    size = C.c_uint32()
    assert lib.cmsSaveProfileToMem(profile, None, C.byref(size))
    buffer = C.create_string_buffer(size.value)
    assert lib.cmsSaveProfileToMem(profile, buffer, C.byref(size))
    data = bytearray(buffer.raw)
    data[24:36] = struct.pack(">6H", 2026, 1, 1, 0, 0, 0)
    Path(__file__).with_name(name).write_bytes(data)
    lib.cmsCloseProfile(profile)
lib.cmsFreeToneCurve(gamma)
