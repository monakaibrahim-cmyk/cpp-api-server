# mod_jwt: OpenSSL Tokenization & Authentication Module

`mod_jwt` is a modular C++ extension providing compiled, high-performance RFC 7519 JSON Web Token (JWT) signing, verification, and authentication management using OpenSSL 3.x EVP routines.

---

## Features

- **Asymmetric Cryptography**: Full support for RS256 (RSA with SHA-256) and ES256 (ECDSA P-256 with SHA-256) using standard PKCS#1 and PKCS#8 PEM private/public keypairs.
- **Symmetric Fallback**: Support for HS256 (HMAC-SHA256) shared secrets.
- **Compiled C++ Performance**: All cryptographic hashing, signature generation, signature verification, and Base64URL encoding/decoding run in native C++.
- **Service Registry Integration**: Registers `jwt_engine` into `api::s_services()` for direct C++ microservice and module consumption.
- **Dynamic Lua Bindings**: Exposes high-level `JWT` functions in the Lua environment for route script handlers.
- **Native REST Endpoints**: Pre-built C++ endpoints for health status, public PEM key distribution, and Bearer token verification.

---

## Keypair Generation

To generate an RSA 2048-bit keypair for production or development:

```bash
mkdir -p config/certs
openssl genpkey -algorithm RSA -out config/certs/jwt_private.pem -pkeyopt rsa_keygen_bits:2048
openssl rsa -pubout -in config/certs/jwt_private.pem -out config/certs/jwt_public.pem
```

---

## C++ Native Usage

```cpp
#include "jwt_engine.h"
#include "api/service_registry.h"

// Retrieve service from global registry
auto jwt_service = api::s_services().get_service<api::jwt_engine>("jwt");

if (jwt_service && jwt_service->has_private_key())
{
    // Sign token (1 hour expiration)
    std::string token = jwt_service->sign("{\"sub\":\"user_123\",\"role\":\"admin\"}", 3600);

    // Verify token
    auto verification = jwt_service->verify(token);
    if (verification.is_valid)
    {
        // verification.subject == "user_123"
        // verification.payload_json contains full decoded JSON
    }
}
```

---

## Lua Route Usage

### 1. Token Issuing Endpoint

```lua
route("POST", "/api/auth/token", function(req)
    local token = JWT.sign({
        sub = "user_42",
        email = "alice@example.com",
        role = "administrator"
    }, { ttl = 3600 })

    return {
        status = 200,
        body = json.encode({
            access_token = token,
            token_type = "Bearer",
            expires_in = 3600
        }),
        content_type = "application/json"
    }
end)
```

### 2. Protected Route with Token Verification

```lua
route("GET", "/api/v1/profile", function(req)
    local token = JWT.extract_bearer(req)
    if token == "" then
        return { status = 401, body = '{"error":"Missing Bearer token"}' }
    end

    local claims, error_message = JWT.verify(token)
    if not claims then
        return { status = 401, body = json.encode({ error = error_message or "Unauthorized" }) }
    end

    return {
        status = 200,
        body = json.encode({
            message = "Access granted",
            user_id = claims.sub,
            email = claims.email
        }),
        content_type = "application/json"
    }
end)
```
