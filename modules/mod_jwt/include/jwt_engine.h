#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <sol/sol.hpp>

// Forward declaration of OpenSSL types to minimize header pollution
typedef struct evp_pkey_st EVP_PKEY;

namespace api
{

class lua_engine;

/**
 * @brief Configuration parameters for JWT cryptographic signing and
 * verification.
 */
struct JwtConfig
{
    /** @brief Filesystem path to the PEM private key file (e.g.
     * "config/certs/jwt_private.pem"). */
    std::string private_key_path;

    /** @brief Filesystem path to the PEM public key file (e.g.
     * "config/certs/jwt_public.pem"). */
    std::string public_key_path;

    /** @brief Optional passphrase for encrypted private keys. */
    std::string private_key_passphrase;

    /** @brief Secret key used for symmetric algorithms (HS256). */
    std::string secret;

    /** @brief Cryptographic signing algorithm ("RS256", "ES256", or "HS256").
     */
    std::string algorithm = "RS256";

    /** @brief Token issuer identifier claim ("iss"). */
    std::string issuer = "api-server";

    /** @brief Default token validity lifetime in seconds (1 hour default). */
    int default_time_to_live_seconds = 3600;

    /** @brief Clock skew tolerance leeway in seconds (60 seconds default). */
    int clock_tolerance_seconds = 60;
};

/**
 * @brief Evaluation result produced from token signature and claims
 * verification.
 */
struct JwtVerificationResult
{
    /** @brief True if signature is cryptographically valid and token is not
     * expired. */
    bool is_valid = false;

    /** @brief Decoded payload JSON string. */
    std::string payload_json;

    /** @brief Diagnostic error message if verification fails. */
    std::string error_message;

    /** @brief Subject claim ("sub") extracted from payload. */
    std::string subject;

    /** @brief Issuer claim ("iss") extracted from payload. */
    std::string issuer;

    /** @brief Issued-at timestamp in Unix seconds. */
    int64_t issued_at = 0;

    /** @brief Expiration timestamp in Unix seconds. */
    int64_t expires_at = 0;
};

/**
 * @brief High-performance C++ tokenization and asymmetric cryptographic engine.
 *
 * @details Implements RFC 7519 JSON Web Token signing and RFC 7515 JSON Web
 * Signature verification using OpenSSL 3.x EVP routines. Supports:
 * - Asymmetric RSA (RS256) with PEM private and public keypairs.
 * - Asymmetric ECDSA (ES256) with P-256 curve and PEM keypairs.
 * - Symmetric HMAC (HS256) shared secrets.
 *
 * All cryptographic operations, key parsing, hashing, base64url encoding, and
 * claim validation are executed entirely in compiled C++ for maximum
 * security and throughput.
 *
 * @par C++ Signing Example
 * @code{.cpp}
 * api::JwtConfig configuration;
 * configuration.private_key_path = "config/certs/jwt_private.pem";
 * configuration.algorithm = "RS256";
 *
 * api::jwt_engine engine(configuration);
 * std::string token = engine.sign("{\"sub\":\"42\",\"role\":\"admin\"}", 3600);
 * @endcode
 *
 * @par C++ Verification Example
 * @code{.cpp}
 * api::JwtConfig configuration;
 * configuration.public_key_path = "config/certs/jwt_public.pem";
 *
 * api::jwt_engine engine(configuration);
 * auto result = engine.verify(token);
 * if (result.is_valid)
 * {
 *     LOG_INFO("auth", "Authenticated subject: " << result.subject);
 * }
 * @endcode
 *
 * @thread_safety All public methods are synchronized using an internal mutex
 * and are safe to invoke concurrently across worker threads.
 *
 * @headerfile jwt_engine.h
 */
class jwt_engine
{
  public:
    /**
     * @brief Constructs the token engine with the provided configuration.
     *
     * @param configuration Active JWT configuration parameters.
     */
    explicit jwt_engine(const JwtConfig &configuration = JwtConfig{});

    /**
     * @brief Destructor releasing internal OpenSSL EVP_PKEY key handles.
     */
    ~jwt_engine();

    /**
     * @brief Deleted copy constructor.
     */
    jwt_engine(const jwt_engine &) = delete;

    /**
     * @brief Deleted copy assignment operator.
     */
    jwt_engine &operator=(const jwt_engine &) = delete;

    /**
     * @brief Loads an asymmetric private key from a PEM file on the filesystem.
     *
     * @param file_path Filesystem path to the private key PEM file.
     * @param passphrase Optional decryption passphrase if the PEM is encrypted.
     * @return true If key was successfully parsed and loaded; false otherwise.
     */
    bool load_private_key_file(const std::string &file_path,
                               const std::string &passphrase = "");

    /**
     * @brief Loads an asymmetric private key directly from a PEM-encoded string
     * buffer.
     *
     * @param pem_content String containing PEM data ("-----BEGIN ...
     * PRIVATE...").
     * @param passphrase Optional decryption passphrase if the PEM is encrypted.
     * @return true If key was successfully parsed; false otherwise.
     */
    bool load_private_key_pem(const std::string &pem_content,
                              const std::string &passphrase = "");

    /**
     * @brief Loads an asymmetric public key from a PEM file on the filesystem.
     *
     * @param file_path Filesystem path to the public key PEM file.
     * @return true If public key was successfully parsed and loaded; false
     * otherwise.
     */
    bool load_public_key_file(const std::string &file_path);

    /**
     * @brief Loads an asymmetric public key directly from a PEM-encoded string
     * buffer.
     *
     * @param pem_content String containing PEM data ("-----BEGIN PUBLIC
     * KEY-----").
     * @return true If public key was successfully parsed; false otherwise.
     */
    bool load_public_key_pem(const std::string &pem_content);

    /**
     * @brief Configures secret key used for symmetric HMAC-SHA256 (HS256)
     * signing.
     *
     * @param secret Shared secret string.
     */
    void set_secret(const std::string &secret);

    /**
     * @brief Sets the active signing and verification algorithm ("RS256",
     * "ES256", "HS256").
     *
     * @param algorithm Target algorithm identifier.
     */
    void set_algorithm(const std::string &algorithm);

    /**
     * @brief Sets the token issuer identifier claim ("iss").
     *
     * @param issuer Token issuer string.
     */
    void set_issuer(const std::string &issuer);

    /**
     * @brief Sets the default token time-to-live lifetime in seconds.
     *
     * @param time_to_live_seconds Duration in seconds.
     */
    void set_default_time_to_live(int time_to_live_seconds);

    /**
     * @brief Signs a JSON payload into a compact RFC 7519 JSON Web Token
     * string.
     *
     * Automatically injects standard claims ("iat", "exp", "nbf", "iss", "jti")
     * if not already present in the payload.
     *
     * @param payload_json String containing JSON claims to serialize into
     * payload.
     * @param time_to_live_seconds Optional lifetime in seconds (-1 uses
     * default).
     * @return std::string Compact three-segment JWT
     * ("header.payload.signature").
     */
    std::string sign(const std::string &payload_json,
                     int time_to_live_seconds = -1);

    /**
     * @brief Signs a map of key-value string claims into a compact JWT.
     *
     * @param claims Map of claim keys and string values.
     * @param time_to_live_seconds Optional lifetime in seconds (-1 uses
     * default).
     * @return std::string Compact three-segment JWT.
     */
    std::string
    sign_claims(const std::unordered_map<std::string, std::string> &claims,
                int time_to_live_seconds = -1);

    /**
     * @brief Cryptographically verifies and decodes a compact JSON Web Token.
     *
     * Validates:
     * 1. Three-segment format ("header.payload.signature").
     * 2. Header algorithm matches configured algorithm (prevents substitution
     * attacks).
     * 3. Cryptographic signature matches public key (or secret for HS256).
     * 4. Expiration timestamp ("exp") with clock tolerance leeway.
     * 5. Not-before timestamp ("nbf") with clock tolerance leeway.
     * 6. Issuer ("iss") matches configured issuer if set.
     *
     * @param token Compact three-segment JWT string.
     * @return JwtVerificationResult Complete verification details and claims.
     */
    JwtVerificationResult verify(const std::string &token);

    /**
     * @brief Extracts bearer token from an HTTP Authorization header.
     *
     * Parses headers matching "Bearer <token>" format (case-insensitive
     * prefix).
     *
     * @param authorization_header Full value of the HTTP Authorization header.
     * @return std::string Extracted token string, or empty if prefix is absent.
     */
    static std::string
    extract_bearer_token(const std::string &authorization_header);

    /**
     * @brief Returns the active PEM public key string for distribution or
     * inspection.
     *
     * @return std::string PEM-encoded public key.
     */
    std::string get_public_key_pem() const;

    /**
     * @brief Returns the configured algorithm identifier string (e.g. "RS256").
     *
     * @return std::string Active algorithm.
     */
    std::string get_algorithm() const;

    /**
     * @brief Returns the configured issuer identifier string.
     *
     * @return std::string Active issuer.
     */
    std::string get_issuer() const;

    /**
     * @brief Checks whether a valid private key is currently loaded.
     *
     * @return true If private key is available for signing; false otherwise.
     */
    bool has_private_key() const;

    /**
     * @brief Checks whether a valid public key is currently loaded.
     *
     * @return true If public key is available for verification; false
     * otherwise.
     */
    bool has_public_key() const;

    /**
     * @brief Encodes binary or text string into Base64URL without padding (RFC
     * 7515).
     *
     * @param input_bytes Raw string data.
     * @return std::string URL-safe Base64 encoded string.
     */
    static std::string base64url_encode(const std::string &input_bytes);

    /**
     * @brief Decodes a Base64URL string back into raw bytes.
     *
     * @param base64url_string URL-safe Base64 encoded string.
     * @return std::string Decoded binary or text string.
     */
    static std::string base64url_decode(const std::string &base64url_string);

    /**
     * @brief Binds high-level C++ tokenization helpers to the global @c JWT Lua
     * table.
     *
     * @param lua_engine_instance Active Lua engine instance.
     */
    void bind_lua(lua_engine &lua_engine_instance);

  private:
    std::string sign_digest(const std::string &signing_input);
    bool verify_digest(const std::string &signing_input,
                       const std::string &signature_bytes);

    mutable std::mutex mutex_;
    JwtConfig configuration_;
    EVP_PKEY *private_key_ = nullptr;
    EVP_PKEY *public_key_ = nullptr;
    std::string public_key_pem_cache_;
};

} // namespace api
