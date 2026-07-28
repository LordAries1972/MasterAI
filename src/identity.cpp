#include "masterai.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__) && defined(MASTERAI_HAS_PAM)
#include <security/pam_appl.h>
#endif

namespace masterai {
namespace {

void secure_erase(std::string& value) noexcept {
    if (!value.empty()) {
#if defined(_WIN32)
        SecureZeroMemory(&value[0], value.size());
#else
        volatile char* bytes = &value[0];
        for (std::size_t index = 0; index < value.size(); ++index) {
            bytes[index] = '\0';
        }
#endif
    }
    value.clear();
}

bool valid_username(const std::string& username) {
    if (username.empty() || username.size() > 256U) {
        return false;
    }
    return std::all_of(username.begin(), username.end(), [](const char character) {
        const unsigned char value = static_cast<unsigned char>(character);
        return value >= 0x20U && value != 0x7fU && character != '\r' &&
               character != '\n' && character != '\0';
    });
}

#if defined(_WIN32)
bool utf8_to_wide(const std::string& input, std::vector<wchar_t>& output) {
    if (input.empty() ||
        input.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    const int required =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                            static_cast<int>(input.size()), nullptr, 0);
    if (required <= 0) {
        return false;
    }
    output.assign(static_cast<std::size_t>(required) + 1U, L'\0');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                               static_cast<int>(input.size()), output.data(),
                               required) == required;
}

void erase_wide(std::vector<wchar_t>& value) noexcept {
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
    }
    value.clear();
}
#elif defined(__linux__) && defined(MASTERAI_HAS_PAM)
struct PamCredential {
    const char* password;
};

int pam_conversation(const int message_count, const pam_message** messages,
                     pam_response** responses, void* context) {
    if (message_count <= 0 || messages == nullptr || responses == nullptr ||
        context == nullptr) {
        return PAM_CONV_ERR;
    }
    auto* credential = static_cast<PamCredential*>(context);
    auto* reply = static_cast<pam_response*>(
        std::calloc(static_cast<std::size_t>(message_count), sizeof(pam_response)));
    if (reply == nullptr) {
        return PAM_BUF_ERR;
    }
    for (int index = 0; index < message_count; ++index) {
        if (messages[index] == nullptr) {
            std::free(reply);
            return PAM_CONV_ERR;
        }
        if (messages[index]->msg_style == PAM_PROMPT_ECHO_OFF) {
            reply[index].resp = ::strdup(credential->password);
            if (reply[index].resp == nullptr) {
                for (int cleanup = 0; cleanup < index; ++cleanup) {
                    std::free(reply[cleanup].resp);
                }
                std::free(reply);
                return PAM_BUF_ERR;
            }
        }
    }
    *responses = reply;
    return PAM_SUCCESS;
}
#endif

}  // namespace

bool OsIdentityProvider::available() const noexcept {
#if defined(_WIN32) || (defined(__linux__) && defined(MASTERAI_HAS_PAM))
    return true;
#else
    return false;
#endif
}

AuthenticationResult OsIdentityProvider::authenticate(
    const std::string& username, std::string& password) const noexcept {
    AuthenticationResult result;
    if (!valid_username(username) || password.empty() || password.size() > 4096U) {
        secure_erase(password);
        result.status = AuthenticationStatus::invalid_input;
        result.diagnostic = "Credential input is outside policy.";
        return result;
    }

#if defined(_WIN32)
    std::vector<wchar_t> wide_username;
    std::vector<wchar_t> wide_password;
    if (!utf8_to_wide(username, wide_username) ||
        !utf8_to_wide(password, wide_password)) {
        erase_wide(wide_password);
        secure_erase(password);
        result.status = AuthenticationStatus::invalid_input;
        result.diagnostic = "Credential text is not valid UTF-8.";
        return result;
    }

    HANDLE token = nullptr;
    wchar_t* account = wide_username.data();
    const wchar_t* domain = nullptr;
    const auto separator =
        std::find(wide_username.begin(), wide_username.end(), L'\\');
    if (separator != wide_username.end()) {
        *separator = L'\0';
        domain = wide_username.data();
        account = &*(separator + 1);
        if (*domain == L'\0' || *account == L'\0') {
            erase_wide(wide_password);
            secure_erase(password);
            result.status = AuthenticationStatus::invalid_input;
            result.diagnostic = "Windows account name is invalid.";
            return result;
        }
    } else if (std::find(wide_username.begin(), wide_username.end(), L'@') ==
               wide_username.end()) {
        domain = L".";
    }
    SetLastError(ERROR_SUCCESS);
    const BOOL accepted =
        LogonUserW(account, domain, wide_password.data(),
                   LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT, &token);
    const DWORD error = accepted != 0 ? ERROR_SUCCESS : GetLastError();
    erase_wide(wide_password);
    secure_erase(password);
    if (token != nullptr) {
        CloseHandle(token);
    }
    if (accepted != 0) {
        result.status = AuthenticationStatus::authenticated;
        result.principal = username;
        result.diagnostic = "Identity verified by Windows.";
    } else if (error == ERROR_LOGON_FAILURE || error == ERROR_ACCOUNT_RESTRICTION ||
               error == ERROR_ACCOUNT_DISABLED || error == ERROR_INVALID_LOGON_HOURS ||
               error == ERROR_PASSWORD_EXPIRED || error == ERROR_ACCOUNT_EXPIRED) {
        result.status = AuthenticationStatus::denied;
        result.diagnostic = "The operating system denied the credentials.";
    } else {
        result.status = AuthenticationStatus::system_error;
        result.diagnostic = "Windows identity verification was unavailable.";
    }
#elif defined(__linux__) && defined(MASTERAI_HAS_PAM)
    PamCredential credential{password.c_str()};
    pam_conv conversation{pam_conversation, &credential};
    pam_handle_t* handle = nullptr;
    const int started =
        pam_start("masterai", username.c_str(), &conversation, &handle);
    int status = started;
    if (started == PAM_SUCCESS) {
        status = pam_authenticate(handle, PAM_SILENT | PAM_DISALLOW_NULL_AUTHTOK);
        if (status == PAM_SUCCESS) {
            status = pam_acct_mgmt(handle, PAM_SILENT);
        }
    }
    if (handle != nullptr) {
        pam_end(handle, status);
    }
    secure_erase(password);
    if (status == PAM_SUCCESS) {
        result.status = AuthenticationStatus::authenticated;
        result.principal = username;
        result.diagnostic = "Identity verified by PAM.";
    } else if (status == PAM_AUTH_ERR || status == PAM_USER_UNKNOWN ||
               status == PAM_MAXTRIES || status == PAM_ACCT_EXPIRED ||
               status == PAM_NEW_AUTHTOK_REQD) {
        result.status = AuthenticationStatus::denied;
        result.diagnostic = "The operating system denied the credentials.";
    } else {
        result.status = AuthenticationStatus::system_error;
        result.diagnostic = "PAM identity verification was unavailable.";
    }
#else
    secure_erase(password);
    result.status = AuthenticationStatus::unavailable;
    result.diagnostic =
        "This build has no native operating-system identity provider.";
#endif
    return result;
}

}  // namespace masterai
