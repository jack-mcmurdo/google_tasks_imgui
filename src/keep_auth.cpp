#include "keep_auth.h"
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

#include <fstream>
#include <chrono>
#include <cstdlib>
#include <map>
#include <vector>

using json = nlohmann::json;

namespace KeepAuth {

namespace {

struct ServiceAccountKey {
    std::string client_email;
    std::string private_key;
    std::string token_uri;
};

struct CachedToken {
    std::string access_token;
    long long expires_at;
};

std::map<std::string, CachedToken> g_token_cache; // keyed by impersonated user email

long long now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string base64url_encode(const std::string& input) {
    static const char* alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(alphabet[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(alphabet[((val << 8) >> (valb + 8)) & 0x3F]);
    for (char& c : out) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    return out; // unpadded, matches base64url-without-padding required by JWT
}

bool rsa_sign_sha256(const std::string& private_key_pem, const std::string& data, std::string& signature_out) {
    BIO* bio = BIO_new_mem_buf(private_key_pem.data(), (int)private_key_pem.size());
    if (!bio) return false;
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) return false;

    bool ok = false;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx) {
        size_t sig_len = 0;
        if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) > 0 &&
            EVP_DigestSignUpdate(ctx, data.data(), data.size()) > 0 &&
            EVP_DigestSign(ctx, nullptr, &sig_len, nullptr, 0) > 0) {
            std::vector<unsigned char> sig(sig_len);
            if (EVP_DigestSign(ctx, sig.data(), &sig_len, (const unsigned char*)data.data(), data.size()) > 0) {
                signature_out.assign((const char*)sig.data(), sig_len);
                ok = true;
            }
        }
        EVP_MD_CTX_free(ctx);
    }
    EVP_PKEY_free(pkey);
    return ok;
}

bool load_service_account_key(const std::string& path, ServiceAccountKey& key) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j = json::parse(f);
        key.client_email = j.value("client_email", "");
        key.private_key = j.value("private_key", "");
        key.token_uri = j.value("token_uri", "https://oauth2.googleapis.com/token");
        return !key.client_email.empty() && !key.private_key.empty();
    } catch (...) {
        return false;
    }
}

// Deliberately narrower than client_secret.json's resolution order: no
// install-prefix fallback, since this key must never be bundled into a
// distributed package (see README "Enabling Google Keep for Workspace Admins").
bool resolve_service_account_key(ServiceAccountKey& key) {
    if (const char* e = std::getenv("GOOGLE_APPLICATION_CREDENTIALS")) {
        if (load_service_account_key(e, key)) return true;
    }
    if (load_service_account_key("keep_service_account.json", key)) return true;
    if (load_service_account_key("../keep_service_account.json", key)) return true;
    return false;
}

void split_url(const std::string& url, std::string& host, std::string& path) {
    size_t host_start = url.find("://");
    host_start = (host_start == std::string::npos) ? 0 : host_start + 3;
    size_t path_start = url.find('/', host_start);
    if (path_start == std::string::npos) {
        host = url;
        path = "/";
    } else {
        host = url.substr(0, path_start);
        path = url.substr(path_start);
    }
}

} // namespace

std::string get_access_token(const std::string& user_email, std::string& error) {
    long long now = now_unix();

    auto cached = g_token_cache.find(user_email);
    if (cached != g_token_cache.end() && now < cached->second.expires_at - 60) {
        return cached->second.access_token;
    }

    ServiceAccountKey key;
    if (!resolve_service_account_key(key)) {
        error = "No Keep service account key configured (GOOGLE_APPLICATION_CREDENTIALS or keep_service_account.json)";
        return "";
    }

    json header;
    header["alg"] = "RS256";
    header["typ"] = "JWT";

    json claims;
    claims["iss"] = key.client_email;
    claims["sub"] = user_email;
    claims["scope"] = "https://www.googleapis.com/auth/keep";
    claims["aud"] = key.token_uri;
    claims["iat"] = now;
    claims["exp"] = now + 3600;

    std::string signing_input = base64url_encode(header.dump()) + "." + base64url_encode(claims.dump());

    std::string signature;
    if (!rsa_sign_sha256(key.private_key, signing_input, signature)) {
        error = "Failed to RS256-sign JWT (invalid service account private key)";
        return "";
    }
    std::string jwt = signing_input + "." + base64url_encode(signature);

    std::string host, path;
    split_url(key.token_uri, host, path);

    httplib::Client cli(host.c_str());
    httplib::Params params{
        {"grant_type", "urn:ietf:params:oauth:grant-type:jwt-bearer"},
        {"assertion", jwt}
    };
    auto res = cli.Post(path.c_str(), params);
    if (!res || res->status != 200) {
        error = "Keep token exchange failed: " + (res ? res->body : std::string("no response"));
        return "";
    }

    json j = json::parse(res->body);
    std::string access_token = j.value("access_token", "");
    long long expires_in = j.value("expires_in", 3600);
    if (access_token.empty()) {
        error = "Keep token exchange returned no access_token";
        return "";
    }

    g_token_cache[user_email] = CachedToken{access_token, now + expires_in};
    return access_token;
}

} // namespace KeepAuth
