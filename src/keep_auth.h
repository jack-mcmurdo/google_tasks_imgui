#pragma once

#include <string>

namespace KeepAuth {

// Mints a short-lived access token (scope https://www.googleapis.com/auth/keep)
// impersonating `user_email` via a service-account JWT-bearer exchange. This is
// a separate auth mechanism from Auth::TokenManager's interactive OAuth flow —
// Keep access requires a Workspace admin to have granted the service account
// domain-wide delegation for the Keep scope.
//
// Returns the access token, or "" with `error` set if no service-account key
// is configured (personal/non-delegated accounts) or the exchange fails.
// Tokens are cached in-memory per user_email until near expiry.
std::string get_access_token(const std::string& user_email, std::string& error);

} // namespace KeepAuth
