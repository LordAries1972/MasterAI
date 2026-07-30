#include "masterai.hpp"
#include "json.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace masterai {
namespace {

void require_only(const JsonValue& value,
                  const std::set<std::string>& allowed,
                  const std::string& location) {
    for (const auto& field : value.as_object()) {
        if (allowed.find(field.first) == allowed.end()) {
            throw std::runtime_error("unknown configuration field: " +
                                     location + field.first);
        }
    }
}

std::string read_file(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    if (size == 0U || size > 1024U * 1024U) {
        throw std::runtime_error("configuration file size is invalid");
    }
    std::ifstream input(path, std::ios::binary);
    std::ostringstream output;
    output << input.rdbuf();
    if (!input || (!input.good() && !input.eof())) {
        throw std::runtime_error("configuration file could not be read");
    }
    return output.str();
}

std::set<std::string> string_set(const JsonValue& value,
                                 const std::string& field) {
    std::set<std::string> result;
    for (const auto& item : value.required(field).as_array()) {
        const auto& text = item.as_string();
        if (text.empty() || text.size() > 512U || !result.insert(text).second) {
            throw std::runtime_error(field + " contains an invalid or duplicate value");
        }
    }
    return result;
}

std::uint64_t positive(const JsonValue& value, const std::string& field,
                       const std::uint64_t maximum) {
    const auto number = value.required(field).as_integer();
    if (number <= 0 || static_cast<std::uint64_t>(number) > maximum) {
        throw std::runtime_error(field + " is outside policy");
    }
    return static_cast<std::uint64_t>(number);
}

std::uint64_t non_negative(const JsonValue& value, const std::string& field,
                           const std::uint64_t maximum) {
    const auto number = value.required(field).as_integer();
    if (number < 0 || static_cast<std::uint64_t>(number) > maximum) {
        throw std::runtime_error(field + " is outside policy");
    }
    return static_cast<std::uint64_t>(number);
}

bool parse_bool_text(const std::string& value) {
    if (value == "1" || value == "true") {
        return true;
    }
    if (value == "0" || value == "false") {
        return false;
    }
    throw std::runtime_error("boolean override must be true, false, 1, or 0");
}

void apply_values(AppConfig& config,
                  const std::map<std::string, std::string>& values) {
    for (const auto& item : values) {
        if (item.first == "host" || item.first == "MASTERAI_HOST") {
            config.host = item.second;
        } else if (item.first == "port" || item.first == "MASTERAI_PORT") {
            const auto value = std::stoul(item.second);
            if (value > 65535U) throw std::runtime_error("port override is invalid");
            config.port = static_cast<std::uint16_t>(value);
        } else if (item.first == "allowIntranet" ||
                   item.first == "MASTERAI_ALLOW_INTRANET") {
            config.allow_intranet = parse_bool_text(item.second);
        } else if (item.first == "runtimeRoot" ||
                   item.first == "MASTERAI_RUNTIME_ROOT") {
            config.runtime_root = item.second;
        } else if (item.first == "modelsRoot" ||
                   item.first == "MASTERAI_MODELS_ROOT") {
            config.models_root = item.second;
        } else if (item.first == "llamaServerExecutable" ||
                   item.first == "MASTERAI_LLAMA_SERVER") {
            config.llama_server_executable = item.second;
        } else if (item.first == "curlExecutable" ||
                   item.first == "MASTERAI_CURL") {
            config.curl_executable = item.second;
        } else {
            throw std::runtime_error("unsupported configuration override: " +
                                     item.first);
        }
    }
}

std::string quote(const std::string& text) {
    std::string result{"\""};
    for (const unsigned char character : text) {
        switch (character) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (character < 0x20U) {
                    throw std::runtime_error("configuration contains control text");
                }
                result.push_back(static_cast<char>(character));
        }
    }
    return result + "\"";
}

std::string array(const std::set<std::string>& values) {
    std::string result{"["};
    bool first = true;
    for (const auto& value : values) {
        if (!first) result += ",";
        first = false;
        result += quote(value);
    }
    return result + "]";
}

bool loopback(const std::string& host) {
    return host == "127.0.0.1" || host == "::1" || host == "localhost";
}

}  // namespace

AppConfig ConfigurationManager::safe_defaults() { return AppConfig{}; }

AppConfig ConfigurationManager::load(
    const std::filesystem::path& settings,
    const std::map<std::string, std::string>& environment,
    const std::map<std::string, std::string>& overrides) {
    AppConfig config = safe_defaults();
    if (std::filesystem::exists(settings)) {
        const auto root = parse_json(read_file(settings));
        require_only(root, {"schemaVersion", "server", "tls", "auth",
                            "workspace", "models", "inference", "downloads",
                            "memory", "indexing", "retrieval"}, "");
        config.schema_version =
            static_cast<int>(root.required("schemaVersion").as_integer());

        const auto& server = root.required("server");
        require_only(server, {"host", "port", "allowIntranet",
                              "maxRequestBytes", "requestTimeoutSeconds",
                              "maxConcurrentConnections", "rateLimitPerMinute",
                              "allowedHosts", "allowedOrigins"}, "server.");
        config.host = server.required("host").as_string();
        config.port = static_cast<std::uint16_t>(
            positive(server, "port", 65535U));
        config.allow_intranet = server.required("allowIntranet").as_boolean();
        config.max_request_bytes =
            positive(server, "maxRequestBytes", 64U * 1024U * 1024U);
        config.request_timeout_seconds = static_cast<std::uint32_t>(
            positive(server, "requestTimeoutSeconds", 300U));
        config.max_concurrent_connections = static_cast<std::uint32_t>(
            positive(server, "maxConcurrentConnections", 1024U));
        config.rate_limit_per_minute = static_cast<std::uint32_t>(
            positive(server, "rateLimitPerMinute", 100000U));
        config.allowed_hosts = string_set(server, "allowedHosts");
        config.allowed_origins = string_set(server, "allowedOrigins");

        const auto& tls = root.required("tls");
        require_only(tls, {"mode", "certificateFile", "privateKeyFile"}, "tls.");
        config.tls_mode = tls.required("mode").as_string();
        config.tls_certificate_file = tls.required("certificateFile").as_string();
        config.tls_private_key_file = tls.required("privateKeyFile").as_string();

        const auto& auth = root.required("auth");
        require_only(auth, {"enabled", "sessionMinutes",
                            "allowLocalPasswordAccounts",
                            "allowOsIdentityAccounts"}, "auth.");
        config.authentication_enabled = auth.required("enabled").as_boolean();
        config.session_minutes = positive(auth, "sessionMinutes", 10080U);
        if (const auto* local_accounts =
                auth.optional("allowLocalPasswordAccounts")) {
            config.allow_local_password_accounts = local_accounts->as_boolean();
        }
        if (const auto* os_accounts =
                auth.optional("allowOsIdentityAccounts")) {
            config.allow_os_identity_accounts = os_accounts->as_boolean();
        }

        const auto& workspace = root.required("workspace");
        require_only(workspace, {"runtimeRoot", "modelsRoot"}, "workspace.");
        config.runtime_root = workspace.required("runtimeRoot").as_string();
        config.models_root = workspace.required("modelsRoot").as_string();

        const auto& models = root.required("models");
        require_only(models, {"memoryReserveMiB"}, "models.");
        config.memory_reserve_mib =
            positive(models, "memoryReserveMiB", 1048576U);

        if (const auto* memory = root.optional("memory")) {
            require_only(*memory, {"hardLimitMiB", "minimumFreePercent",
                                   "criticalPressurePercent", "profile"},
                         "memory.");
            config.memory_hard_limit_mib =
                non_negative(*memory, "hardLimitMiB", 1048576U);
            config.minimum_free_ram_percent = static_cast<unsigned int>(
                positive(*memory, "minimumFreePercent", 50U));
            config.critical_memory_percent = static_cast<unsigned int>(
                positive(*memory, "criticalPressurePercent", 99U));
            config.resource_profile = memory->required("profile").as_string();
        }

        if (const auto* inference = root.optional("inference")) {
            require_only(*inference, {"llamaServerExecutable", "runnerPort"},
                         "inference.");
            config.llama_server_executable =
                inference->required("llamaServerExecutable").as_string();
            config.runner_port = static_cast<std::uint16_t>(
                positive(*inference, "runnerPort", 65535U));
        }

        if (const auto* downloads = root.optional("downloads")) {
            require_only(*downloads, {"curlExecutable"}, "downloads.");
            config.curl_executable =
                downloads->required("curlExecutable").as_string();
        }

        if (const auto* indexing = root.optional("indexing")) {
            require_only(*indexing, {"watchProjectFiles"}, "indexing.");
            config.watch_project_files =
                indexing->required("watchProjectFiles").as_boolean();
        }

        if (const auto* retrieval = root.optional("retrieval")) {
            require_only(*retrieval,
                         {"enabled", "deadlineMilliseconds",
                          "maximumContextBytes", "maximumChunksPerSource",
                          "maximumTotalChunks"},
                         "retrieval.");
            config.retrieval_enabled =
                retrieval->required("enabled").as_boolean();
            config.retrieval_deadline_milliseconds = static_cast<std::uint32_t>(
                positive(*retrieval, "deadlineMilliseconds", 30000U));
            config.retrieval_maximum_context_bytes =
                positive(*retrieval, "maximumContextBytes", 4U * 1024U * 1024U);
            config.retrieval_maximum_chunks_per_source =
                static_cast<std::uint32_t>(
                    positive(*retrieval, "maximumChunksPerSource", 256U));
            config.retrieval_maximum_total_chunks = static_cast<std::uint32_t>(
                positive(*retrieval, "maximumTotalChunks", 1024U));
        }
    }
    apply_values(config, environment);
    apply_values(config, overrides);
    validate(config);
    return config;
}

void ConfigurationManager::validate(const AppConfig& config) {
    if (config.schema_version != 1 || config.host.empty() || config.port < 1024U ||
        config.runtime_root.empty() || config.models_root.empty() ||
        config.max_request_bytes < 1024U || config.allowed_hosts.empty()) {
        throw std::runtime_error("configuration violates the version or bounds policy");
    }
    const bool local = loopback(config.host);
    if (!local) {
        if (!config.allow_intranet || !config.authentication_enabled ||
            config.tls_mode != "required" ||
            config.tls_certificate_file.empty() ||
            config.tls_private_key_file.empty() ||
            config.allowed_origins.empty()) {
            throw std::runtime_error(
                "intranet binding requires explicit enablement, authentication, "
                "TLS certificate/key files, host and origin allow-lists");
        }
        if (!std::filesystem::is_regular_file(config.tls_certificate_file) ||
            !std::filesystem::is_regular_file(config.tls_private_key_file)) {
            throw std::runtime_error("configured TLS certificate material is unavailable");
        }
    } else if (config.tls_mode != "disabled-loopback-only" &&
               config.tls_mode != "required") {
        throw std::runtime_error("TLS mode is invalid");
    }
    if (!config.authentication_enabled) {
        throw std::runtime_error("authentication cannot be disabled");
    }
    if (!config.allow_local_password_accounts &&
        !config.allow_os_identity_accounts) {
        throw std::runtime_error(
            "at least one of local password accounts or OS identity accounts "
            "must remain enabled");
    }
    if (config.minimum_free_ram_percent > 50U ||
        config.critical_memory_percent < 80U ||
        config.critical_memory_percent > 99U ||
        (config.resource_profile != "minimal" &&
         config.resource_profile != "balanced" &&
         config.resource_profile != "performance")) {
        throw std::runtime_error("memory profile is outside policy");
    }
    for (const auto& executable :
         {config.llama_server_executable, config.curl_executable}) {
        if (!executable.empty() &&
            (!std::filesystem::is_regular_file(executable) ||
             std::filesystem::is_symlink(executable))) {
            throw std::runtime_error(
                "configured external executable is not an approved regular file");
        }
    }
    if (config.runner_port < 1024U || config.runner_port == config.port) {
        throw std::runtime_error("runner IPC port is outside policy");
    }
}

std::string ConfigurationManager::serialize(const AppConfig& c) {
    return "{\n"
        "  \"schemaVersion\":1,\n"
        "  \"server\":{\"host\":" + quote(c.host) +
        ",\"port\":" + std::to_string(c.port) +
        ",\"allowIntranet\":" + (c.allow_intranet ? "true" : "false") +
        ",\"maxRequestBytes\":" + std::to_string(c.max_request_bytes) +
        ",\"requestTimeoutSeconds\":" + std::to_string(c.request_timeout_seconds) +
        ",\"maxConcurrentConnections\":" +
        std::to_string(c.max_concurrent_connections) +
        ",\"rateLimitPerMinute\":" + std::to_string(c.rate_limit_per_minute) +
        ",\"allowedHosts\":" + array(c.allowed_hosts) +
        ",\"allowedOrigins\":" + array(c.allowed_origins) + "},\n"
        "  \"tls\":{\"mode\":" + quote(c.tls_mode) +
        ",\"certificateFile\":" + quote(c.tls_certificate_file.string()) +
        ",\"privateKeyFile\":" + quote(c.tls_private_key_file.string()) + "},\n"
        "  \"auth\":{\"enabled\":" +
        std::string(c.authentication_enabled ? "true" : "false") +
        ",\"sessionMinutes\":" + std::to_string(c.session_minutes) +
        ",\"allowLocalPasswordAccounts\":" +
        (c.allow_local_password_accounts ? "true" : "false") +
        ",\"allowOsIdentityAccounts\":" +
        (c.allow_os_identity_accounts ? "true" : "false") + "},\n"
        "  \"workspace\":{\"runtimeRoot\":" + quote(c.runtime_root.string()) +
        ",\"modelsRoot\":" + quote(c.models_root.string()) + "},\n"
        "  \"models\":{\"memoryReserveMiB\":" +
        std::to_string(c.memory_reserve_mib) + "},\n"
        "  \"memory\":{\"hardLimitMiB\":" +
        std::to_string(c.memory_hard_limit_mib) +
        ",\"minimumFreePercent\":" +
        std::to_string(c.minimum_free_ram_percent) +
        ",\"criticalPressurePercent\":" +
        std::to_string(c.critical_memory_percent) +
        ",\"profile\":" + quote(c.resource_profile) + "},\n"
        "  \"inference\":{\"llamaServerExecutable\":" +
        quote(c.llama_server_executable.string()) +
        ",\"runnerPort\":" + std::to_string(c.runner_port) + "},\n"
        "  \"downloads\":{\"curlExecutable\":" +
        quote(c.curl_executable.string()) + "},\n"
        "  \"indexing\":{\"watchProjectFiles\":" +
        (c.watch_project_files ? "true" : "false") + "},\n"
        "  \"retrieval\":{\"enabled\":" +
        (c.retrieval_enabled ? "true" : "false") +
        ",\"deadlineMilliseconds\":" +
        std::to_string(c.retrieval_deadline_milliseconds) +
        ",\"maximumContextBytes\":" +
        std::to_string(c.retrieval_maximum_context_bytes) +
        ",\"maximumChunksPerSource\":" +
        std::to_string(c.retrieval_maximum_chunks_per_source) +
        ",\"maximumTotalChunks\":" +
        std::to_string(c.retrieval_maximum_total_chunks) + "}\n}\n";
}

void ConfigurationManager::save_atomic(const AppConfig& config,
                                       const std::filesystem::path& settings) {
    validate(config);
    const auto parent = settings.parent_path().empty()
                            ? std::filesystem::current_path()
                            : settings.parent_path();
    std::filesystem::create_directories(parent);
    const auto temporary = settings.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        const auto content = serialize(config);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output) throw std::runtime_error("temporary configuration write failed");
    }
    static_cast<void>(load(temporary));
    if (std::filesystem::exists(settings)) {
        std::filesystem::copy_file(
            settings, settings.string() + ".bak",
            std::filesystem::copy_options::overwrite_existing);
    }
#if defined(_WIN32)
    if (MoveFileExW(std::filesystem::path(temporary).c_str(), settings.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("atomic configuration replacement failed");
    }
#else
    std::filesystem::rename(temporary, settings);
#endif
}

}  // namespace masterai
