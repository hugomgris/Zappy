#include "TlsContext.hpp"

#include "../helpers/Logger.hpp"
#include "../../incs/result.hpp"
#include <openssl/err.h>
#include <openssl/x509.h>

// Custom callback to accept any certificate in insecure mode
static int insecure_mode_verify_callback(int ok, X509_STORE_CTX* ctx) {
    (void)ok;   // Unused in insecure mode
    (void)ctx;  // Unused in insecure mode
    // Always return 1 (OK/accept) in insecure mode
    return 1;
}

TlsContext::~TlsContext() {
    if (_ctx) {
        SSL_CTX_free(_ctx);
        _ctx = nullptr;
    }
}

TlsContext& TlsContext::instance() {
    static TlsContext g_instance;
    return g_instance;
}

Result TlsContext::initialize(bool insecureMode) {
    if (_initialized) {
        return Result::success();
    }

    // Create SSL context for TLS 1.2+
    _ctx = SSL_CTX_new(TLS_client_method());
    if (!_ctx) {
        const char* err = ERR_reason_error_string(ERR_get_error());
        std::string msg = std::string("SSL_CTX_new failed: ") + (err ? err : "unknown error");
        Logger::error(msg);
        return Result::failure(ErrorCode::NetworkError, msg);
    }

    // Set minimum TLS version to 1.2
    if (!SSL_CTX_set_min_proto_version(_ctx, TLS1_2_VERSION)) {
        const char* err = ERR_reason_error_string(ERR_get_error());
        std::string msg = std::string("SSL_CTX_set_min_proto_version failed: ") + (err ? err : "unknown error");
        Logger::error(msg);
        SSL_CTX_free(_ctx);
        _ctx = nullptr;
        return Result::failure(ErrorCode::NetworkError, msg);
    }

    // Certificate verification setup
    if (insecureMode) {
        // Skip certificate verification for testing
        SSL_CTX_set_verify(_ctx, SSL_VERIFY_NONE, insecure_mode_verify_callback);
        
        // Additional options to accept self-signed/invalid certs
        SSL_CTX_set_options(_ctx, SSL_OP_NO_QUERY_MTU);
        
        Logger::warn("TLS: Certificate verification disabled (insecure mode)");
    } else {
        // Enable certificate verification with system CA store
        SSL_CTX_set_verify(_ctx, SSL_VERIFY_PEER, nullptr);
        
        if (!SSL_CTX_set_default_verify_paths(_ctx)) {
            const char* err = ERR_reason_error_string(ERR_get_error());
            Logger::warn(std::string("Failed to load system CA store: ") + (err ? err : "unknown error"));
            // Non-fatal: continue with verification enabled
        }
    }

    _initialized = true;
    Logger::info("TLS context initialized successfully");
    return Result::success();
}

SSL_CTX* TlsContext::getCtx() const {
    return _ctx;
}

bool TlsContext::isInitialized() const {
    return _initialized;
}
