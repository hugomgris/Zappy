#pragma once

#include "../../incs/Result.hpp"
#include <openssl/ssl.h>
#include <memory>

namespace zappy {
    class TlsContext {
        public:
            ~TlsContext();

            TlsContext(const TlsContext&) = delete;
            TlsContext& operator=(const TlsContext&) = delete;

            static TlsContext& instance();

            Result initialize(bool insecureMode = false);
            SSL_CTX* getCtx() const;
            bool isInitialized() const;

        private:
            TlsContext() = default;

            SSL_CTX* _ctx = nullptr;
            bool _initialized = false;
    };
} // namespae zappy
