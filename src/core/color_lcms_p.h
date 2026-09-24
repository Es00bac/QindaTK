// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QString>
#include <lcms2.h>

namespace QindaTK::ColorLcms {

struct Context {
    QString diagnostic;
    cmsContext handle = cmsCreateContext(nullptr, this);
    Context()
    {
        if (handle) cmsSetLogErrorHandlerTHR(handle, [](cmsContext context, cmsUInt32Number,
                                                       const char *message) {
            auto *self = static_cast<Context *>(cmsGetContextUserData(context));
            if (self) self->diagnostic = QString::fromUtf8(message);
        });
    }
    ~Context() { if (handle) cmsDeleteContext(handle); }
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;
};
struct Profile {
    cmsHPROFILE handle = nullptr;
    ~Profile() { if (handle) cmsCloseProfile(handle); }
    Profile(const Profile &) = delete;
    Profile &operator=(const Profile &) = delete;
    explicit Profile(cmsHPROFILE value) : handle(value) {}
};

} // namespace QindaTK::ColorLcms
