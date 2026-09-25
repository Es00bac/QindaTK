// SPDX-License-Identifier: LGPL-3.0-or-later
// qindatk-test-broken.clap: a CLAP binary whose entry crashes while it is
// initialised, as a damaged plugin would during a scan. The scan runs out of
// process, so the registry records the crash and the host keeps running
// (tst_audio_plugin_host::scanSurvivesBrokenBinaries).
#include <clap/clap.h>

#include <csignal>
#if defined(__linux__)
#include <sys/prctl.h>
#endif

namespace {

bool crashingInit(const char *)
{
#if defined(__linux__)
    ::prctl(PR_SET_DUMPABLE, 0, 0, 0, 0); // no core dump for a deliberate crash
#endif
    std::raise(SIGSEGV);
    return false;
}
void deinit() {}
const void *factory(const char *) { return nullptr; }

} // namespace

// clap/entry.h declares clap_entry with CLAP_EXPORT; repeating the attribute
// on this definition is ignored by GCC (-Wattributes).
extern "C" {
const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, crashingInit, deinit, factory};
}
