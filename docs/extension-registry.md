<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Local extension registry

`Tk.ExtensionRegistry` is a host-owned catalog and lifecycle manager for
explicitly selected, trusted native Qt plugins. Registering a descriptor only
reads local JSON. `load(id)` is the point at which native code enters the
process, and must be called only after the application chooses to trust that
extension. There is no global directory scan, marketplace, sandbox or promise
that arbitrary binary plugins are compatible. Extensions built for another
Qt/compiler/toolkit ABI need rebuilding. The versioned `ExtensionProvider`
interface is `<qindatk/extension_provider.h>`.

## Descriptor version 1

```json
{
  "schemaVersion": 1,
  "abiVersion": 1,
  "id": "org.example.counter",
  "version": "1.2.0",
  "library": "libexample-counter.so",
  "operations": ["increment"],
  "dependencies": [{"id": "org.example.base", "minVersion": "1.0"}],
  "requiresCapabilities": [{"featureId": "host.document", "operationId": "read"}],
  "providesCapabilities": [{"featureId": "counter", "operationId": "increment"}]
}
```

The host calls `registerDescriptor(absoluteLocalPath)`, inspects
`extension(id).diagnostic`, then explicitly calls `load(id)`. `library` is
relative to the descriptor directory or an absolute local path. Descriptors
are bounded to 256 KiB. IDs and operations use letters, digits, `.`, `-` and
`_`; versions are numeric dot-separated segments. Dependencies must already be
loaded at the requested minimum version. Required capabilities are checked
against the optional host-supplied `CapabilityRegistry`; a missing registry
cannot satisfy them. Incompatible schema or ABI, missing libraries and missing
dependencies appear in `diagnostic`. A rejected descriptor with a valid ID
remains visible so the application can explain why it failed.

`ExtensionProvider::start(savedState, error)` receives only that extension's
opaque saved map. A loaded provider may be called through
`invoke(id, declaredOperation, arguments)`. Operations absent from the
descriptor are rejected before reaching native code. The provider owns the
meaning and validation of its operation arguments. `stop()` releases its
resources. Unload refuses while another loaded extension depends on it.
`providesCapabilities` are published only after successful `start()`, using
backend ID `extension:<id>`, and are removed on unload. They are discovery
metadata; the host still invokes the provider explicitly.

`saveState()` returns one entry per known extension with ID, version,
descriptor path and provider state. `restoreState(snapshot)` preserves entries
even if their descriptor or library is absent. The host can later register a
matching descriptor and load it with that saved state. Unknown document
payloads and extension-owned data must remain in the application document;
unloading this registry never rewrites the document. Restoring is allowed only
while no extension is loaded. This is a local lifecycle API, not an automatic
installation or migration service.

The registry and plugins run on the GUI thread. A provider must not block the
UI or hand GUI `QObject`s to workers. Native plugins run in the application's
process with its permissions; applications needing isolation must use a
separate process adapter behind this host boundary. QML controls supplied by
plugins need separate application registration and accessible fallback UI.
