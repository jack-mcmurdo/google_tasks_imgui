#include "auth.h"
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <algorithm>

using json = nlohmann::json;

static std::string url_encode(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.length());
    for (char c : value) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped += c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
            escaped += buf;
        }
    }
    return escaped;
}

static std::string base64url_encode(const unsigned char* data, size_t len) {
    static const char* alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (size_t i = 0; i < len; i++) {
        val = (val << 8) + data[i];
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
    return out; // unpadded, matches base64url-without-padding required by PKCE
}

// PKCE (RFC 7636): proves to Google that whoever redeems the authorization
// code is the same process that started the flow, without needing a
// client_secret. code_verifier must be 43-128 chars from the unreserved
// base64url set; 32 random bytes -> 43 base64url chars satisfies that.
static std::string generate_code_verifier() {
    unsigned char buf[32];
    RAND_bytes(buf, sizeof(buf));
    return base64url_encode(buf, sizeof(buf));
}

static std::string code_challenge_s256(const std::string& verifier) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    EVP_Digest(verifier.data(), verifier.size(), digest, &digest_len, EVP_sha256(), nullptr);
    return base64url_encode(digest, digest_len);
}

namespace Auth {

static std::string g_client_id = "";
static std::string g_client_secret = "";
static const std::string REDIRECT_URI = "http://127.0.0.1:8080/";
static const std::string AUTH_URL = "https://accounts.google.com/o/oauth2/v2/auth";
static const std::string TOKEN_URL = "https://oauth2.googleapis.com/token";

void set_client_credentials(const std::string& client_id, const std::string& client_secret) {
    g_client_id = client_id;
    g_client_secret = client_secret;
}

TokenManager::TokenManager(const std::string& filepath) : filepath_(filepath) {}

void TokenManager::load() {
    std::ifstream f(filepath_);
    if (!f.is_open()) return;
    try {
        json j = json::parse(f);
        accounts_.clear();
        for (const auto& item : j) {
            Account acc;
            acc.id = item.value("id", "");
            acc.email = item.value("email", "");
            acc.access_token = item.value("access_token", "");
            acc.refresh_token = item.value("refresh_token", "");
            acc.expires_at = item.value("expires_at", 0LL);
            accounts_.push_back(acc);
        }
    } catch (...) {
        // Handle parsing error
    }
}

void TokenManager::save() {
    json j = json::array();
    for (const auto& acc : accounts_) {
        json item;
        item["id"] = acc.id;
        item["email"] = acc.email;
        item["access_token"] = acc.access_token;
        item["refresh_token"] = acc.refresh_token;
        item["expires_at"] = acc.expires_at;
        j.push_back(item);
    }
    std::ofstream f(filepath_);
    if (f.is_open()) {
        f << j.dump(4);
    }
}

void TokenManager::add_account(const Account& acc) {
    for (auto& existing : accounts_) {
        if (existing.id == acc.id) {
            existing = acc;
            save();
            return;
        }
    }
    accounts_.push_back(acc);
    save();
}

void TokenManager::remove_account(const std::string& id) {
    accounts_.erase(std::remove_if(accounts_.begin(), accounts_.end(),
        [&id](const Account& acc) { return acc.id == id; }), accounts_.end());
    save();
}

Account* TokenManager::get_account(const std::string& id) {
    for (auto& acc : accounts_) {
        if (acc.id == id) return &acc;
    }
    return nullptr;
}

std::vector<Account>& TokenManager::get_accounts() {
    return accounts_;
}

void open_browser(const std::string& url) {
    std::string cmd;
#if defined(_WIN32)
    cmd = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
    cmd = "open \"" + url + "\"";
#else
    cmd = "xdg-open \"" + url + "\" || python3 -m webbrowser \"" + url + "\"";
#endif
    int ret = system(cmd.c_str());
    (void)ret; // Ignore return value
}

static Account fetch_user_info(const std::string& access_token) {
    httplib::Client cli("https://www.googleapis.com");
    cli.set_bearer_token_auth(access_token);
    auto res = cli.Get("/oauth2/v2/userinfo");
    Account acc;
    if (res && res->status == 200) {
        auto j = json::parse(res->body);
        if (j.contains("id")) acc.id = j["id"];
        if (j.contains("email")) acc.email = j["email"];
    }
    return acc;
}

void start_oauth_flow(std::function<void(bool success, std::string error_msg, Account acc)> callback) {
    std::thread([callback]() {
        if (g_client_id.empty() || g_client_secret.empty()) {
            callback(false, "OAuth client credentials not set", Account{});
            return;
        }

        std::string code_verifier = generate_code_verifier();
        std::string code_challenge = code_challenge_s256(code_verifier);

        std::string auth_request_url = AUTH_URL + "?client_id=" + g_client_id +
            "&redirect_uri=" + url_encode(REDIRECT_URI) +
            "&response_type=code" +
            "&scope=" + url_encode("https://www.googleapis.com/auth/tasks https://www.googleapis.com/auth/userinfo.email https://www.googleapis.com/auth/userinfo.profile") +
            "&access_type=offline&prompt=consent" +
            "&code_challenge=" + code_challenge +
            "&code_challenge_method=S256";

        open_browser(auth_request_url);

        httplib::Server svr;
        std::string auth_code;
        std::string auth_error;

        svr.Get("/", [&svr, &auth_code, &auth_error](const httplib::Request& req, httplib::Response& res) {
            if (req.has_param("code")) {
                auth_code = req.get_param_value("code");
                res.set_content("<html><body><h1>Login successful!</h1><p>You can close this tab.</p></body></html>", "text/html");
            } else if (req.has_param("error")) {
                auth_error = req.get_param_value("error");
                res.set_content("<html><body><h1>Login failed</h1><p>Error: " + auth_error + "</p></body></html>", "text/html");
            } else {
                res.set_content("Invalid request", "text/plain");
            }
            svr.stop();
        });

        // Run the server on port 8080 (blocking until svr.stop() is called)
        if (!svr.listen("127.0.0.1", 8080)) {
            callback(false, "Failed to start local server on port 8080. Is the port in use?", Account{});
            return;
        }

        if (!auth_code.empty()) {
            // Exchange code for token
            httplib::Client cli("https://oauth2.googleapis.com");
            httplib::Params params{
                {"code", auth_code},
                {"client_id", g_client_id},
                {"client_secret", g_client_secret},
                {"code_verifier", code_verifier},
                {"redirect_uri", REDIRECT_URI},
                {"grant_type", "authorization_code"}
            };
            auto res = cli.Post("/token", params);
            if (res && res->status == 200) {
                auto j = json::parse(res->body);
                std::string access_token = j.value("access_token", "");
                std::string refresh_token = j.value("refresh_token", "");
                long long expires_in = j.value("expires_in", 3600);
                long long expires_at = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count() + expires_in;

                Account acc = fetch_user_info(access_token);
                if (!acc.id.empty()) {
                    acc.access_token = access_token;
                    acc.refresh_token = refresh_token;
                    acc.expires_at = expires_at;
                    callback(true, "", acc);
                } else {
                    callback(false, "Failed to fetch user info", Account{});
                }
            } else {
                callback(false, "Token exchange failed: " + (res ? res->body : "No response"), Account{});
            }
        } else {
            callback(false, "OAuth error: " + auth_error, Account{});
        }
    }).detach();
}

bool refresh_token_sync(Account& acc) {
    if (g_client_id.empty() || g_client_secret.empty() || acc.refresh_token.empty()) return false;
    
    httplib::Client cli("https://oauth2.googleapis.com");
    httplib::Params params{
        {"client_id", g_client_id},
        {"client_secret", g_client_secret},
        {"refresh_token", acc.refresh_token},
        {"grant_type", "refresh_token"}
    };
    auto res = cli.Post("/token", params);
    if (res && res->status == 200) {
        auto j = json::parse(res->body);
        acc.access_token = j.value("access_token", "");
        long long expires_in = j.value("expires_in", 3600);
        acc.expires_at = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() + expires_in;
        return true;
    }
    return false;
}

} // namespace Auth
