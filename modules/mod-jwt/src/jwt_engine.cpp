#include <jwt_engine.h>
#include <core/logger.h>
#include <scripting/lua_engine.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

#include <crow/json.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>

namespace api
{

jwt_engine::jwt_engine(const JwtConfig &configuration)
    : configuration_(configuration)
{
    if (!configuration_.private_key_path.empty())
    {
        load_private_key_file(configuration_.private_key_path,
                              configuration_.private_key_passphrase);
    }

    if (!configuration_.public_key_path.empty())
    {
        load_public_key_file(configuration_.public_key_path);
    }
}

jwt_engine::~jwt_engine()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (private_key_)
    {
        EVP_PKEY_free(private_key_);
        private_key_ = nullptr;
    }

    if (public_key_)
    {
        EVP_PKEY_free(public_key_);
        public_key_ = nullptr;
    }
}

bool jwt_engine::load_private_key_file(const std::string &file_path,
                                       const std::string &passphrase)
{
    std::ifstream input_stream(file_path);

    if (!input_stream.is_open())
    {
        LOG_ERROR("jwt", "Unable to open private key file: " << file_path);
        return false;
    }

    std::stringstream buffer_stream;

    buffer_stream << input_stream.rdbuf();

    return load_private_key_pem(buffer_stream.str(), passphrase);
}

bool jwt_engine::load_private_key_pem(const std::string &pem_content,
                                      const std::string &passphrase)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (private_key_)
    {
        EVP_PKEY_free(private_key_);
        private_key_ = nullptr;
    }

    BIO *bio_buffer = BIO_new_mem_buf(pem_content.data(),
                                      static_cast<int>(pem_content.size()));

    if (!bio_buffer)
    {
        LOG_ERROR("jwt", "Failed to allocate memory BIO for private key");
        return false;
    }

    private_key_ = PEM_read_bio_PrivateKey(
        bio_buffer, nullptr, nullptr,
        passphrase.empty() ? nullptr : const_cast<char *>(passphrase.c_str()));

    BIO_free(bio_buffer);

    if (!private_key_)
    {
        char error_buffer[256];

        ERR_error_string_n(ERR_get_error(), error_buffer, sizeof(error_buffer));
        LOG_ERROR("jwt", "Failed to parse PEM private key: " << error_buffer);

        return false;
    }

    LOG_INFO("jwt", "Successfully loaded PEM private key");

    if (!public_key_)
    {
        BIO *public_bio = BIO_new(BIO_s_mem());

        if (public_bio)
        {
            if (PEM_write_bio_PUBKEY(public_bio, private_key_) == 1)
            {
                char *data_pointer = nullptr;
                long data_length = BIO_get_mem_data(public_bio, &data_pointer);

                if (data_length > 0 && data_pointer)
                {
                    public_key_pem_cache_ = std::string(
                        data_pointer, static_cast<size_t>(data_length));
                }
            }

            BIO_free(public_bio);
        }
    }

    return true;
}

bool jwt_engine::load_public_key_file(const std::string &file_path)
{
    std::ifstream input_stream(file_path);

    if (!input_stream.is_open())
    {
        LOG_ERROR("jwt", "Unable to open public key file: " << file_path);
        return false;
    }

    std::stringstream buffer_stream;

    buffer_stream << input_stream.rdbuf();

    return load_public_key_pem(buffer_stream.str());
}

bool jwt_engine::load_public_key_pem(const std::string &pem_content)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (public_key_)
    {
        EVP_PKEY_free(public_key_);
        public_key_ = nullptr;
    }

    BIO *bio_buffer = BIO_new_mem_buf(pem_content.data(),
                                      static_cast<int>(pem_content.size()));

    if (!bio_buffer)
    {
        LOG_ERROR("jwt", "Failed to allocate memory BIO for public key");
        return false;
    }

    public_key_ = PEM_read_bio_PUBKEY(bio_buffer, nullptr, nullptr, nullptr);
    BIO_free(bio_buffer);

    if (!public_key_)
    {
        char error_buffer[256];

        ERR_error_string_n(ERR_get_error(), error_buffer, sizeof(error_buffer));
        LOG_ERROR("jwt", "Failed to parse PEM public key: " << error_buffer);

        return false;
    }

    public_key_pem_cache_ = pem_content;
    LOG_INFO("jwt", "Successfully loaded PEM public key");

    return true;
}

void jwt_engine::set_secret(const std::string &secret)
{
    std::lock_guard<std::mutex> lock(mutex_);

    configuration_.secret = secret;
}

void jwt_engine::set_algorithm(const std::string &algorithm)
{
    std::lock_guard<std::mutex> lock(mutex_);

    configuration_.algorithm = algorithm;
}

void jwt_engine::set_issuer(const std::string &issuer)
{
    std::lock_guard<std::mutex> lock(mutex_);

    configuration_.issuer = issuer;
}

void jwt_engine::set_default_time_to_live(int time_to_live_seconds)
{
    std::lock_guard<std::mutex> lock(mutex_);

    configuration_.default_time_to_live_seconds = time_to_live_seconds;
}

std::string jwt_engine::get_algorithm() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return configuration_.algorithm;
}

std::string jwt_engine::get_issuer() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return configuration_.issuer;
}

bool jwt_engine::has_private_key() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return private_key_ != nullptr || !configuration_.secret.empty();
}

bool jwt_engine::has_public_key() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return public_key_ != nullptr || private_key_ != nullptr ||
           !configuration_.secret.empty();
}

std::string jwt_engine::get_public_key_pem() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return public_key_pem_cache_;
}

std::string jwt_engine::sign(const std::string &payload_json,
                             int time_to_live_seconds)
{
    std::lock_guard<std::mutex> lock(mutex_);

    int active_time_to_live = (time_to_live_seconds > 0)
                                  ? time_to_live_seconds
                                  : configuration_.default_time_to_live_seconds;

    auto current_time_point = std::chrono::system_clock::now();
    int64_t current_timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(
            current_time_point.time_since_epoch())
            .count();
    int64_t expiration_timestamp = current_timestamp + active_time_to_live;

    crow::json::wvalue header_json;

    header_json["alg"] = configuration_.algorithm;
    header_json["typ"] = "JWT";

    std::string header_string = header_json.dump();
    std::string encoded_header = base64url_encode(header_string);

    auto parsed_payload = crow::json::load(payload_json);
    crow::json::wvalue payload_writable;

    if (parsed_payload)
    {
        for (const auto &key_name : parsed_payload.keys())
        {
            const auto &element = parsed_payload[key_name];

            switch (element.t())
            {
            case crow::json::type::Number:
                if (element.nt() == crow::json::num_type::Floating_point ||
                    element.nt() ==
                        crow::json::num_type::Double_precision_floating_point)
                {
                    payload_writable[key_name] = element.d();
                }
                else
                {
                    payload_writable[key_name] = element.i();
                }
                break;
            case crow::json::type::String:
                payload_writable[key_name] = std::string(element.s());
                break;
            case crow::json::type::True:
                payload_writable[key_name] = true;
                break;
            case crow::json::type::False:
                payload_writable[key_name] = false;
                break;
            default:
                break;
            }
        }
    }

    if (!payload_writable.count("iat"))
    {
        payload_writable["iat"] = current_timestamp;
    }

    if (!payload_writable.count("nbf"))
    {
        payload_writable["nbf"] = current_timestamp;
    }

    if (!payload_writable.count("exp"))
    {
        payload_writable["exp"] = expiration_timestamp;
    }

    if (!payload_writable.count("iss") && !configuration_.issuer.empty())
    {
        payload_writable["iss"] = configuration_.issuer;
    }

    std::string payload_string = payload_writable.dump();
    std::string encoded_payload = base64url_encode(payload_string);

    std::string signing_input = encoded_header + "." + encoded_payload;
    std::string signature_bytes = sign_digest(signing_input);

    if (signature_bytes.empty())
    {
        LOG_ERROR("jwt", "Token signature generation failed");
        return "";
    }

    std::string encoded_signature = base64url_encode(signature_bytes);

    return signing_input + "." + encoded_signature;
}

std::string jwt_engine::sign_claims(
    const std::unordered_map<std::string, std::string> &claims,
    int time_to_live_seconds)
{
    crow::json::wvalue payload_json;

    for (const auto &[claim_key, claim_value] : claims)
    {
        payload_json[claim_key] = claim_value;
    }

    return sign(payload_json.dump(), time_to_live_seconds);
}

std::string jwt_engine::sign_digest(const std::string &signing_input)
{
    if (configuration_.algorithm == "HS256")
    {
        if (configuration_.secret.empty())
        {
            LOG_ERROR("jwt", "HS256 signing requires secret");
            return "";
        }

        unsigned char hmac_buffer[EVP_MAX_MD_SIZE];
        unsigned int hmac_length = 0;

        HMAC(EVP_sha256(), configuration_.secret.data(),
             static_cast<int>(configuration_.secret.size()),
             reinterpret_cast<const unsigned char *>(signing_input.data()),
             signing_input.size(), hmac_buffer, &hmac_length);

        return std::string(reinterpret_cast<const char *>(hmac_buffer),
                           hmac_length);
    }

    if (!private_key_)
    {
        LOG_ERROR("jwt",
                  "Cannot sign token: No private key loaded for algorithm "
                      << configuration_.algorithm);
        return "";
    }

    EVP_MD_CTX *digest_context = EVP_MD_CTX_new();

    if (!digest_context)
    {
        return "";
    }

    if (EVP_DigestSignInit(digest_context, nullptr, EVP_sha256(), nullptr,
                           private_key_) <= 0)
    {
        EVP_MD_CTX_free(digest_context);
        return "";
    }

    if (EVP_DigestSignUpdate(digest_context, signing_input.data(),
                             signing_input.size()) <= 0)
    {
        EVP_MD_CTX_free(digest_context);
        return "";
    }

    size_t signature_length = 0;

    if (EVP_DigestSignFinal(digest_context, nullptr, &signature_length) <= 0)
    {
        EVP_MD_CTX_free(digest_context);
        return "";
    }

    std::vector<unsigned char> signature_buffer(signature_length);

    if (EVP_DigestSignFinal(digest_context, signature_buffer.data(),
                            &signature_length) <= 0)
    {
        EVP_MD_CTX_free(digest_context);
        return "";
    }

    EVP_MD_CTX_free(digest_context);

    return std::string(reinterpret_cast<const char *>(signature_buffer.data()),
                       signature_length);
}

bool jwt_engine::verify_digest(const std::string &signing_input,
                               const std::string &signature_bytes)
{
    if (configuration_.algorithm == "HS256")
    {
        if (configuration_.secret.empty())
        {
            return false;
        }

        unsigned char hmac_buffer[EVP_MAX_MD_SIZE];
        unsigned int hmac_length = 0;

        HMAC(EVP_sha256(), configuration_.secret.data(),
             static_cast<int>(configuration_.secret.size()),
             reinterpret_cast<const unsigned char *>(signing_input.data()),
             signing_input.size(), hmac_buffer, &hmac_length);

        std::string expected_signature(
            reinterpret_cast<const char *>(hmac_buffer), hmac_length);

        return expected_signature == signature_bytes;
    }

    EVP_PKEY *active_verification_key =
        public_key_ ? public_key_ : private_key_;

    if (!active_verification_key)
    {
        LOG_ERROR("jwt", "Cannot verify token: No verification key loaded for "
                             << configuration_.algorithm);
        return false;
    }

    EVP_MD_CTX *digest_context = EVP_MD_CTX_new();

    if (!digest_context)
    {
        return false;
    }

    if (EVP_DigestVerifyInit(digest_context, nullptr, EVP_sha256(), nullptr,
                             active_verification_key) <= 0)
    {
        EVP_MD_CTX_free(digest_context);
        return false;
    }

    if (EVP_DigestVerifyUpdate(digest_context, signing_input.data(),
                               signing_input.size()) <= 0)
    {
        EVP_MD_CTX_free(digest_context);
        return false;
    }

    int verification_code = EVP_DigestVerifyFinal(
        digest_context,
        reinterpret_cast<const unsigned char *>(signature_bytes.data()),
        signature_bytes.size());

    EVP_MD_CTX_free(digest_context);

    return verification_code == 1;
}

JwtVerificationResult jwt_engine::verify(const std::string &token)
{
    JwtVerificationResult verification_result;

    if (token.empty())
    {
        verification_result.error_message = "Token string is empty";
        return verification_result;
    }

    size_t first_dot_position = token.find('.');
    size_t second_dot_position = (first_dot_position != std::string::npos)
                                     ? token.find('.', first_dot_position + 1)
                                     : std::string::npos;

    if (first_dot_position == std::string::npos ||
        second_dot_position == std::string::npos ||
        token.find('.', second_dot_position + 1) != std::string::npos)
    {
        verification_result.error_message =
            "Invalid JWT token format (expected 3 dot-separated segments)";
        return verification_result;
    }

    std::string encoded_header = token.substr(0, first_dot_position);
    std::string encoded_payload = token.substr(
        first_dot_position + 1, second_dot_position - first_dot_position - 1);
    std::string encoded_signature = token.substr(second_dot_position + 1);

    std::string decoded_header = base64url_decode(encoded_header);
    auto parsed_header = crow::json::load(decoded_header);

    if (!parsed_header)
    {
        verification_result.error_message =
            "Invalid token header: JSON decoding failure";
        return verification_result;
    }

    std::string algorithm_in_header;

    if (parsed_header.has("alg"))
    {
        algorithm_in_header = std::string(parsed_header["alg"].s());
    }

    std::unique_lock<std::mutex> lock(mutex_);

    if (algorithm_in_header != configuration_.algorithm)
    {
        verification_result.error_message =
            "Token algorithm mismatch (expected " + configuration_.algorithm +
            ", got " + algorithm_in_header + ")";
        return verification_result;
    }

    std::string signing_input = encoded_header + "." + encoded_payload;
    std::string signature_bytes = base64url_decode(encoded_signature);

    if (!verify_digest(signing_input, signature_bytes))
    {
        verification_result.error_message =
            "Cryptographic signature verification failed";
        return verification_result;
    }

    std::string decoded_payload = base64url_decode(encoded_payload);
    auto parsed_payload = crow::json::load(decoded_payload);

    if (!parsed_payload)
    {
        verification_result.error_message =
            "Invalid token payload: JSON decoding failure";
        return verification_result;
    }

    auto current_time_point = std::chrono::system_clock::now();
    int64_t current_timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(
            current_time_point.time_since_epoch())
            .count();

    if (parsed_payload.has("exp"))
    {
        int64_t expiration_timestamp = parsed_payload["exp"].i();

        verification_result.expires_at = expiration_timestamp;

        if (current_timestamp >
            expiration_timestamp + configuration_.clock_tolerance_seconds)
        {
            verification_result.error_message =
                "Token has expired (exp claim violation)";
            return verification_result;
        }
    }

    if (parsed_payload.has("nbf"))
    {
        int64_t not_before_timestamp = parsed_payload["nbf"].i();

        if (current_timestamp <
            not_before_timestamp - configuration_.clock_tolerance_seconds)
        {
            verification_result.error_message =
                "Token not yet valid (nbf claim violation)";
            return verification_result;
        }
    }

    if (parsed_payload.has("iss"))
    {
        verification_result.issuer = std::string(parsed_payload["iss"].s());

        if (!configuration_.issuer.empty() &&
            verification_result.issuer != configuration_.issuer)
        {
            verification_result.error_message = "Token issuer mismatch";
            return verification_result;
        }
    }

    if (parsed_payload.has("sub"))
    {
        verification_result.subject = std::string(parsed_payload["sub"].s());
    }

    if (parsed_payload.has("iat"))
    {
        verification_result.issued_at = parsed_payload["iat"].i();
    }

    verification_result.is_valid = true;
    verification_result.payload_json = decoded_payload;

    return verification_result;
}

std::string
jwt_engine::extract_bearer_token(const std::string &authorization_header)
{
    if (authorization_header.empty())
    {
        return "";
    }

    std::string prefix = "Bearer ";

    if (authorization_header.size() > prefix.size())
    {
        std::string header_prefix =
            authorization_header.substr(0, prefix.size());

        bool matches = true;

        for (size_t index = 0; index < prefix.size(); ++index)
        {
            if (std::tolower(
                    static_cast<unsigned char>(header_prefix[index])) !=
                std::tolower(static_cast<unsigned char>(prefix[index])))
            {
                matches = false;
                break;
            }
        }

        if (matches)
        {
            return authorization_header.substr(prefix.size());
        }
    }

    return "";
}

std::string jwt_engine::base64url_encode(const std::string &input_bytes)
{
    static const char encoding_table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    std::string encoded_output;
    const size_t input_length = input_bytes.size();

    encoded_output.reserve(((input_length + 2) / 3) * 4);

    size_t index = 0;

    while (index + 3 <= input_length)
    {
        uint32_t octet_a = static_cast<uint8_t>(input_bytes[index]);
        uint32_t octet_b = static_cast<uint8_t>(input_bytes[index + 1]);
        uint32_t octet_c = static_cast<uint8_t>(input_bytes[index + 2]);
        index += 3;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        encoded_output.push_back(encoding_table[(triple >> 18) & 0x3F]);
        encoded_output.push_back(encoding_table[(triple >> 12) & 0x3F]);
        encoded_output.push_back(encoding_table[(triple >> 6) & 0x3F]);
        encoded_output.push_back(encoding_table[triple & 0x3F]);
    }

    if (index < input_length)
    {
        size_t remaining_bytes = input_length - index;
        uint32_t octet_a = static_cast<uint8_t>(input_bytes[index]);
        uint32_t octet_b = (remaining_bytes == 2)
                               ? static_cast<uint8_t>(input_bytes[index + 1])
                               : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8);

        encoded_output.push_back(encoding_table[(triple >> 18) & 0x3F]);
        encoded_output.push_back(encoding_table[(triple >> 12) & 0x3F]);

        if (remaining_bytes == 2)
        {
            encoded_output.push_back(encoding_table[(triple >> 6) & 0x3F]);
        }
    }

    return encoded_output;
}

std::string jwt_engine::base64url_decode(const std::string &base64url_string)
{
    std::string decoded_output;
    decoded_output.reserve((base64url_string.size() * 3) / 4);

    uint32_t accumulator = 0;
    int bits_collected = 0;

    for (unsigned char character : base64url_string)
    {
        int value = -1;

        if (character >= 'A' && character <= 'Z')
        {
            value = character - 'A';
        }
        else if (character >= 'a' && character <= 'z')
        {
            value = character - 'a' + 26;
        }
        else if (character >= '0' && character <= '9')
        {
            value = character - '0' + 52;
        }
        else if (character == '-' || character == '+')
        {
            value = 62;
        }
        else if (character == '_' || character == '/')
        {
            value = 63;
        }
        else if (character == '=')
        {
            break;
        }
        else if (std::isspace(character))
        {
            continue;
        }
        else
        {
            return "";
        }

        accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
        bits_collected += 6;

        if (bits_collected >= 8)
        {
            bits_collected -= 8;
            decoded_output.push_back(
                static_cast<char>((accumulator >> bits_collected) & 0xFF));
        }
    }

    return decoded_output;
}

void jwt_engine::bind_lua(lua_engine &lua_engine_instance)
{
    std::lock_guard<std::mutex> lock(lua_engine_instance.mutex());
    sol::table jwt_table =
        lua_engine_instance.state().create_named_table("JWT");

    jwt_table["sign"] = [this](sol::table claims_table,
                               sol::optional<sol::table> options) -> std::string
    {
        int time_to_live = -1;

        if (options && options.value().valid())
        {
            time_to_live = options.value().get_or("ttl", -1);

            if (time_to_live < 0)
            {
                time_to_live = options.value().get_or("expires_in", -1);
            }
        }

        std::string payload_json = lua_engine::lua_to_json(claims_table);

        return sign(payload_json, time_to_live);
    };

    jwt_table["verify"] = [this, &lua_engine_instance](
                              const std::string &token) -> sol::variadic_results
    {
        sol::variadic_results results;
        auto verification_result = verify(token);

        if (!verification_result.is_valid)
        {
            results.push_back(sol::make_object(
                lua_engine_instance.state().lua_state(), sol::nil));
            results.push_back(
                sol::make_object(lua_engine_instance.state().lua_state(),
                                 verification_result.error_message));

            return results;
        }

        auto parsed_json = crow::json::load(verification_result.payload_json);

        if (parsed_json)
        {
            results.push_back(lua_engine::json_to_lua(
                lua_engine_instance.state(), parsed_json));
        }
        else
        {
            results.push_back(lua_engine_instance.state().create_table());
        }

        return results;
    };

    jwt_table["extract_bearer"] = [](sol::object input_object) -> std::string
    {
        if (input_object.is<std::string>())
        {
            return extract_bearer_token(input_object.as<std::string>());
        }

        if (input_object.is<sol::table>())
        {
            sol::table request_table = input_object.as<sol::table>();
            sol::optional<sol::table> headers = request_table["headers"];

            if (headers)
            {
                std::string auth_header =
                    headers.value().get_or<std::string>("authorization", "");

                if (auth_header.empty())
                {
                    auth_header = headers.value().get_or<std::string>(
                        "Authorization", "");
                }

                return extract_bearer_token(auth_header);
            }
        }

        return "";
    };

    jwt_table["algorithm"] = [this]() -> std::string
    { return get_algorithm(); };

    jwt_table["public_key"] = [this]() -> std::string
    { return get_public_key_pem(); };

    jwt_table["has_private_key"] = [this]() -> bool
    { return has_private_key(); };

    jwt_table["has_public_key"] = [this]() -> bool { return has_public_key(); };

    jwt_table["issuer"] = [this]() -> std::string { return get_issuer(); };
}

} // namespace api
