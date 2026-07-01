#pragma once

#include <string>
#include <vector>
#include <functional>

namespace Auth {

struct Account {
    std::string id; // Google account ID
    std::string email;
    std::string access_token;
    std::string refresh_token;
    long long expires_at; // UNIX timestamp
};

class TokenManager {
public:
    TokenManager(const std::string& filepath);
    void load();
    void save();
    
    void add_account(const Account& acc);
    void remove_account(const std::string& id);
    Account* get_account(const std::string& id);
    std::vector<Account>& get_accounts();
    
private:
    std::string filepath_;
    std::vector<Account> accounts_;
};

// Starts the OAuth loop for a new account.
// Spawns a background thread to handle the callback and token exchange.
void start_oauth_flow(std::function<void(bool success, std::string error_msg, Account acc)> callback);

// Refresh token helper (blocking)
bool refresh_token_sync(Account& acc);

// Global settings
void set_client_credentials(const std::string& client_id, const std::string& client_secret);

} // namespace Auth
