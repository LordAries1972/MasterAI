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
        } else if (item.first == "llamaFinetuneExecutable" ||
                   item.first == "MASTERAI_LLAMA_FINETUNE") {
            config.llama_finetune_executable = item.second;
        } else if (item.first == "llamaExportLoraExecutable" ||
                   item.first == "MASTERAI_LLAMA_EXPORT_LORA") {
            config.llama_export_lora_executable = item.second;
        } else if (item.first == "parquetHelperExecutable" ||
                   item.first == "MASTERAI_PARQUET_HELPER") {
            config.parquet_helper_executable = item.second;
        } else if (item.first == "acceleratorPolicy" ||
                   item.first == "MASTERAI_ACCELERATOR_POLICY") {
            config.accelerator_policy = item.second;
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

// Phase 33 (LOCAL-ONLY slice): renders AppConfig::local_runner_pool the same
// hand-written way every other configuration section in this file is
// serialized (no generic JSON-object writer exists here -- see quote()/
// array() above), so a saved settings file round-trips through load()
// unchanged when nothing else changed.
std::string local_runner_pool_array(const std::vector<LocalRunnerConfig>& pool) {
    std::string result{"["};
    bool first = true;
    for (const auto& entry : pool) {
        if (!first) result += ",";
        first = false;
        result += "{\"id\":" + quote(entry.id) +
                  ",\"port\":" + std::to_string(entry.port) +
                  ",\"acceleratorPolicy\":" + quote(entry.accelerator_policy) +
                  ",\"capabilities\":" + array(entry.capabilities) +
                  ",\"authorizedProjectIds\":" +
                  array(entry.authorized_project_ids) +
                  ",\"priority\":" + std::to_string(entry.priority) + "}";
    }
    return result + "]";
}

// Phase 33 (INTRANET-WORKER slice): renders AppConfig::intranet_worker_pool
// the same hand-written way local_runner_pool_array() above does.
std::string intranet_worker_pool_array(const std::vector<IntranetWorkerConfig>& pool) {
    std::string result{"["};
    bool first = true;
    for (const auto& entry : pool) {
        if (!first) result += ",";
        first = false;
        result += "{\"id\":" + quote(entry.id) +
                  ",\"host\":" + quote(entry.host) +
                  ",\"port\":" + std::to_string(entry.port) +
                  ",\"expectedServerCertificateSha256\":" +
                  quote(entry.expected_server_certificate_sha256) +
                  ",\"capabilities\":" + array(entry.capabilities) +
                  ",\"authorizedProjectIds\":" +
                  array(entry.authorized_project_ids) +
                  ",\"priority\":" + std::to_string(entry.priority) + "}";
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
                            "knowledge", "memory", "hardware", "indexing",
                            "retrieval", "cache", "session", "performance",
                            "storage", "machineLearning",
                            // Phase 33 (LOCAL-ONLY slice): optional, absent
                            // in every pre-Phase-33 settings file.
                            "localRunnerPool",
                            // Phase 33 (INTRANET-WORKER slice): optional,
                            // absent in every pre-this-pass settings file.
                            "intranetWorkerPool", "privateCa", "workerMode"},
                     "");
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
        // A relative runtimeRoot/modelsRoot resolves against the directory
        // that holds this settings file, not the process's current working
        // directory -- otherwise all of a server's saved state (chats,
        // projects, downloaded models) lands wherever the process happened
        // to be launched from (e.g. a shell's home directory on C:\) instead
        // of consistently inside the workspace this settings file belongs
        // to, regardless of how or from where masterai is started.
        std::error_code anchor_error;
        auto settings_directory =
            std::filesystem::absolute(settings, anchor_error).parent_path();
        if (anchor_error) settings_directory = std::filesystem::current_path();
        const auto resolve_workspace_path =
            [&settings_directory](const std::string& raw) {
                const std::filesystem::path candidate(raw);
                return candidate.is_absolute()
                           ? candidate
                           : (settings_directory / candidate).lexically_normal();
            };
        config.runtime_root =
            resolve_workspace_path(workspace.required("runtimeRoot").as_string());
        config.models_root =
            resolve_workspace_path(workspace.required("modelsRoot").as_string());

        // Phase: "PageFile" -- a MasterAI-scoped substitute for spilling to
        // the system's own pagefile/temp drive. Left empty (the default),
        // resolve_page_file_root() falls back to runtime_root/"cache", the
        // same disk-cache location MasterAI has always used, so an unset
        // pageFileRoot changes nothing about existing installs. Set, it
        // redirects that same disk-backed, per-category, key-indexed cache
        // (CacheManager -- already the quick-lookup, MasterAI-only store for
        // tokenization/prompt/model-manifest data) to live at the
        // administrator's chosen location instead, with no admin privilege
        // required since it is an ordinary directory, not a real Windows
        // pagefile.
        if (const auto* storage = root.optional("storage")) {
            require_only(*storage,
                         {"pageFileRoot", "scratchRoot", "scratchGlobalQuotaMiB",
                          "scratchFreeSpaceReserveMiB"},
                         "storage.");
            const auto raw = storage->required("pageFileRoot").as_string();
            if (!raw.empty()) config.page_file_root = resolve_workspace_path(raw);
            // Phase 31: scratch fields are all optional so a pre-Phase-31
            // settings file (with only pageFileRoot) keeps loading
            // unchanged, defaulting to AppConfig's own safe values.
            if (storage->optional("scratchRoot") != nullptr) {
                const auto scratch_raw = storage->required("scratchRoot").as_string();
                if (!scratch_raw.empty()) {
                    config.scratch_root = resolve_workspace_path(scratch_raw);
                }
            }
            if (storage->optional("scratchGlobalQuotaMiB") != nullptr) {
                config.scratch_global_quota_mib =
                    positive(*storage, "scratchGlobalQuotaMiB", 1048576ULL);
            }
            if (storage->optional("scratchFreeSpaceReserveMiB") != nullptr) {
                config.scratch_free_space_reserve_mib =
                    positive(*storage, "scratchFreeSpaceReserveMiB", 1048576ULL);
            }
        }

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

        if (const auto* hardware = root.optional("hardware")) {
            require_only(*hardware, {"acceleratorPolicy"}, "hardware.");
            config.accelerator_policy =
                hardware->required("acceleratorPolicy").as_string();
        }

        if (const auto* inference = root.optional("inference")) {
            require_only(*inference,
                         {"llamaServerExecutable", "runnerPort",
                          "chatMaxReplyTokens", "chatContextLength",
                          "startupTimeoutSeconds", "stallTimeoutSeconds",
                          "llamaFinetuneExecutable",
                          "llamaExportLoraExecutable"},
                         "inference.");
            config.llama_server_executable =
                inference->required("llamaServerExecutable").as_string();
            // Phase 73: optional -- empty disables real LLM LoRA
            // fine-tuning (see LlmFineTuneOptions in masterai.hpp).
            if (inference->optional("llamaFinetuneExecutable") != nullptr) {
                config.llama_finetune_executable =
                    inference->required("llamaFinetuneExecutable").as_string();
            }
            if (inference->optional("llamaExportLoraExecutable") != nullptr) {
                config.llama_export_lora_executable =
                    inference->required("llamaExportLoraExecutable")
                        .as_string();
            }
            config.runner_port = static_cast<std::uint16_t>(
                positive(*inference, "runnerPort", 65535U));
            if (inference->optional("chatMaxReplyTokens") != nullptr) {
                config.chat_max_reply_tokens = static_cast<std::uint32_t>(
                    positive(*inference, "chatMaxReplyTokens", 32768U));
            }
            if (inference->optional("chatContextLength") != nullptr) {
                config.chat_context_length = static_cast<std::uint32_t>(
                    positive(*inference, "chatContextLength", 1048576U));
            }
            if (inference->optional("startupTimeoutSeconds") != nullptr) {
                config.runner_startup_timeout_seconds =
                    static_cast<std::uint32_t>(
                        positive(*inference, "startupTimeoutSeconds", 3600U));
            }
            if (inference->optional("stallTimeoutSeconds") != nullptr) {
                config.runner_stall_timeout_seconds =
                    static_cast<std::uint32_t>(
                        positive(*inference, "stallTimeoutSeconds", 3600U));
            }
        }

        // Phase 33 (LOCAL-ONLY slice): opt-in local multi-runner pool. Absent
        // entirely (the common case), config.local_runner_pool stays empty
        // and single-runner mode is exactly what every pre-Phase-33 settings
        // file already produces -- this array is purely additive.
        if (const auto* pool = root.optional("localRunnerPool")) {
            for (const auto& item : pool->as_array()) {
                require_only(item,
                             {"id", "port", "acceleratorPolicy",
                              "capabilities", "authorizedProjectIds",
                              "priority"},
                             "localRunnerPool[].");
                LocalRunnerConfig entry;
                entry.id = item.required("id").as_string();
                entry.port = static_cast<std::uint16_t>(
                    positive(item, "port", 65535U));
                if (item.optional("acceleratorPolicy") != nullptr) {
                    entry.accelerator_policy =
                        item.required("acceleratorPolicy").as_string();
                }
                if (item.optional("capabilities") != nullptr) {
                    entry.capabilities = string_set(item, "capabilities");
                }
                if (item.optional("authorizedProjectIds") != nullptr) {
                    entry.authorized_project_ids =
                        string_set(item, "authorizedProjectIds");
                }
                if (item.optional("priority") != nullptr) {
                    entry.priority = static_cast<unsigned int>(
                        non_negative(item, "priority", 1000U));
                }
                config.local_runner_pool.push_back(std::move(entry));
            }
        }

        // Phase 33 (INTRANET-WORKER slice): the private CA this control
        // plane's own worker PKI is rooted at. Absent entirely means no
        // private CA is configured -- intranetWorkerPool/workerMode both
        // require one and ConfigurationManager::validate() rejects a
        // non-empty one without it.
        if (const auto* private_ca = root.optional("privateCa")) {
            require_only(*private_ca, {"certificateFile", "privateKeyFile"},
                        "privateCa.");
            const auto certificate_raw =
                private_ca->required("certificateFile").as_string();
            const auto key_raw = private_ca->required("privateKeyFile").as_string();
            if (!certificate_raw.empty()) {
                config.private_ca.certificate_file =
                    resolve_workspace_path(certificate_raw);
            }
            if (!key_raw.empty()) {
                config.private_ca.private_key_file =
                    resolve_workspace_path(key_raw);
            }
        }

        // Phase 33 (INTRANET-WORKER slice): opt-in remote worker routing.
        // Absent entirely (the common case), config.intranet_worker_pool
        // stays empty and no remote worker is ever consulted -- purely
        // additive, exactly like localRunnerPool above.
        if (const auto* worker_pool_section = root.optional("intranetWorkerPool")) {
            require_only(*worker_pool_section,
                         {"clientCertificateFile", "clientPrivateKeyFile",
                          "workers"},
                         "intranetWorkerPool.");
            const auto client_certificate_raw =
                worker_pool_section->required("clientCertificateFile").as_string();
            const auto client_key_raw =
                worker_pool_section->required("clientPrivateKeyFile").as_string();
            if (!client_certificate_raw.empty()) {
                config.intranet_worker_client_certificate_file =
                    resolve_workspace_path(client_certificate_raw);
            }
            if (!client_key_raw.empty()) {
                config.intranet_worker_client_private_key_file =
                    resolve_workspace_path(client_key_raw);
            }
            for (const auto& item : worker_pool_section->required("workers").as_array()) {
                require_only(item,
                             {"id", "host", "port",
                              "expectedServerCertificateSha256",
                              "capabilities", "authorizedProjectIds",
                              "priority"},
                             "intranetWorkerPool.workers[].");
                IntranetWorkerConfig entry;
                entry.id = item.required("id").as_string();
                entry.host = item.required("host").as_string();
                entry.port = static_cast<std::uint16_t>(
                    positive(item, "port", 65535U));
                entry.expected_server_certificate_sha256 =
                    item.required("expectedServerCertificateSha256").as_string();
                if (item.optional("capabilities") != nullptr) {
                    entry.capabilities = string_set(item, "capabilities");
                }
                if (item.optional("authorizedProjectIds") != nullptr) {
                    entry.authorized_project_ids =
                        string_set(item, "authorizedProjectIds");
                }
                if (item.optional("priority") != nullptr) {
                    entry.priority = static_cast<unsigned int>(
                        non_negative(item, "priority", 1000U));
                }
                config.intranet_worker_pool.push_back(std::move(entry));
            }
        }

        // Phase 33 (INTRANET-WORKER slice): opt-in worker-mode listener.
        // Absent entirely (the common case), config.worker_mode.enabled
        // stays false and this machine remains loopback-only, exactly like
        // every pre-this-pass deployment.
        if (const auto* worker_mode = root.optional("workerMode")) {
            require_only(*worker_mode,
                         {"enabled", "bindHost", "port", "caCertificateFile",
                          "serverCertificateFile", "serverPrivateKeyFile",
                          "approvedClientCertificateSha256"},
                         "workerMode.");
            config.worker_mode.enabled = worker_mode->required("enabled").as_boolean();
            config.worker_mode.bind_host = worker_mode->required("bindHost").as_string();
            config.worker_mode.port = static_cast<std::uint16_t>(
                positive(*worker_mode, "port", 65535U));
            const auto ca_certificate_raw =
                worker_mode->required("caCertificateFile").as_string();
            const auto server_certificate_raw =
                worker_mode->required("serverCertificateFile").as_string();
            const auto server_key_raw =
                worker_mode->required("serverPrivateKeyFile").as_string();
            if (!ca_certificate_raw.empty()) {
                config.worker_mode.ca_certificate_file =
                    resolve_workspace_path(ca_certificate_raw);
            }
            if (!server_certificate_raw.empty()) {
                config.worker_mode.server_certificate_file =
                    resolve_workspace_path(server_certificate_raw);
            }
            if (!server_key_raw.empty()) {
                config.worker_mode.server_private_key_file =
                    resolve_workspace_path(server_key_raw);
            }
            config.worker_mode.approved_client_certificate_sha256 =
                string_set(*worker_mode, "approvedClientCertificateSha256");
        }

        if (const auto* session = root.optional("session")) {
            require_only(
                *session,
                {"enabled", "maxSlots", "idleRetentionSeconds"}, "session.");
            config.session_reuse_enabled =
                session->required("enabled").as_boolean();
            config.session_reuse_max_slots = static_cast<unsigned int>(
                positive(*session, "maxSlots", 64U));
            config.session_reuse_idle_retention_seconds =
                static_cast<std::uint32_t>(
                    positive(*session, "idleRetentionSeconds", 86400U));
        }

        if (const auto* performance = root.optional("performance")) {
            require_only(*performance, {"autoTune"}, "performance.");
            config.performance_auto_tune =
                performance->required("autoTune").as_boolean();
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

        if (const auto* knowledge = root.optional("knowledge")) {
            require_only(*knowledge,
                         {"parquetHelperExecutable", "maximumDocumentBytes"},
                         "knowledge.");
            if (knowledge->optional("parquetHelperExecutable") != nullptr) {
                config.parquet_helper_executable =
                    knowledge->required("parquetHelperExecutable").as_string();
            }
            if (knowledge->optional("maximumDocumentBytes") != nullptr) {
                config.knowledge_maximum_document_bytes =
                    positive(*knowledge, "maximumDocumentBytes", 256ULL * 1024ULL * 1024ULL);
            }
        }

        // Machine Learning Settings (docs/PLAN.md "Machine Learning
        // Abilities" section 49): mirrors the "knowledge" section above.
        if (const auto* machine_learning = root.optional("machineLearning")) {
            require_only(*machine_learning, {"tabularDatasetMaximumCsvBytes"},
                         "machineLearning.");
            if (machine_learning->optional("tabularDatasetMaximumCsvBytes") != nullptr) {
                config.tabular_dataset_maximum_csv_bytes =
                    positive(*machine_learning, "tabularDatasetMaximumCsvBytes",
                            64ULL * 1024ULL * 1024ULL);
            }
        }

        if (const auto* cache = root.optional("cache")) {
            require_only(*cache, {"enabled", "maximumBytesPerCategory"},
                         "cache.");
            config.cache_enabled = cache->required("enabled").as_boolean();
            config.cache_maximum_bytes_per_category = positive(
                *cache, "maximumBytesPerCategory", 4ULL * 1024ULL * 1024ULL * 1024ULL);
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
    if (config.accelerator_policy != "auto" &&
        config.accelerator_policy != "cpu_only" &&
        config.accelerator_policy != "gpu_allowed") {
        throw std::runtime_error("accelerator policy is outside policy");
    }
    for (const auto& executable :
         {config.llama_server_executable, config.curl_executable,
          config.parquet_helper_executable, config.llama_finetune_executable,
          config.llama_export_lora_executable}) {
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
    // Phase 33 (LOCAL-ONLY slice): every configured pool runner needs a
    // unique, non-privileged, non-HTTP, non-default-runner port, and the
    // pool as a whole requires llama_server_executable the same way the
    // single default runner does -- there is no separate "pool backend"
    // executable, only more instances of the same approved backend.
    if (!config.local_runner_pool.empty() &&
        config.llama_server_executable.empty()) {
        throw std::runtime_error(
            "local runner pool requires llamaServerExecutable to be set");
    }
    {
        std::set<std::string> seen_ids;
        std::set<std::uint16_t> seen_ports{config.runner_port};
        for (const auto& entry : config.local_runner_pool) {
            if (entry.id.empty() || !seen_ids.insert(entry.id).second) {
                throw std::runtime_error(
                    "local runner pool ids must be unique and non-empty");
            }
            if (entry.port < 1024U || entry.port == config.port ||
                !seen_ports.insert(entry.port).second) {
                throw std::runtime_error(
                    "local runner pool port is outside policy or duplicated");
            }
        }
    }
    // Phase 33 (INTRANET-WORKER slice): the same "declared, then hard
    // rejected before the feature can ever run" discipline as the
    // local_runner_pool checks above -- any configuration that would leave
    // this feature only partially wired is rejected here, before
    // HttpServer ever constructs an IntranetWorkerPool/WorkerListener from
    // it. Neither check runs on the common case (both empty/disabled).
    const bool wants_intranet_worker_features =
        !config.intranet_worker_pool.empty() || config.worker_mode.enabled;
    if (wants_intranet_worker_features) {
        if (!openssl_available()) {
            throw std::runtime_error(
                "intranet worker mTLS is configured but this build was "
                "compiled without OpenSSL -- see docs/PLAN.md Phase 33");
        }
        if (config.private_ca.certificate_file.empty() ||
            config.private_ca.private_key_file.empty()) {
            throw std::runtime_error(
                "intranet worker features require privateCa to be configured");
        }
    }
    if (!config.intranet_worker_pool.empty()) {
        if (config.intranet_worker_client_certificate_file.empty() ||
            config.intranet_worker_client_private_key_file.empty()) {
            throw std::runtime_error(
                "intranetWorkerPool requires clientCertificateFile and "
                "clientPrivateKeyFile to be configured");
        }
        std::set<std::string> seen_worker_ids;
        for (const auto& worker : config.intranet_worker_pool) {
            if (worker.id.empty() || !seen_worker_ids.insert(worker.id).second) {
                throw std::runtime_error(
                    "intranet worker pool ids must be unique and non-empty");
            }
            if (worker.host.empty() || worker.port == 0U) {
                throw std::runtime_error(
                    "intranet worker pool entries require a host and port");
            }
            if (worker.expected_server_certificate_sha256.empty()) {
                throw std::runtime_error(
                    "intranet worker pool entries require an expected "
                    "server certificate digest");
            }
        }
    }
    if (config.worker_mode.enabled) {
        if (config.worker_mode.bind_host.empty() ||
            config.worker_mode.bind_host == "127.0.0.1" ||
            config.worker_mode.bind_host == "localhost") {
            throw std::runtime_error(
                "workerMode.bindHost must be an explicit non-loopback "
                "address -- loopback deployments never need worker mode");
        }
        if (config.worker_mode.port == 0U ||
            config.worker_mode.port == config.port ||
            config.worker_mode.port == config.runner_port) {
            throw std::runtime_error(
                "workerMode.port must be set and distinct from the "
                "administrator HTTP port and runner IPC port");
        }
        if (config.worker_mode.ca_certificate_file.empty() ||
            config.worker_mode.server_certificate_file.empty() ||
            config.worker_mode.server_private_key_file.empty()) {
            throw std::runtime_error(
                "workerMode requires caCertificateFile, "
                "serverCertificateFile, and serverPrivateKeyFile");
        }
        if (config.worker_mode.approved_client_certificate_sha256.empty()) {
            throw std::runtime_error(
                "workerMode requires at least one approved client "
                "certificate digest");
        }
        if (config.llama_server_executable.empty()) {
            throw std::runtime_error(
                "workerMode requires llamaServerExecutable to be set -- a "
                "worker still supervises its own local backend process");
        }
    }
    if (!config.page_file_root.empty() &&
        std::filesystem::exists(config.page_file_root) &&
        !std::filesystem::is_directory(config.page_file_root)) {
        throw std::runtime_error(
            "configured page file location is not a directory");
    }
    if (!config.scratch_root.empty() &&
        std::filesystem::exists(config.scratch_root) &&
        !std::filesystem::is_directory(config.scratch_root)) {
        throw std::runtime_error(
            "configured scratch location is not a directory");
    }
    if (config.scratch_global_quota_mib == 0U ||
        config.scratch_free_space_reserve_mib == 0U) {
        throw std::runtime_error("scratch quota/reserve configuration is invalid");
    }
}

std::filesystem::path resolve_page_file_root(const AppConfig& configuration) {
    return configuration.page_file_root.empty()
               ? configuration.runtime_root / "cache"
               : configuration.page_file_root;
}

std::filesystem::path resolve_scratch_root(const AppConfig& configuration) {
    return configuration.scratch_root.empty()
               ? configuration.runtime_root / "scratch"
               : configuration.scratch_root;
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
        "  \"storage\":{\"pageFileRoot\":" +
        quote(c.page_file_root.string()) +
        ",\"scratchRoot\":" + quote(c.scratch_root.string()) +
        ",\"scratchGlobalQuotaMiB\":" +
        std::to_string(c.scratch_global_quota_mib) +
        ",\"scratchFreeSpaceReserveMiB\":" +
        std::to_string(c.scratch_free_space_reserve_mib) + "},\n"
        "  \"models\":{\"memoryReserveMiB\":" +
        std::to_string(c.memory_reserve_mib) + "},\n"
        "  \"memory\":{\"hardLimitMiB\":" +
        std::to_string(c.memory_hard_limit_mib) +
        ",\"minimumFreePercent\":" +
        std::to_string(c.minimum_free_ram_percent) +
        ",\"criticalPressurePercent\":" +
        std::to_string(c.critical_memory_percent) +
        ",\"profile\":" + quote(c.resource_profile) + "},\n"
        "  \"hardware\":{\"acceleratorPolicy\":" +
        quote(c.accelerator_policy) + "},\n"
        "  \"inference\":{\"llamaServerExecutable\":" +
        quote(c.llama_server_executable.string()) +
        ",\"runnerPort\":" + std::to_string(c.runner_port) +
        ",\"chatMaxReplyTokens\":" + std::to_string(c.chat_max_reply_tokens) +
        ",\"chatContextLength\":" + std::to_string(c.chat_context_length) +
        ",\"startupTimeoutSeconds\":" +
        std::to_string(c.runner_startup_timeout_seconds) +
        ",\"stallTimeoutSeconds\":" +
        std::to_string(c.runner_stall_timeout_seconds) +
        ",\"llamaFinetuneExecutable\":" +
        quote(c.llama_finetune_executable.string()) +
        ",\"llamaExportLoraExecutable\":" +
        quote(c.llama_export_lora_executable.string()) +
        "},\n"
        "  \"localRunnerPool\":" + local_runner_pool_array(c.local_runner_pool) +
        ",\n"
        "  \"privateCa\":{\"certificateFile\":" +
        quote(c.private_ca.certificate_file.string()) +
        ",\"privateKeyFile\":" + quote(c.private_ca.private_key_file.string()) +
        "},\n"
        "  \"intranetWorkerPool\":{\"clientCertificateFile\":" +
        quote(c.intranet_worker_client_certificate_file.string()) +
        ",\"clientPrivateKeyFile\":" +
        quote(c.intranet_worker_client_private_key_file.string()) +
        ",\"workers\":" + intranet_worker_pool_array(c.intranet_worker_pool) +
        "},\n"
        "  \"workerMode\":{\"enabled\":" +
        (c.worker_mode.enabled ? "true" : "false") +
        ",\"bindHost\":" + quote(c.worker_mode.bind_host) +
        ",\"port\":" + std::to_string(c.worker_mode.port) +
        ",\"caCertificateFile\":" +
        quote(c.worker_mode.ca_certificate_file.string()) +
        ",\"serverCertificateFile\":" +
        quote(c.worker_mode.server_certificate_file.string()) +
        ",\"serverPrivateKeyFile\":" +
        quote(c.worker_mode.server_private_key_file.string()) +
        ",\"approvedClientCertificateSha256\":" +
        array(c.worker_mode.approved_client_certificate_sha256) + "},\n"
        "  \"downloads\":{\"curlExecutable\":" +
        quote(c.curl_executable.string()) + "},\n"
        "  \"knowledge\":{\"parquetHelperExecutable\":" +
        quote(c.parquet_helper_executable.string()) +
        ",\"maximumDocumentBytes\":" +
        std::to_string(c.knowledge_maximum_document_bytes) + "},\n"
        "  \"machineLearning\":{\"tabularDatasetMaximumCsvBytes\":" +
        std::to_string(c.tabular_dataset_maximum_csv_bytes) + "},\n"
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
        std::to_string(c.retrieval_maximum_total_chunks) + "},\n"
        "  \"cache\":{\"enabled\":" +
        (c.cache_enabled ? "true" : "false") +
        ",\"maximumBytesPerCategory\":" +
        std::to_string(c.cache_maximum_bytes_per_category) + "},\n"
        "  \"session\":{\"enabled\":" +
        (c.session_reuse_enabled ? "true" : "false") +
        ",\"maxSlots\":" + std::to_string(c.session_reuse_max_slots) +
        ",\"idleRetentionSeconds\":" +
        std::to_string(c.session_reuse_idle_retention_seconds) + "},\n"
        "  \"performance\":{\"autoTune\":" +
        (c.performance_auto_tune ? "true" : "false") + "}\n}\n";
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
