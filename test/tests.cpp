// MasterAI native core validation suite.
//
// This unit exercises security, persistence, workflow, transport, recovery,
// and performance contracts without placing test code in production sources.
// Each phase-level test drives public interfaces and checks fail-closed paths.
#include "json.hpp"
#include "masterai.hpp"
#include "mcp_http_fixture.hpp"
#include "server_internal.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#elif defined(__linux__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

using masterai_test::McpHttpFixture;
using masterai_test::fake_curl_executable;
using masterai_test::fake_llama_executable;
using masterai_test::require;
using masterai_test::TemporaryDirectory;
using masterai_test::test_executable;
using masterai_test::write_text;

void test_json() {
    const auto root = masterai::parse_json(
        "{\"schemaVersion\":1,\"name\":\"masterai\",\"enabled\":true}");
    require(root.required("schemaVersion").as_integer() == 1,
            "JSON integer parsing failed");
    require(root.required("name").as_string() == "masterai",
            "JSON string parsing failed");
    require(masterai::json_string(
                std::string{"quote\" slash\\\b\f\n\r\t"} +
                std::string(1U, '\x01')) ==
                "\"quote\\\" slash\\\\\\b\\f\\n\\r\\t\\u0001\"",
            "canonical JSON string escaping changed");
    bool duplicate_rejected = false;
    try {
        static_cast<void>(masterai::parse_json("{\"a\":1,\"a\":2}"));
    } catch (const std::exception&) {
        duplicate_rejected = true;
    }
    require(duplicate_rejected, "duplicate JSON fields were not rejected");
}

void test_sessions() {
    masterai::SessionStore sessions;
    const std::string token = sessions.create("administrator", 1000U, 60U);
    require(token.size() == 64U, "session token entropy encoding is wrong");
    require(sessions.validate(token, 1059U).has_value(),
            "valid session was rejected");
    require(!sessions.validate(token, 1060U).has_value(),
            "expired session was accepted");
    const std::string revoked = sessions.create("developer", 2000U, 60U);
    sessions.revoke(revoked);
    require(!sessions.validate(revoked, 2001U).has_value(),
            "revoked session was accepted");
}

void test_configuration_and_intranet_policy() {
    TemporaryDirectory temporary;
    const auto settings = temporary.path() / "settings.json";
    auto configuration = masterai::ConfigurationManager::safe_defaults();
    configuration.runtime_root = temporary.path() / "runtime";
    configuration.models_root = temporary.path() / "models";
    configuration.runner_startup_timeout_seconds = 245U;
    masterai::ConfigurationManager::save_atomic(configuration, settings);
    const auto loaded = masterai::ConfigurationManager::load(
        settings, {{"MASTERAI_PORT", "7171"}}, {{"port", "7272"}});
    require(loaded.port == 7272U,
            "configuration precedence did not apply CLI after environment");
    require(loaded.runner_startup_timeout_seconds == 245U,
            "runner startup timeout did not round-trip through save/load");

    write_text(settings, "{\"unexpected\":true}");
    bool unknown_rejected = false;
    try {
        static_cast<void>(masterai::ConfigurationManager::load(settings));
    } catch (const std::exception&) {
        unknown_rejected = true;
    }
    require(unknown_rejected, "unknown configuration fields were accepted");

    configuration.host = "192.168.1.20";
    configuration.allow_intranet = true;
    bool unsafe_intranet_rejected = false;
    try {
        masterai::ConfigurationManager::validate(configuration);
    } catch (const std::exception&) {
        unsafe_intranet_rejected = true;
    }
    require(unsafe_intranet_rejected,
            "intranet binding without TLS and origins was accepted");
}

void test_record_recovery_users_and_persistent_sessions() {
    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    records.put("settings", "active", "value");
    masterai::UserStore users(records);
    const auto administrator =
        users.create_first_administrator("TEST\\operator", "Operator");
    require(!users.setup_required() &&
            users.find_by_principal("TEST\\operator")->id == administrator.id,
            "first administrator mapping was not persisted");

    masterai::SessionStore sessions(records);
    const auto token = sessions.create(administrator.id, 100U, 100U);
    masterai::SessionStore reloaded_sessions(records);
    const auto reloaded = reloaded_sessions.validate(token, 150U);
    require(reloaded.has_value() && reloaded->csrf_secret.size() == 64U,
            "persistent session or CSRF binding was not restored");
    const auto replacement = reloaded_sessions.rotate(token, 151U, 100U);
    require(!reloaded_sessions.validate(token, 152U).has_value() &&
            reloaded_sessions.validate(replacement, 152U).has_value(),
            "session rotation did not revoke the predecessor");
    require(masterai::role_allows(masterai::UserRole::developer, "models.read") &&
            !masterai::role_allows(masterai::UserRole::viewer, "models.load"),
            "role permission boundaries are incorrect");
    masterai::ApiTokenStore api_tokens(records);
    const auto api_token = api_tokens.create(
        administrator.id, {"identity.read", "models.read"}, 100U, 100U,
        {"project-one"});
    const auto validated_api_token =
        api_tokens.validate(api_token, "models.read", 150U);
    require(validated_api_token.has_value() &&
            validated_api_token->project_ids.count("project-one") == 1U &&
            !api_tokens.validate(api_token, "models.load", 150U).has_value(),
            "scoped API token or project binding authorization failed");
    masterai::ApiTokenStore restored_api_tokens(records);
    require(restored_api_tokens.validate(api_token, "models.read", 150U)
                    ->project_ids.count("project-one") == 1U,
            "API token project binding was not restored");
    api_tokens.revoke(api_token);
    require(!api_tokens.validate(api_token, "models.read", 151U).has_value(),
            "revoked API token remained valid");

    records.checkpoint();
    write_text(temporary.path() / "database" / "records.journal",
               "truncated-journal-entry\n");
    masterai::RecordStore recovered(temporary.path() / "database");
    recovered.open();
    require(recovered.get("settings", "active").has_value(),
            "journal recovery discarded the last valid checkpoint state");
    recovered.checkpoint();
    const auto backup = recovered.backup(temporary.path() / "backups");
    require(std::filesystem::is_directory(backup),
            "record-store backup was not created");
}

void test_os_secret_store() {
#if defined(_WIN32)
    TemporaryDirectory temporary;
    masterai::SecretStore secrets(temporary.path() / "secrets");
    require(secrets.available(), "Windows DPAPI secret store is unavailable");
    secrets.set("mcp-token", "scoped-revocable-token");
    require(secrets.get("mcp-token") ==
                std::optional<std::string>("scoped-revocable-token"),
            "DPAPI secret round trip failed");
    secrets.erase("mcp-token");
    require(!secrets.get("mcp-token").has_value(),
            "DPAPI secret revocation failed");
#endif
}

void test_os_identity_boundary() {
    const masterai::OsIdentityProvider identity;
#if defined(_WIN32)
    require(identity.available(), "Windows OS identity provider is unavailable");
#endif
    std::string password = "must-be-erased";
    const auto result = identity.authenticate("", password);
    require(result.status == masterai::AuthenticationStatus::invalid_input,
            "invalid identity input was not rejected");
    require(password.empty(), "transient password was not erased");
}

void test_suitability() {
    masterai::ModelManifest manifest;
    manifest.format = "gguf";
    manifest.backend = "llama-cpp";
    manifest.minimum_ram_mib = 4096U;
    manifest.recommended_ram_mib = 8192U;
    masterai::HardwareInfo hardware;
    hardware.available_ram_mib = 16384U;
    const auto result = masterai::assess_model(manifest, hardware, 2048U);
    require(result.rating == masterai::Suitability::recommended,
            "suitable model was not recommended");
}

void test_unverified_model_cannot_launch() {
    masterai::ModelRecord model;
    model.state = masterai::ModelState::unverified;
    model.directory = std::filesystem::current_path();
    model.manifest.model_file = "unverified.gguf";

    const auto backend = test_executable();
    require(std::filesystem::is_regular_file(backend),
            "test backend fixture is unavailable");
    masterai::LlamaCppAdapter adapter(backend);
    bool rejected = false;
    try {
        static_cast<void>(adapter.build_launch_spec(model, 4096U, 8080U));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "unverified model was eligible for launch");
}

void test_verified_model_promotion_and_load_recheck() {
    TemporaryDirectory temporary;
    const auto model_root = temporary.path() / "models";
    const auto directory =
        model_root / "general-programming" / "verified-coder";
    const std::string contents = "GGUF-test-model";
    write_text(directory / "model.gguf", contents);
    std::clog << "  hashing fixture\n";
    const auto digest = masterai::sha256_file_hex(directory / "model.gguf");
    const std::string manifest =
        "{\"schemaVersion\":1,\"id\":\"verified-coder\","
        "\"displayName\":\"Verified Coder\","
        "\"category\":\"general-programming\","
        "\"model\":{\"format\":\"gguf\",\"architecture\":\"test\","
        "\"quantization\":\"q4\"},"
        "\"requirements\":{\"minimumRamMiB\":1,\"recommendedRamMiB\":1,"
        "\"estimatedDiskMiB\":1},"
        "\"files\":[{\"path\":\"model.gguf\",\"sizeBytes\":" +
        std::to_string(contents.size()) + ",\"sha256\":\"" + digest + "\"}],"
        "\"backends\":[\"llama-cpp\"],"
        "\"provenance\":{\"sourceUrl\":\"https://github.com/example/model\","
        "\"revision\":\"0123456789abcdef\"},"
        "\"license\":{\"spdx\":\"MIT\",\"accepted\":true},"
        "\"hardware\":{\"cpuFeatures\":[],\"gpuBackend\":\"\"}}";
    write_text(directory / "manifest.json", manifest);

    std::clog << "  verifying registry\n";
    masterai::HardwareInfo hardware;
    hardware.available_ram_mib = 8192U;
    const masterai::ModelRegistry registry(model_root, hardware, 1024U);
    std::size_t verified_count = 0U;
    registry.verify([&](const std::string&, const bool ok, const std::string&) {
        if (ok) ++verified_count;
    });
    require(verified_count == 1U, "verify() did not confirm the fixture model");

    std::clog << "  scanning registry\n";
    const auto models = registry.scan();
    require(models.size() == 1U &&
            models.front().state == masterai::ModelState::ready,
            "verified suitable model was not promoted to Ready");

    std::clog << "  checking launch\n";
    const auto backend = test_executable();
    masterai::LlamaCppAdapter adapter(backend);
    static_cast<void>(adapter.build_launch_spec(models.front(), 4096U, 8080U));
    std::clog << "  checking tamper block\n";
    write_text(directory / "model.gguf", "GGUF-tamper-data");
    bool tamper_rejected = false;
    try {
        static_cast<void>(
            adapter.build_launch_spec(models.front(), 4096U, 8080U));
    } catch (const std::exception&) {
        tamper_rejected = true;
    }
    require(tamper_rejected,
            "model modified after verification was accepted at load time");
}

void test_path_containment() {
    const auto root = std::filesystem::current_path();
    require(masterai::is_path_within(root, root / "models"),
            "contained path was rejected");
    require(!masterai::is_path_within(root, root.parent_path()),
            "parent path escape was accepted");
}

void test_phase_four_runner_supervisor() {
    TemporaryDirectory temporary;
    const auto model_directory = temporary.path() / "model";
    std::filesystem::create_directories(model_directory);
    const auto model_file = model_directory / "model.gguf";
    write_text(model_file, "GGUF-supervisor-fixture");
    masterai::ModelRecord model;
    model.directory = model_directory;
    model.manifest.id = "supervisor-fixture";
    model.manifest.model_file = "model.gguf";
    model.manifest.model_size_bytes = std::filesystem::file_size(model_file);
    model.manifest.model_sha256 = masterai::sha256_file_hex(model_file);
    model.state = masterai::ModelState::ready;
    const auto backend = fake_llama_executable();
    require(std::filesystem::is_regular_file(backend),
            "fake llama runner fixture is unavailable");
    masterai::RunnerSupervisor supervisor(backend, temporary.path() / "runtime");
    supervisor.load(model, 4096U, 18081U, 5U);
    require(supervisor.metrics().state == masterai::RunnerState::ready,
            "supervised runner did not become ready");
    require(supervisor.tokenize("one two three") == 3U,
            "runner tokenization IPC failed");
    std::atomic_bool cancellation{false};
    std::string streamed;
    const auto generated = supervisor.generate(
        "return value", {}, [&](const std::string& chunk) { streamed += chunk; },
        cancellation);
    require(generated.text == "return value;" && streamed == generated.text &&
                generated.generated_tokens == 2U,
            "runner generation stream was not assembled correctly");
    require(supervisor.metrics().requests_completed == 1U,
            "runner completion metrics were not recorded");
    supervisor.unload(0U);
    require(supervisor.metrics().state == masterai::RunnerState::unloaded,
            "runner did not unload");
}

void test_phase_five_chat_and_projects() {
    TemporaryDirectory temporary;
    const auto workspace = temporary.path() / "projects";
    std::filesystem::create_directories(workspace / "masterai");
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::ProjectCatalog projects(workspace, records);
    const auto project =
        projects.add("masterai", "MasterAI", workspace / "masterai");
    require(projects.find(project.id).has_value(), "project was not retained");

    masterai::ChatStore chats(records);
    const auto chat = chats.create("operator", project.id, "coder-model");
    chats.append(chat.id, masterai::ChatRole::user, "Review this function.");
    chats.append(chat.id, masterai::ChatRole::assistant,
                "Line one.\nLine two.\n\tIndented.");
    const auto visible = chats.find_for_owner(chat.id, "operator");
    require(visible.has_value() && visible->messages.size() == 2U,
            "owned chat history was not retained");
    require(visible->messages[1].role == masterai::ChatRole::assistant &&
                visible->messages[1].content ==
                    "Line one.\nLine two.\n\tIndented.",
            "assistant message was not retained verbatim");
    require(!chats.find_for_owner(chat.id, "another-user").has_value(),
            "chat ownership boundary was bypassed");
    require(!chats.set_model(chat.id, "another-user", "other-model"),
            "set_model bypassed the chat ownership boundary");
    require(chats.set_model(chat.id, "operator", "other-model"),
            "set_model failed for the chat's own owner");
    require(chats.find_for_owner(chat.id, "operator")->model_id ==
                "other-model",
            "set_model did not persist the new model in memory");

    masterai::AttachmentStore attachments(temporary.path() / "attachments",
                                          records);
    const auto attachment = attachments.add_text(
        "operator", project.id, "review.cpp", "int value = 1;\n");
    require(attachments.find_for_owner(attachment.id, "operator").has_value() &&
                !attachments.find_for_owner(attachment.id, "other").has_value(),
            "attachment ownership boundary was not enforced");

    masterai::ProjectCatalog restored_projects(workspace, records);
    masterai::ChatStore restored_chats(records);
    masterai::AttachmentStore restored_attachments(
        temporary.path() / "attachments", records);
    require(restored_projects.find(project.id).has_value() &&
                restored_chats.find_for_owner(chat.id, "operator").has_value() &&
                restored_attachments.find_for_owner(attachment.id, "operator")
                    .has_value(),
            "phase five records did not survive service reconstruction");
    const auto restored_chat = restored_chats.find_for_owner(chat.id, "operator");
    require(restored_chat->messages.size() == 2U &&
                restored_chat->messages[0].content == "Review this function." &&
                restored_chat->messages[1].content ==
                    "Line one.\nLine two.\n\tIndented." &&
                restored_chat->model_id == "other-model",
            "chat messages/model did not survive the split header/message "
            "persistence scheme across service reconstruction");
}

// Chat records written before the split header/message-per-key scheme
// packed the whole chat -- header fields followed by every message inline --
// into one "chats" value. ChatStore::restore() must still recognize and
// migrate that shape (both the original 4-field header and the later
// 6-field one) onto the current scheme without losing any message. This
// hand-builds a legacy-format record the same way the old ChatStore::persist
// once did, since nothing in the current code produces that shape anymore.
void test_phase_five_chat_legacy_migration() {
    const auto pack = [](const std::vector<std::string>& fields) {
        std::string result;
        for (const auto& field : fields) {
            result += std::to_string(field.size()) + ":" + field;
        }
        return result;
    };
    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    const std::string chat_id(32U, 'a');
    // Legacy 6-field header: owner, project, model, title, createdAt, count,
    // followed by 3 fields per message (role, content, createdAt).
    records.put("chats", chat_id,
               pack({"operator", "proj", "coder-model", "Old chat", "1000", "2",
                     "user", "First message.", "1000", "assistant",
                     "Second message.\nWith a newline.", "1001"}));

    masterai::ChatStore chats(records);
    const auto restored = chats.find_for_owner(chat_id, "operator");
    require(restored.has_value() && restored->messages.size() == 2U &&
                restored->messages[0].content == "First message." &&
                restored->messages[1].content ==
                    "Second message.\nWith a newline." &&
                restored->title == "Old chat",
            "legacy inline-message chat record was not migrated correctly");

    // The migration should have rewritten this chat onto the split scheme --
    // reopening the store must not need to re-migrate, and the messages must
    // still come back in order from "chat_messages".
    masterai::ChatStore reopened(records);
    const auto reopened_chat = reopened.find_for_owner(chat_id, "operator");
    require(reopened_chat.has_value() && reopened_chat->messages.size() == 2U &&
                reopened_chat->messages[0].content == "First message." &&
                reopened_chat->messages[1].content ==
                    "Second message.\nWith a newline.",
            "migrated chat record did not survive a second reload");
}

void test_phase_six_download_policy() {
    const std::string digest(64U, 'a');
    masterai::DownloadJob job(masterai::DownloadRequest{
        "https://huggingface.co/owner/model/resolve/0123456789/model.gguf",
        "0123456789", digest, "model.gguf.part", true});
    job.begin(100U);
    job.record_progress(200U);
    job.pause();
    job.begin(200U);
    job.begin_verification();
    job.complete(std::string(64U, 'b'));
    require(job.state() == masterai::DownloadState::quarantined,
            "digest mismatch was not quarantined");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    const auto transport = fake_llama_executable();
    const auto root = temporary.path() / "downloads";
    masterai::DownloadManager downloads(transport, root, records);
    const auto persisted = downloads.create(masterai::DownloadRequest{
        "https://github.com/owner/repository/releases/download/0123456789/model.gguf",
        "0123456789", digest, root / "artifacts" / "model.gguf", true});
    masterai::DownloadManager restored(transport, root, records);
    require(restored.find(persisted.id()).has_value(),
            "resumable download job state was not restored");
}

// Verifies the concurrency fix behind the "download blocks the progress bar"
// bug: DownloadManager::run() must (a) let find()/list() return promptly for
// other callers while a transfer is in flight, rather than blocking for the
// whole transfer, and (b) reject a second concurrent run() for the same job
// id instead of racing two transfers over the same .part file.
void test_phase_sixteen_concurrent_downloads() {
    const auto set_env = [](const char* name, const std::string& value) {
#if defined(_WIN32)
        _putenv_s(name, value.c_str());
#else
        setenv(name, value.c_str(), 1);
#endif
    };
    // Slow enough (~10 chunks * 30ms) that a concurrent find() and a second
    // run() attempt reliably land while the transfer is still in progress,
    // but short enough not to make the suite noticeably slower.
    set_env("MASTERAI_FAKE_CURL_TOTAL_BYTES", "200000");
    set_env("MASTERAI_FAKE_CURL_CHUNK_BYTES", "20000");
    set_env("MASTERAI_FAKE_CURL_CHUNK_DELAY_MS", "30");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    const auto transport = masterai_test::fake_curl_executable();
    const auto root = temporary.path() / "downloads";
    masterai::DownloadManager downloads(transport, root, records);
    // Must match what fake_curl actually writes (200000 'x' bytes) so the
    // transfer verifies as complete rather than being quarantined -- this
    // test is about concurrency correctness, not digest mismatch handling
    // (that path is already covered by test_phase_six_download_policy).
    const auto digest = masterai::sha256_hex(std::string(200000U, 'x'));
    const auto job = downloads.create(masterai::DownloadRequest{
        "https://huggingface.co/owner/model/resolve/0123456789/model.gguf",
        "0123456789", digest, root / "artifacts" / "model.gguf", true});

    std::atomic_bool cancellation_a{false};
    std::atomic_bool cancellation_b{false};
    std::atomic_bool already_running_seen{false};
    std::thread first([&]() { downloads.run(job.id(), cancellation_a); });

    // Give the first run() a moment to begin the transfer, then confirm a
    // second run() for the same id is rejected rather than starting a
    // second transfer, and that find() keeps returning promptly (never
    // blocked by the in-flight transfer) with advancing progress.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    try {
        downloads.run(job.id(), cancellation_b);
    } catch (const masterai::DownloadAlreadyRunning&) {
        already_running_seen = true;
    }
    require(already_running_seen.load(),
            "a concurrent run() for the same job id was not rejected");

    std::uint64_t previous_bytes = 0U;
    bool progress_advanced = false;
    for (int i = 0; i < 5; ++i) {
        const auto observed = downloads.find(job.id());
        require(observed.has_value(), "find() failed while a transfer was in flight");
        if (observed->completed_bytes() > previous_bytes) progress_advanced = true;
        previous_bytes = observed->completed_bytes();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    require(progress_advanced,
            "completedBytes never advanced while polling during an active transfer");

    first.join();
    const auto finished = downloads.find(job.id());
    require(finished.has_value() && finished->state() == masterai::DownloadState::complete,
            "concurrent access left the download job in an unexpected state");
    require(std::filesystem::file_size(root / "artifacts" / "model.gguf") == 200000U,
            "download destination did not contain the full transferred content");

    // The in-flight marker must be cleared after completion so retrying the
    // same id is refused for the real reason (destination already exists),
    // not a stale "already running" state.
    std::atomic_bool cancellation_retry{false};
    bool already_running_again = false;
    try {
        downloads.run(job.id(), cancellation_retry);
    } catch (const masterai::DownloadAlreadyRunning&) {
        already_running_again = true;
    } catch (const std::exception&) {
        // Expected: the destination already exists from the first run.
    }
    require(!already_running_again,
            "in-flight marker was not released after the transfer finished");
}

void test_phase_seven_benchmarks() {
    masterai::BenchmarkStore benchmarks;
    masterai::BenchmarkRecord record;
    record.model_id = "coder-model";
    record.backend_version = "llama-cpp-test";
    record.build_id = "masterai-test";
    record.hardware_id = "test-host";
    record.prompt_suite_hash = std::string(64U, 'c');
    record.profile = masterai::BenchmarkProfile::quick;
    record.prompt_tokens = 10U;
    record.generated_tokens = 20U;
    record.elapsed_microseconds = 1000U;
    record.passed_cases = 1U;
    benchmarks.add(record);
    require(benchmarks.comparable("test-host", record.prompt_suite_hash,
                                  masterai::BenchmarkProfile::quick).size() == 1U,
            "reproducible benchmark match failed");
    require(benchmarks.comparable("different-host", record.prompt_suite_hash,
                                  masterai::BenchmarkProfile::quick).empty(),
            "incompatible benchmark was compared");
    auto faster = record;
    faster.model_id = "faster-model";
    faster.elapsed_microseconds = 500U;
    benchmarks.add(faster);
    const auto recommended = benchmarks.recommend(
        "test-host", record.prompt_suite_hash, masterai::BenchmarkProfile::quick);
    require(recommended.has_value() &&
                recommended->model_id == "faster-model",
            "compatible benchmark recommendation did not prefer quality then speed");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::BenchmarkStore durable(records);
    durable.add(record);
    masterai::BenchmarkStore restored(records);
    require(restored.comparable("test-host", record.prompt_suite_hash,
                                masterai::BenchmarkProfile::quick)
                .size() == 1U,
            "benchmark result persistence failed");
}

// Verifies Phase 8 protocol negotiation, catalogue discovery, project-bound
// source access, denial behavior, resources, and newline-delimited stdio.
void test_phase_eight_mcp_inbound() {
    TemporaryDirectory temporary;
    const auto workspace = temporary.path() / "projects";
    const auto project_root = workspace / "demo";
    const auto models_root = temporary.path() / "models";
    std::filesystem::create_directories(project_root / "src");
    std::filesystem::create_directories(models_root);
    write_text(project_root / "src" / "answer.cpp",
               "// searchable marker\nint answer() { return 42; }\n");

    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::ProjectCatalog projects(workspace, records);
    projects.add("demo", "Demonstration", project_root);
    masterai::McpInboundServer server(projects, models_root, 512U);
    masterai::McpIdentity identity{
        "developer",
        {"mcp.connect", "projects.read", "models.read"},
        {"demo"}};
    std::atomic_bool cancellation{false};

    const auto initialized = server.handle(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
        "\"params\":{\"protocolVersion\":\"2025-11-25\","
        "\"capabilities\":{},\"clientInfo\":{\"name\":\"test\","
        "\"version\":\"1\"}}}",
        identity, cancellation);
    require(initialized.find("\"protocolVersion\":\"2025-11-25\"") !=
                std::string::npos,
            "MCP initialize did not negotiate the pinned revision");

    const auto tools = server.handle(
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\","
        "\"params\":{}}",
        identity, cancellation);
    require(tools.find("masterai.project.read_file") != std::string::npos &&
                tools.find("masterai.project.search") != std::string::npos,
            "MCP programming tools were not discoverable");

    const auto read = server.handle(
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"masterai.project.read_file\","
        "\"arguments\":{\"projectId\":\"demo\","
        "\"path\":\"src/answer.cpp\"}}}",
        identity, cancellation);
    require(read.find("return 42") != std::string::npos &&
                read.find("\"isError\":true") == std::string::npos,
            "authorized MCP project source read failed");

    const auto search = server.handle(
        "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"masterai.project.search\","
        "\"arguments\":{\"projectId\":\"demo\","
        "\"query\":\"searchable marker\"}}}",
        identity, cancellation);
    require(search.find("answer.cpp") != std::string::npos,
            "bounded MCP project search failed");

    const auto denied = server.handle(
        "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"masterai.project.read_file\","
        "\"arguments\":{\"projectId\":\"demo\","
        "\"path\":\"src/answer.cpp\"}}}",
        {"developer", {"mcp.connect", "projects.read"}, {}}, cancellation);
    require(denied.find("\"isError\":true") != std::string::npos &&
                denied.find("return 42") == std::string::npos,
            "MCP token without a project binding read project source");

    const auto resources = server.handle(
        "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"resources/list\","
        "\"params\":{}}",
        identity, cancellation);
    require(resources.find("masterai://project/demo") != std::string::npos,
            "authorized MCP project resource was not listed");

    const auto wrong_revision = server.handle(
        "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"initialize\","
        "\"params\":{\"protocolVersion\":\"2024-11-05\","
        "\"capabilities\":{},\"clientInfo\":{\"name\":\"old\","
        "\"version\":\"1\"}}}",
        identity, cancellation);
    require(wrong_revision.find("\"code\":-32602") != std::string::npos,
            "unsupported MCP protocol revision was accepted");

    std::istringstream input(
        "{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"initialize\","
        "\"params\":{\"protocolVersion\":\"2025-11-25\","
        "\"capabilities\":{},\"clientInfo\":{\"name\":\"stdio\","
        "\"version\":\"1\"}}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"ping\","
        "\"params\":{}}\n");
    std::ostringstream output;
    std::ostringstream error;
    cancellation.store(false);
    require(masterai::run_mcp_stdio(server, identity, input, output, error,
                                    cancellation) == 0 &&
                output.str().find("\"id\":8") != std::string::npos &&
                output.str().find("\"id\":9") != std::string::npos &&
                error.str().empty(),
            "MCP stdio transport did not preserve JSON-RPC message framing");
}

// Verifies Phase 9 durable registry restoration, executable digest pinning,
// explicit approval/project/scope checks, native stdio process isolation,
// deterministic cancellation, and sanitized audit outcomes.
void test_phase_nine_mcp_policy() {
    masterai::McpPolicy policy;
    policy.register_inbound_tool({"projects.read", "projects.read"});
    require(policy.authorize_inbound("projects.read", {"projects.read"}, true),
            "authorized inbound MCP call was denied");
    require(!policy.authorize_inbound("projects.read", {"projects.read"}, false),
            "inbound MCP project boundary was bypassed");

    masterai::McpOutboundServer server;
    server.id = "local-tools";
    server.transport = masterai::McpTransport::streamable_http;
    server.endpoint = "http://127.0.0.1:9010/mcp";
    server.allowed_tools.insert("search");
    server.enabled = true;
    policy.register_outbound_server(server);
    require(policy.authorize_outbound("local-tools", "search",
                                      {"mcp.tools.invoke"}, true, true),
            "authorized outbound MCP call was denied");
    require(!policy.authorize_outbound("local-tools", "search",
                                       {"mcp.tools.invoke"}, true, false),
            "outbound MCP approval boundary was bypassed");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::McpOutboundRegistry registry(records);
#if defined(_WIN32)
    const auto fixture =
        std::filesystem::current_path() / "masterai_fake_mcp.exe";
#else
    const auto fixture =
        std::filesystem::current_path() / "masterai_fake_mcp";
#endif
    require(std::filesystem::is_regular_file(fixture),
            "outbound MCP process fixture was not built");

    masterai::McpOutboundServer stdio_server;
    stdio_server.id = "isolated-tools";
    stdio_server.transport = masterai::McpTransport::stdio_transport;
    stdio_server.executable = fixture;
    stdio_server.allowed_tools.insert("echo");
    stdio_server.allowed_project_ids.insert("demo");
    stdio_server.enabled = true;
    stdio_server.timeout_seconds = 10U;
    stdio_server.maximum_output_bytes = 128U * 1024U;
    stdio_server.memory_limit_mib = 256U;
    const auto registered = registry.register_server(stdio_server);
    require(registered.executable_sha256.size() == 64U,
            "outbound MCP executable digest was not pinned");

    masterai::McpOutboundRegistry restored_registry(records);
    const auto restored = restored_registry.find("isolated-tools");
    require(restored.has_value() &&
                restored->executable_sha256 ==
                    registered.executable_sha256 &&
                restored->allowed_project_ids.count("demo") == 1U,
            "durable outbound MCP registry restoration failed");

    masterai::SecretStore secrets(temporary.path() / "secrets");
    masterai::AuditLog audit(temporary.path() / "audit" / "audit.log");
    masterai::McpOutboundGateway gateway(restored_registry, secrets, audit);
    masterai::McpOutboundCall call;
    call.server_id = "isolated-tools";
    call.tool = "echo";
    call.arguments_json = "{\"text\":\"hello\"}";
    call.actor_id = "developer";
    call.actor_scopes.insert("mcp.tools.invoke");
    call.actor_project_ids.insert("demo");
    call.project_id = "demo";
    call.user_approved = true;
    std::atomic_bool cancellation{false};
    const auto invoked = gateway.invoke(call, cancellation);
    require(invoked.succeeded &&
                invoked.response_json.find("sandboxed echo") !=
                    std::string::npos,
            "isolated outbound MCP stdio tool call failed");

    McpHttpFixture http_fixture;
    masterai::McpOutboundServer http_server;
    http_server.id = "loopback-tools";
    http_server.transport = masterai::McpTransport::streamable_http;
    http_server.endpoint = "http://127.0.0.1:" +
                           std::to_string(http_fixture.port()) + "/mcp";
    http_server.allowed_tools.insert("echo");
    http_server.allowed_project_ids.insert("demo");
    http_server.enabled = true;
    http_server.timeout_seconds = 5U;
    http_server.maximum_output_bytes = 128U * 1024U;
    restored_registry.register_server(http_server);
    call.server_id = "loopback-tools";
    const auto http_invoked = gateway.invoke(call, cancellation);
    require(http_invoked.succeeded &&
                http_invoked.response_json.find("loopback echo") !=
                    std::string::npos &&
                http_fixture.valid(),
            "outbound MCP Streamable HTTP client failed conformance flow");

    call.server_id = "isolated-tools";
    call.user_approved = false;
    const auto denied_approval = gateway.invoke(call, cancellation);
    require(!denied_approval.succeeded &&
                denied_approval.diagnostic.find("authorization") !=
                    std::string::npos,
            "outbound MCP call bypassed explicit user approval");
    call.user_approved = true;
    call.actor_project_ids.clear();
    const auto denied_project = gateway.invoke(call, cancellation);
    require(!denied_project.succeeded,
            "outbound MCP call bypassed caller project binding");
    call.actor_project_ids.insert("demo");
    cancellation.store(true);
    const auto cancelled = gateway.invoke(call, cancellation);
    require(cancelled.cancelled && !cancelled.succeeded,
            "pre-cancelled outbound MCP call executed");

    std::ifstream audit_input(temporary.path() / "audit" / "audit.log",
                              std::ios::binary);
    const std::string audit_text{
        std::istreambuf_iterator<char>(audit_input),
        std::istreambuf_iterator<char>()};
    require(audit_text.find("mcp.outbound") != std::string::npos &&
                audit_text.find("approved") != std::string::npos &&
                audit_text.find("denied") != std::string::npos &&
                audit_text.find("cancelled") != std::string::npos,
            "outbound MCP decisions were not fully audited");
}

// Verifies Phase 10 backend-neutral profiles, project-bound diagnostics,
// read-only diff summaries, traversal denial, and OS-protected IDE tokens.
void test_phase_ten_ide_integrations() {
    TemporaryDirectory temporary;
    const auto workspace = temporary.path() / "projects";
    const auto project_root = workspace / "demo";
    std::filesystem::create_directories(project_root / "src");
    write_text(project_root / "src" / "sample.cpp",
               "int sample() { return 1; } // TODO review\n");

    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::ProjectCatalog projects(workspace, records);
    projects.add("demo", "Demonstration", project_root);
    masterai::IdeIntegrationService ide(projects);
    require(ide.capabilities_json().find("\"appliesChanges\":false") !=
                std::string::npos,
            "IDE capability contract did not preserve read-only diff preview");

    const auto diagnostics =
        ide.diagnostics_json("demo", "src/sample.cpp", {"demo"});
    require(diagnostics.find("source-unit-comment-missing") !=
                std::string::npos &&
                diagnostics.find("unfinished-marker") != std::string::npos,
            "IDE deterministic source diagnostics were incomplete");
    bool denied_diagnostics = false;
    try {
        static_cast<void>(
            ide.diagnostics_json("demo", "src/sample.cpp", {}));
    } catch (const std::exception&) {
        denied_diagnostics = true;
    }
    require(denied_diagnostics,
            "IDE diagnostics bypassed token project authorization");

    const std::string diff =
        "--- a/src/sample.cpp\n"
        "+++ b/src/sample.cpp\n"
        "@@ -1 +1 @@\n"
        "-int sample() { return 1; } // TODO review\n"
        "+// Sample operation.\n"
        "+int sample() { return 2; }\n";
    const auto preview = ide.diff_preview_json("demo", diff, {"demo"});
    require(preview.find("\"files\":1") != std::string::npos &&
                preview.find("\"additions\":2") != std::string::npos &&
                preview.find("\"deletions\":1") != std::string::npos &&
                preview.find("\"appliesChanges\":false") !=
                    std::string::npos,
            "IDE unified-diff preview summary is incorrect");
    bool denied_traversal = false;
    try {
        static_cast<void>(ide.diff_preview_json(
            "demo",
            "--- a/../escape.cpp\n+++ b/../escape.cpp\n"
            "@@ -0,0 +1 @@\n+escape\n",
            {"demo"}));
    } catch (const std::exception&) {
        denied_traversal = true;
    }
    require(denied_traversal,
            "IDE diff preview accepted project traversal");

    auto configuration = masterai::ConfigurationManager::safe_defaults();
    const auto profile = masterai::ide_connection_profile_json(
        masterai::IdeClientKind::vscode,
        temporary.path() / "masterai.exe",
        temporary.path() / "settings.json", configuration);
    require(profile.find("Agent-Coder MCP client") != std::string::npos &&
                profile.find("\"containsCredential\":false") !=
                    std::string::npos &&
                profile.find("\"mcp-stdio\"") != std::string::npos,
            "VS Code Agent-Coder profile is incomplete or secret-bearing");
    const auto visual_studio_profile =
        masterai::ide_connection_profile_json(
            masterai::IdeClientKind::visual_studio,
            temporary.path() / "masterai.exe",
            temporary.path() / "settings.json", configuration);
    require(visual_studio_profile.find(
                "Visual Studio native external-tool bridge") !=
                std::string::npos,
            "Visual Studio profile did not use the generic native bridge");

    masterai::UserStore users(records);
    const auto administrator =
        users.create_first_administrator("TEST\\ide", "IDE Operator");
    masterai::ApiTokenStore tokens(records);
    const auto token = tokens.create(
        administrator.id,
        {"ide.connect", "mcp.connect", "projects.read"}, 100U, 100U,
        {"demo"});
    masterai::SecretStore secrets(temporary.path() / "secrets");
    masterai::AuditLog audit(temporary.path() / "audit" / "audit.log");
    if (secrets.available()) {
        const auto authority = masterai::store_ide_token(
            masterai::IdeClientKind::vscode, token, tokens, users, secrets,
            audit, 150U);
        const auto protected_token =
            secrets.get(masterai::ide_secret_name(
                masterai::IdeClientKind::vscode));
        require(authority.user_id == administrator.id &&
                    authority.project_ids.count("demo") == 1U &&
                    protected_token.has_value() &&
                    masterai::constant_time_equal(*protected_token, token),
                "IDE token was not validated and stored with OS protection");
        masterai::remove_ide_token(
            masterai::IdeClientKind::vscode, secrets, audit,
            administrator.id);
        require(!secrets
                     .get(masterai::ide_secret_name(
                         masterai::IdeClientKind::vscode))
                     .has_value(),
                "IDE token removal left the OS-protected credential active");
    }
}

// Verifies Phase 11 manifest backup/restore, tamper rejection, OS-secret and
// operational-log rotation, hash-bound upgrade rollback, and crash cleanup.
void test_phase_eleven_operations() {
    TemporaryDirectory temporary;
    const auto runtime = temporary.path() / "runtime";
    const auto settings = temporary.path() / "config" / "settings.json";
    auto configuration = masterai::ConfigurationManager::safe_defaults();
    configuration.runtime_root = runtime;
    configuration.models_root = temporary.path() / "models";
    masterai::ConfigurationManager::save_atomic(configuration, settings);

    masterai::RecordStore records(runtime / "database");
    records.open();
    records.put("operations_test", "preserved", "phase-eleven");
    records.checkpoint();
    masterai::AuditLog(runtime / "audit" / "audit.log")
        .append("test.prepare", "test", "success", "backup fixture");
    write_text(runtime / "attachments" / "test" / "fixture.txt",
               "portable attachment\n");

    masterai::OperationsManager operations(runtime);
    const auto backup =
        operations.create_backup(settings, temporary.path() / "backups");
    require(backup.file_count >= 4U &&
                backup.manifest_sha256.size() == 64U &&
                std::filesystem::is_regular_file(
                    backup.backup_path / "manifest.tsv"),
            "Phase 11 portable backup manifest is incomplete");

    const auto restored_settings =
        temporary.path() / "clean-host" / "config" / "settings.json";
    const auto restored_runtime =
        temporary.path() / "clean-host" / "runtime";
    const auto restored = masterai::OperationsManager::restore_backup(
        backup.backup_path, restored_settings, restored_runtime);
    masterai::RecordStore restored_records(restored_runtime / "database");
    restored_records.open();
    const auto restored_value =
        restored_records.get("operations_test", "preserved");
    const auto restored_configuration =
        masterai::ConfigurationManager::load(restored_settings);
    require(restored.file_count == backup.file_count &&
                restored_value.has_value() &&
                *restored_value == "phase-eleven" &&
                restored_configuration.runtime_root == restored_runtime &&
                std::filesystem::is_regular_file(
                    restored_runtime / "attachments" / "test" /
                    "fixture.txt"),
            "clean-host recovery exercise did not restore verified state");

    write_text(backup.backup_path / "runtime" / "attachments" / "test" /
                   "fixture.txt",
               "tampered attachment\n");
    bool tamper_rejected = false;
    try {
        static_cast<void>(masterai::OperationsManager::restore_backup(
            backup.backup_path,
            temporary.path() / "tampered" / "settings.json",
            temporary.path() / "tampered" / "runtime"));
    } catch (const std::exception&) {
        tamper_rejected = true;
    }
    require(tamper_rejected,
            "backup restore accepted a digest-mismatched entry");

    write_text(runtime / "run" / "masterai.log",
               std::string(5000U, 'L'));
    require(operations.rotate_logs(4096U, 3U, "test") == 1U &&
                std::filesystem::is_regular_file(
                    runtime / "run" / "masterai.log.1"),
            "operational log rotation did not preserve its archive");

    masterai::SecretStore secrets(runtime / "secrets");
    if (secrets.available()) {
        auto rotated = operations.rotate_secret("phase11-test", "test");
        const auto protected_value = secrets.get("phase11-test");
        require(rotated.size() == 64U && protected_value.has_value() &&
                    masterai::constant_time_equal(rotated,
                                                  *protected_value),
                "Phase 11 secret rotation did not use native protection");
        std::fill(rotated.begin(), rotated.end(), '\0');
        secrets.erase("phase11-test");
    }

    const auto active = temporary.path() / "upgrade" / "masterai.bin";
    const auto candidate = temporary.path() / "upgrade" / "candidate.bin";
    write_text(active, "release-one");
    write_text(candidate, "release-two");
    const auto upgrade = operations.install_upgrade(
        active, candidate, temporary.path() / "rollbacks", "test");
    require(masterai::sha256_file_hex(active) ==
                masterai::sha256_file_hex(candidate),
            "offline upgrade did not install the verified candidate");
    const auto rollback =
        operations.rollback_upgrade(active, upgrade.receipt_path, "test");
    require(masterai::sha256_file_hex(active) ==
                upgrade.previous_sha256 &&
                rollback.installed_sha256 == upgrade.previous_sha256,
            "hash-bound rollback did not restore the prior executable");

    write_text(runtime / "run" / "stop.request", "stale");
    write_text(runtime / "database" / "records.snapshot.tmp", "stale");
    const auto recovery = operations.recover("test");
    require(recovery.record_store_validated &&
                recovery.stale_stop_request_removed &&
                recovery.stale_checkpoint_removed,
            "crash recovery did not clear known residue and validate records");
}

// Verifies the Phase 12 probe remains deterministic without imposing a
// hardware-dependent wall-clock threshold on functional validation.
void test_phase_twelve_performance_probe() {
    const auto report =
        masterai::run_control_plane_performance_probe(100U);
    require(report.samples.size() == 3U && report.checksum != 0U,
            "control-plane performance probe did not execute every sample");
    for (const auto& sample : report.samples) {
        require(sample.operations >= 100U &&
                    sample.input_bytes > 0U &&
                    sample.elapsed_nanoseconds > 0U &&
                    sample.nanoseconds_per_operation > 0.0 &&
                    sample.mebibytes_per_second > 0.0,
                "performance sample omitted measurement evidence");
    }
}

// Verifies Phase 13 trace transitions use real monotonic measurements, retain
// native resource attribution, and produce a fully keyed reproducible baseline.
void test_phase_thirteen_query_measurement() {
    TemporaryDirectory temporary;
    const auto model_directory = temporary.path() / "model";
    std::filesystem::create_directories(model_directory);
    const auto model_file = model_directory / "model.gguf";
    write_text(model_file, "GGUF-query-trace-fixture");
    masterai::ModelRecord model;
    model.directory = model_directory;
    model.manifest.id = "query-fixture";
    model.manifest.model_file = "model.gguf";
    model.manifest.model_size_bytes = std::filesystem::file_size(model_file);
    model.manifest.model_sha256 = masterai::sha256_file_hex(model_file);
    model.state = masterai::ModelState::ready;
    const auto backend = fake_llama_executable();
    masterai::RunnerSupervisor supervisor(backend,
                                          temporary.path() / "runtime");
    masterai::QueryCoordinator queries(2U);
    const auto id = queries.begin("operator", "project", "model");
    queries.transition(id, masterai::QueryStage::authentication,
                       masterai::QueryStatus::accepted);
    queries.transition(id, masterai::QueryStage::retrieval,
                       masterai::QueryStatus::retrieving);
    queries.observe_resources(id, 1024U);
    queries.transition(id, masterai::QueryStage::runner_queue,
                       masterai::QueryStatus::queued);
    supervisor.load(model, 4096U, 18082U, 5U);
    queries.transition(id, masterai::QueryStage::prompt_evaluation,
                       masterai::QueryStatus::evaluating_prompt);
    std::atomic_bool cancellation{false};
    bool first_token = true;
    const auto generated = supervisor.generate(
        "trace request", {},
        [&](const std::string&) {
            if (first_token) {
                first_token = false;
                queries.transition(id, masterai::QueryStage::generation,
                                   masterai::QueryStatus::generating);
            }
        },
        cancellation);
    require(!first_token, "query trace fixture did not stream a token");
    queries.record_inference(id, generated, 500U);
    queries.observe_resources(id, supervisor.metrics().resident_memory_bytes);
    queries.transition(id, masterai::QueryStage::release,
                       masterai::QueryStatus::generating);
    supervisor.unload(0U);
    queries.finish(id, masterai::QueryStatus::completed);
    const auto trace = queries.find(id);
    require(trace.has_value() &&
                trace->status == masterai::QueryStatus::completed &&
                trace->stages.size() == 7U &&
                trace->time_to_first_token_microseconds == 500U &&
                trace->generation_tokens_per_second > 0.0 &&
                trace->control_plane_peak.resident_memory_bytes > 0U &&
                trace->runner_peak_resident_memory_bytes >= 1024U,
            "query trace omitted monotonic timing or resource evidence");
    for (const auto& stage : trace->stages) {
        require(stage.elapsed_microseconds > 0U,
                "query trace retained an unclosed pipeline stage");
    }
    const auto hardware =
        masterai::probe_hardware(std::filesystem::current_path());
    require(hardware.logical_cpu_count > 0U &&
                hardware.physical_cpu_count > 0U &&
                hardware.total_ram_mib > 0U &&
                !hardware.storage_class.empty(),
            "native Phase 13 host probe omitted required evidence");
    const auto baseline = masterai::make_query_baseline(
        hardware, "model-hash", "backend-hash", "build-hash",
        "settings-hash", "suite-hash", false, *trace, 10U);
    require(baseline.host_hash.size() == 64U &&
                baseline.instrumentation_overhead_nanoseconds > 0U &&
                masterai::QueryCoordinator::to_json(*trace).find(
                    "\"status\":\"completed\"") != std::string::npos,
            "Phase 13 baseline was not reproducibly keyed");
}

// Verifies Phase 14 admission includes every estimate component, pressure
// remains under a hard ceiling, bounded queues reject saturation, and release
// restores a clean shutdown state.
void test_phase_fourteen_bounded_memory() {
    auto hardware =
        masterai::probe_hardware(std::filesystem::current_path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware,
        512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);
    masterai::MemoryEstimate estimate;
    estimate.weights_bytes = 128ULL * 1024ULL * 1024ULL;
    estimate.runtime_buffer_bytes = 32ULL * 1024ULL * 1024ULL;
    estimate.kv_bytes_per_sequence = 16ULL * 1024ULL * 1024ULL;
    estimate.sequences = 2U;
    estimate.transient_bytes = 8ULL * 1024ULL * 1024ULL;
    estimate.safety_margin_bytes = 16ULL * 1024ULL * 1024ULL;
    const auto admitted = memory.reserve(
        masterai::MemoryCategory::runner_weights, estimate, true);
    require(admitted.admitted && admitted.reserved_bytes ==
                216ULL * 1024ULL * 1024ULL,
            "memory admission omitted an estimate component");
    masterai::MemoryEstimate overflow;
    overflow.weights_bytes = 400ULL * 1024ULL * 1024ULL;
    const auto denied = memory.reserve(
        masterai::MemoryCategory::background_jobs, overflow, false);
    require(!denied.admitted && !denied.corrective_actions.empty(),
            "hard memory ceiling admitted an oversized background job");
    const auto sampled = memory.sample();
    require(sampled.reserved_bytes <= sampled.hard_limit_bytes &&
                masterai::MemoryBudgetManager::to_json(sampled).find(
                    "\"hardLimitBytes\"") != std::string::npos,
            "memory status omitted ceiling evidence");
    memory.release(admitted.lease_id);
    require(memory.status().reserved_bytes == 0U,
            "memory lease release did not restore shutdown headroom");

    masterai::BoundedWorkQueue queue(2U, 100U);
    require(queue.enqueue("background", 40U, false) &&
                queue.enqueue("interactive", 40U, true) &&
                !queue.enqueue("overflow", 40U, true) &&
                queue.begin_next() == "interactive",
            "bounded queue failed priority or saturation policy");
    queue.cancel_all();
    require(queue.queued() == 0U && queue.active() == 0U &&
                queue.payload_bytes() == 0U,
            "queue cancellation retained bounded work state");
}

// Verifies Phase 15 bounded discovery, partial usefulness, incremental
// generation publication, restart recovery, cancellation, and corrupt-active
// fallback to the prior checksummed generation.
void test_phase_fifteen_disk_backed_indexing() {
    TemporaryDirectory temporary;
    const auto project_root = temporary.path() / "project";
    std::filesystem::create_directories(project_root / "src");
    write_text(project_root / "src" / "one.cpp",
               "// one\nint searchable_symbol = 1;\n");
    write_text(project_root / "src" / "two.cpp",
               "// two\nint second_symbol = searchable_symbol;\n");
    masterai::ProjectRecord project{"phase15", "Phase 15", project_root};
    const auto hardware =
        masterai::probe_hardware(temporary.path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware,
        512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);
    const auto index_root = temporary.path() / "indexes";
    masterai::ProjectIndexer index(project, index_root, memory);
    std::atomic_bool cancellation{false};
    const auto first = index.rebuild(cancellation, 1U);
    require(first.generation == 1U && first.files_indexed == 2U &&
                first.partial && first.disk_bytes > 0U &&
                !index.search_text("searchable_symbol", 8U).empty(),
            "disk-backed index did not publish bounded partial results");
    const auto unchanged = index.rebuild(cancellation, 0U);
    require(unchanged.generation == 2U &&
                unchanged.files_unchanged == 2U &&
                unchanged.files_indexed == 0U,
            "unchanged-file elimination rebuilt stable project content");
    write_text(project_root / "src" / "two.cpp",
               "// changed\nint updated_symbol = searchable_symbol;\n");
    const auto second =
        index.update({project_root / "src" / "two.cpp"}, cancellation);
    require(second.generation == 3U && second.files_indexed == 1U &&
                !index.search_symbol("updated_symbol", 8U).empty() &&
                index.search_symbol("symbol", 8U).empty() &&
                !index.search_text("searchable_symbol", 8U).empty(),
            "affected-path update or exact symbol retrieval was not incremental");
    const auto manifest = index_root / "phase15" / "manifest";
    std::ifstream manifest_input(manifest);
    std::uint64_t generation = 0U;
    std::string segment_name;
    std::string digest;
    manifest_input >> generation >> segment_name >> digest;
    manifest_input.close();
    write_text(index_root / "phase15" / segment_name, "corrupt");
    masterai::ProjectIndexer recovered(project, index_root, memory);
    require(recovered.status().generation == 2U &&
                !recovered.search_text("searchable_symbol", 8U).empty(),
            "corrupt active segment did not preserve the prior generation");
    cancellation.store(true);
    const auto cancelled = recovered.rebuild(cancellation, 1U);
    require(cancelled.cancelled &&
                recovered.status().generation == 2U,
            "cancelled rebuild replaced the last valid generation");
    cancellation.store(false);
    bool escaped_path_rejected = false;
    try {
        recovered.update({temporary.path() / "outside.cpp"}, cancellation);
    } catch (const std::invalid_argument&) {
        escaped_path_rejected = true;
    }
    require(escaped_path_rejected,
            "incremental update accepted a path outside the project");
    std::filesystem::remove(project_root / "src" / "two.cpp");
    const auto removed_one = recovered.update(
        {project_root / "src" / "two.cpp"}, cancellation);
    require(removed_one.generation == 3U &&
                recovered.search_symbol("second_symbol", 8U).empty() &&
                !recovered.search_symbol("searchable_symbol", 8U).empty(),
            "deleted-path update retained stale symbol chunks");
    std::filesystem::remove(project_root / "src" / "one.cpp");
    const auto removed_all = recovered.update(
        {project_root / "src" / "one.cpp"}, cancellation);
    masterai::ProjectIndexer empty_recovered(project, index_root, memory);
    require(removed_all.generation == 4U && removed_all.chunks == 0U &&
                empty_recovered.status().generation == 4U &&
                empty_recovered.search_text("searchable_symbol", 8U).empty(),
            "empty generation was not durably published after deletion");
    write_text(project_root / "src" / "one.cpp",
               "// one\nint searchable_symbol = 1;\n");
    write_text(project_root / "src" / "two.cpp",
               "// two\nint second_symbol = searchable_symbol;\n");

    masterai::ProjectIndexService service(
        temporary.path() / "service-indexes", memory, 2U);
    require(service.request_rebuild(project) &&
                !service.request_rebuild(project),
            "index service admitted duplicate project work");
    std::optional<masterai::IndexServiceStatus> service_status;
    for (unsigned int attempt = 0U; attempt < 200U; ++attempt) {
        service_status = service.status(project.id);
        if (service_status &&
            (service_status->state == masterai::IndexJobState::ready ||
             service_status->state == masterai::IndexJobState::failed)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(service_status &&
                service_status->state == masterai::IndexJobState::ready &&
                service_status->index.generation == 1U &&
                service_status->index.files_indexed == 2U,
            "bounded index service did not publish observable completion");
    write_text(project_root / "src" / "two.cpp",
               "// saved\nint saved_symbol = searchable_symbol;\n");
    bool update_admitted = false;
    for (unsigned int attempt = 0U; attempt < 200U && !update_admitted;
         ++attempt) {
        update_admitted = service.request_update(
            project, {project_root / "src" / "two.cpp"},
            masterai::IndexTrigger::save);
        if (!update_admitted) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    require(update_admitted, "save trigger was not admitted after rebuild");
    for (unsigned int attempt = 0U; attempt < 200U; ++attempt) {
        service_status = service.status(project.id);
        if (service_status &&
            service_status->state == masterai::IndexJobState::ready &&
            service_status->index.generation == 2U) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(service_status &&
                service_status->state == masterai::IndexJobState::ready &&
                service_status->trigger == masterai::IndexTrigger::save &&
                service_status->index.generation == 2U &&
                service_status->index.files_indexed == 1U,
            "save trigger did not publish one affected-path generation");

    masterai::RecordStore records(temporary.path() / "http-database");
    records.open();
    masterai::ProjectCatalog projects(temporary.path(), records);
    projects.add(project.id, project.display_name, project.root);
    masterai::AttachmentStore attachments(
        temporary.path() / "attachments", records);
    masterai::BenchmarkStore benchmarks(records);
    masterai::AuditLog audit(temporary.path() / "audit.log");
    auto configuration = masterai::ConfigurationManager::safe_defaults();
    configuration.runtime_root = temporary.path();
    configuration.models_root = temporary.path() / "models";
    masterai::CacheManager workload_cache(temporary.path() / "http-cache",
                                          memory, masterai::CachePolicy{});
    masterai::server_internal::WorkloadHttpController workloads(
        configuration, projects, attachments, nullptr, nullptr, benchmarks,
        service, audit, workload_cache);
    const masterai::UserRecord administrator{
        "admin", "TEST\\operator", "Operator",
        masterai::UserRole::administrator, true};
    masterai::server_internal::Request request;
    request.method = "GET";
    request.target = "/api/v1/projects/phase15/index";
    require(workloads.index_status(request, false, {}).find(
                "project_binding_required") != std::string::npos &&
                workloads.index_status(request, true, {}).find(
                    "\"state\":\"ready\"") != std::string::npos,
            "index status route bypassed project binding or hid ready state");
    require(workloads.index_status(request, true, {}).find(
                "\"trigger\":\"save\"") != std::string::npos,
            "index status route omitted the applied change trigger");
    request.method = "POST";
    request.target = "/api/v1/projects/phase15/index/rebuild";
    require(workloads
                .rebuild_index(request, administrator, false, {"phase15"})
                .find("202 Accepted") != std::string::npos,
            "authorized index rebuild route did not admit background work");
    for (unsigned int attempt = 0U; attempt < 200U; ++attempt) {
        service_status = service.status("phase15");
        if (service_status &&
            service_status->state == masterai::IndexJobState::ready) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(service_status &&
                service_status->state == masterai::IndexJobState::ready,
            "route-triggered rebuild did not settle before notify checks");
    request.target = "/api/v1/projects/phase15/index/notify";
    request.body = "{\"trigger\":\"unsupported\",\"paths\":[]}";
    require(workloads
                .notify_index(request, administrator, false, {"phase15"})
                .find("invalid_index_trigger") != std::string::npos,
            "index notify route accepted an unsupported trigger");
    request.body = "{\"trigger\":\"save\",\"paths\":[\"src/two.cpp\"]}";
    std::string notify_response;
    for (unsigned int attempt = 0U; attempt < 200U; ++attempt) {
        notify_response =
            workloads.notify_index(request, administrator, false, {"phase15"});
        if (notify_response.find("202 Accepted") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(notify_response.find("202 Accepted") != std::string::npos &&
                notify_response.find("\"trigger\":\"save\"") !=
                    std::string::npos,
            "authorized save notify route did not admit an affected-path update");
    require(workloads
                .notify_index(request, administrator, false, {"other-project"})
                .find("project_binding_required") != std::string::npos,
            "index notify route bypassed project binding");
    request.body = "{\"trigger\":\"branch-switch\"}";
    for (unsigned int attempt = 0U; attempt < 200U; ++attempt) {
        service_status = service.status("phase15");
        if (service_status &&
            service_status->state == masterai::IndexJobState::ready) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(workloads
                .notify_index(request, administrator, false, {"phase15"})
                .find("\"trigger\":\"branch-switch\"") != std::string::npos,
            "branch-switch notify route did not promote a full-scan trigger");
}

// Exercises the native Phase 15 live adapter end to end: a real ProjectWatcher
// observing a real project directory on disk, with no HTTP layer and no
// external editor/watcher/VCS process involved, must itself notice a file
// save and a `.git/HEAD` branch switch and forward them into a real
// ProjectIndexService.
void test_phase_fifteen_project_watcher() {
    TemporaryDirectory temporary;
    const auto project_root = temporary.path() / "watched-project";
    std::filesystem::create_directories(project_root / "src");
    std::filesystem::create_directories(project_root / ".git");
    write_text(project_root / "src" / "one.cpp",
               "// one\nint watcher_symbol = 1;\n");
    write_text(project_root / ".git" / "HEAD", "ref: refs/heads/main\n");

    masterai::RecordStore records(temporary.path() / "watcher-database");
    records.open();
    masterai::ProjectCatalog catalog(temporary.path(), records);
    const auto project =
        catalog.add("watched", "Watched project", project_root);

    const auto hardware = masterai::probe_hardware(temporary.path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware,
        512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);
    masterai::ProjectIndexService service(
        temporary.path() / "watcher-indexes", memory, 2U);

    // Constructing the watcher starts its background thread; it must find
    // and index the project on its own, with nobody calling
    // request_rebuild/request_update directly.
    masterai::ProjectWatcher watcher(catalog, service);

    std::optional<masterai::IndexServiceStatus> status;
    for (unsigned int attempt = 0U; attempt < 400U; ++attempt) {
        status = service.status("watched");
        if (status && status->state == masterai::IndexJobState::ready &&
            status->index.generation >= 1U) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    require(status && status->state == masterai::IndexJobState::ready &&
                status->index.files_indexed >= 1U,
            "ProjectWatcher did not discover and index a new project on its own");
    const auto initial_generation = status->index.generation;

    write_text(project_root / "src" / "two.cpp",
               "// two\nint second_watcher_symbol = watcher_symbol;\n");
    for (unsigned int attempt = 0U; attempt < 400U; ++attempt) {
        status = service.status("watched");
        if (status && status->state == masterai::IndexJobState::ready &&
            status->index.generation > initial_generation) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    require(status && status->index.generation > initial_generation &&
                status->trigger == masterai::IndexTrigger::watcher &&
                status->index.files_indexed >= 1U,
            "ProjectWatcher did not forward an observed file save");
    const auto after_save_generation = status->index.generation;

    write_text(project_root / ".git" / "HEAD", "ref: refs/heads/feature\n");
    for (unsigned int attempt = 0U; attempt < 400U; ++attempt) {
        status = service.status("watched");
        if (status && status->state == masterai::IndexJobState::ready &&
            status->trigger == masterai::IndexTrigger::branch_switch &&
            status->index.generation > after_save_generation) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    require(status && status->trigger == masterai::IndexTrigger::branch_switch &&
                status->index.generation > after_save_generation,
            "ProjectWatcher did not forward a .git/HEAD branch switch as a "
            "full-scan trigger");
}

// Verifies Phase 16 deadline-bound hybrid retrieval: exact-symbol strategy
// selection and fusion/dedup across an identifier and its literal text,
// live re-read of a project's current index generation with no planner-side
// caching (so index/membership changes are reflected on the very next
// call), a near-zero deadline that returns quickly with partial evidence
// instead of blocking, context-budget capping with full disclosure of what
// was included versus omitted, and query-trace disclosure recording that
// survives a later successful finish().
void test_phase_sixteen_deadline_bound_retrieval() {
    TemporaryDirectory temporary;
    const auto project_root = temporary.path() / "retrieval-project";
    std::filesystem::create_directories(project_root / "src");
    write_text(project_root / "src" / "one.cpp",
               "// one\nint retrieval_marker_symbol = 41;\n");
    masterai::ProjectRecord project{"phase16", "Phase 16", project_root};

    const auto hardware = masterai::probe_hardware(temporary.path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware,
        512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);
    masterai::ProjectIndexService service(
        temporary.path() / "retrieval-indexes", memory, 2U);

    masterai::RetrievalPlanner planner(service);
    masterai::RetrievalRequest request;
    request.project = project;
    request.query_text = "please look at retrieval_marker_symbol";
    request.deadline = std::chrono::milliseconds(2000);
    request.maximum_context_bytes = 16U * 1024U;
    request.maximum_chunks_per_source = 6U;
    request.maximum_total_chunks = 20U;

    const auto before_index = planner.retrieve(request);
    require(before_index.context_text.empty() &&
                before_index.disclosure.empty() && !before_index.partial,
            "retrieval fabricated evidence for a project with no published index");

    require(service.request_rebuild(project),
            "index rebuild was not admitted for the retrieval fixture");
    std::optional<masterai::IndexServiceStatus> status;
    for (unsigned int attempt = 0U; attempt < 200U; ++attempt) {
        status = service.status(project.id);
        if (status && status->state == masterai::IndexJobState::ready) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(status && status->state == masterai::IndexJobState::ready,
            "retrieval fixture index did not settle before retrieval");

    const auto found = planner.retrieve(request);
    require(!found.partial && found.strategy == "exact_symbol" &&
                !found.disclosure.empty() &&
                found.context_text.find("retrieval_marker_symbol") !=
                    std::string::npos &&
                found.context_text.find("src") != std::string::npos,
            "exact-symbol retrieval did not surface the indexed evidence");
    require(found.disclosure.front().included &&
                found.disclosure.front().index_generation ==
                    status->index.generation,
            "retrieval disclosure omitted the index generation it read from");

    // Fusion/dedup relies on canonical chunk identity: the same unchanged
    // chunk returned by an exact-symbol lookup and a literal-text lookup
    // must carry the same id, or the planner's fused map (keyed by id)
    // could not collapse them into one disclosure row instead of two.
    const auto symbol_hit =
        service.search_symbol(project.id, "retrieval_marker_symbol", 8U);
    const auto text_hit =
        service.search_text(project.id, "retrieval_marker_symbol", 8U);
    require(symbol_hit.available && text_hit.available &&
                symbol_hit.chunks.size() == 1U && text_hit.chunks.size() == 1U &&
                symbol_hit.chunks.front().id == text_hit.chunks.front().id,
            "index search results lost the canonical chunk identity retrieval "
            "fusion depends on");

    // No planner-side caching: updating the index and calling retrieve again
    // with the same planner instance must see the new generation immediately.
    write_text(project_root / "src" / "one.cpp",
               "// one\nint retrieval_marker_symbol = 41;\n"
               "int second_retrieval_marker = 2;\n");
    bool update_admitted = false;
    for (unsigned int attempt = 0U; attempt < 200U && !update_admitted;
         ++attempt) {
        update_admitted = service.request_update(
            project, {project_root / "src" / "one.cpp"},
            masterai::IndexTrigger::save);
        if (!update_admitted) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(update_admitted, "retrieval fixture update was not admitted");
    for (unsigned int attempt = 0U; attempt < 200U; ++attempt) {
        status = service.status(project.id);
        if (status && status->state == masterai::IndexJobState::ready &&
            status->index.generation > found.disclosure.front().index_generation) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    masterai::RetrievalRequest updated_request = request;
    updated_request.query_text = "second_retrieval_marker";
    const auto after_update = planner.retrieve(updated_request);
    require(!after_update.disclosure.empty() &&
                after_update.disclosure.front().index_generation >
                    found.disclosure.front().index_generation,
            "retrieval read a stale index generation after an incremental update");

    // Deadline-bound: an already-expired deadline must return quickly with
    // partial evidence rather than blocking on any strategy step.
    masterai::RetrievalRequest expired_request = request;
    expired_request.deadline = std::chrono::milliseconds(0);
    const auto deadline_started = std::chrono::steady_clock::now();
    const auto expired = planner.retrieve(expired_request);
    const auto deadline_elapsed = std::chrono::steady_clock::now() - deadline_started;
    require(deadline_elapsed < std::chrono::seconds(2) &&
                (expired.partial || expired.disclosure.empty()),
            "deadline-bound retrieval blocked instead of returning bounded "
            "partial evidence");

    // Context budgeting: synthetic ranked candidates exceeding the byte
    // budget must keep the highest-ranked evidence and disclose the rest as
    // omitted rather than truncating a kept chunk's text.
    std::vector<masterai::RetrievalCandidate> ranked;
    for (unsigned int index = 0U; index < 5U; ++index) {
        masterai::IndexChunk chunk;
        chunk.id = "chunk-" + std::to_string(index);
        chunk.relative_path = "src/big.cpp";
        chunk.offset = index * 4096ULL;
        chunk.language = "cpp";
        chunk.text = std::string(3000U, 'x');
        chunk.digest = "digest-" + std::to_string(index);
        masterai::RetrievalDisclosureEntry disclosure;
        disclosure.source = "exact_text";
        disclosure.relative_path = chunk.relative_path;
        disclosure.offset = chunk.offset;
        disclosure.index_generation = 1U;
        disclosure.score = 5.0 - static_cast<double>(index);
        disclosure.reason = "synthetic";
        ranked.push_back({chunk, disclosure});
    }
    const auto budgeted = masterai::ContextBudgeter::apply(
        ranked, "exact_text", false, "", 7000U, 6U, 20U);
    std::size_t included_count = 0U;
    for (const auto& entry : budgeted.disclosure) {
        if (entry.included) ++included_count;
    }
    require(budgeted.disclosure.size() == 5U && included_count < 5U &&
                included_count >= 2U &&
                budgeted.context_text.size() <= 7000U + 5U * 200U,
            "context budgeter admitted more evidence than the byte cap allows");
    require(budgeted.disclosure.front().included &&
                !budgeted.disclosure.back().included,
            "context budgeter did not prefer the highest-ranked evidence");

    // Query-trace disclosure: recorded retrieval survives a later successful
    // finish() call, which passes an empty diagnostic by default.
    masterai::QueryCoordinator queries;
    const auto query_id = queries.begin("user", project.id, "model");
    queries.record_retrieval(query_id, found.partial,
                             "{\"strategy\":\"exact_symbol\",\"entries\":[]}");
    queries.finish(query_id, masterai::QueryStatus::completed);
    const auto trace = queries.find(query_id);
    require(trace && trace->retrieval_disclosure ==
                          "{\"strategy\":\"exact_symbol\",\"entries\":[]}" &&
                trace->diagnostic.empty(),
            "a successful finish() erased the recorded retrieval disclosure");
    require(masterai::QueryCoordinator::to_json(*trace).find(
                "\"retrievalDisclosure\":{") != std::string::npos,
            "query trace JSON omitted the retrieval disclosure");
}

void test_phase_seventeen_security_partitioned_cache() {
    TemporaryDirectory temporary;
    const auto hardware = masterai::probe_hardware(temporary.path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware,
        512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);

    const auto cache_root = temporary.path() / "cache";
    masterai::CachePolicy cache_policy;
    cache_policy.maximum_bytes_per_category = 1024ULL * 1024ULL;

    masterai::CacheKey base_key;
    base_key.user_id = "user-a";
    base_key.project_id = "project-a";
    base_key.canonical_identity = "please look at retrieval_marker_symbol";
    base_key.version_tag = "retrieval-v1";
    base_key.index_generation = 1U;

    // Repeated identical retrieval query: a miss, then a hit with the exact
    // value that was stored, without ever touching RetrievalPlanner again.
    {
        masterai::CacheManager cache(cache_root, memory, cache_policy);
        require(!cache.get(masterai::CacheCategory::retrieval_result, base_key)
                     .has_value(),
                "cache served a value before anything was ever put");
        cache.put(masterai::CacheCategory::retrieval_result, base_key,
                 "cached-context-text");
        const auto hit =
            cache.get(masterai::CacheCategory::retrieval_result, base_key);
        require(hit.has_value() && *hit == "cached-context-text",
                "cache did not return the exact value it was given");
        const auto status = cache.status();
        const auto found =
            status.categories.find(masterai::CacheCategory::retrieval_result);
        require(found != status.categories.end() && found->second.hits == 1U &&
                    found->second.misses == 1U && found->second.entries == 1U,
                "cache status did not reflect the recorded hit/miss/entry");
    }

    // Restart: a fresh CacheManager over the same cache_root can still read
    // the previously-put entry, and status() reflects it immediately.
    {
        masterai::CacheManager cache(cache_root, memory, cache_policy);
        const auto hit =
            cache.get(masterai::CacheCategory::retrieval_result, base_key);
        require(hit.has_value() && *hit == "cached-context-text",
                "cache entry did not survive a simulated restart");
    }

    // A republished index generation makes the previous entry unreachable
    // (structural staleness, not an active purge).
    {
        masterai::CacheManager cache(cache_root, memory, cache_policy);
        auto newer_generation_key = base_key;
        newer_generation_key.index_generation = 2U;
        require(!cache
                     .get(masterai::CacheCategory::retrieval_result,
                         newer_generation_key)
                     .has_value(),
                "a stale index generation was still reachable after republish");
    }

    // invalidate_policy() makes a previously-cached key for the same
    // identity unreachable.
    {
        masterai::CacheManager cache(cache_root, memory, cache_policy);
        require(cache.get(masterai::CacheCategory::retrieval_result, base_key)
                    .has_value(),
                "setup for the policy-invalidation case lost its seed entry");
        cache.invalidate_policy();
        auto bumped_key = base_key;
        bumped_key.policy_generation = cache.current_policy_generation();
        require(!cache.get(masterai::CacheCategory::retrieval_result, bumped_key)
                     .has_value(),
                "invalidate_policy() left a prior-generation entry reachable "
                "under the new generation");
    }

    // Cross-project and cross-user isolation: otherwise-identical identities
    // never collide.
    {
        masterai::CacheManager cache(cache_root, memory, cache_policy);
        cache.put(masterai::CacheCategory::retrieval_result, base_key,
                 "project-a-value");
        auto other_project_key = base_key;
        other_project_key.project_id = "project-b";
        require(!cache
                     .get(masterai::CacheCategory::retrieval_result,
                         other_project_key)
                     .has_value(),
                "a different project reused another project's cache entry");
        auto other_user_key = base_key;
        other_user_key.user_id = "user-b";
        require(!cache
                     .get(masterai::CacheCategory::retrieval_result,
                         other_user_key)
                     .has_value(),
                "a different user reused another user's cache entry");
    }

    // Corruption: a tampered checksum on disk is a miss, not a throw or bad
    // data, and the corrupt entry is removed (quarantine-by-deletion).
    {
        masterai::CacheManager cache(cache_root, memory, cache_policy);
        masterai::CacheKey corrupt_key = base_key;
        corrupt_key.canonical_identity = "corruption-fixture";
        cache.put(masterai::CacheCategory::retrieval_result, corrupt_key,
                 "original-value");
        const auto entry_path = cache_root / "retrieval_result" /
                                (corrupt_key.to_cache_id() + ".entry");
        require(std::filesystem::is_regular_file(entry_path),
                "cache entry was not written to the expected disk path");
        {
            std::ofstream corrupt(entry_path,
                                  std::ios::binary | std::ios::trunc);
            corrupt << "MASTERAI-CACHE-1 5 0000000000000000000000000000000000"
                       "000000000000000000000000000000 - -\nWRONG";
        }
        require(!cache
                     .get(masterai::CacheCategory::retrieval_result,
                         corrupt_key)
                     .has_value(),
                "a checksum-corrupted cache entry was still served");
        require(!std::filesystem::is_regular_file(entry_path),
                "a corrupted cache entry was not quarantined by deletion");
    }

    // Segmented LRU capacity: filling a category past its byte cap evicts
    // the oldest entry first and the category never exceeds its cap.
    {
        masterai::CachePolicy small_policy;
        small_policy.maximum_bytes_per_category = 300U;
        masterai::CacheManager cache(temporary.path() / "small-cache", memory,
                                     small_policy);
        std::vector<masterai::CacheKey> keys;
        for (unsigned int index = 0U; index < 5U; ++index) {
            masterai::CacheKey key = base_key;
            key.canonical_identity = "lru-fixture-" + std::to_string(index);
            keys.push_back(key);
            cache.put(masterai::CacheCategory::retrieval_result, key,
                     std::string(100U, 'x'));
        }
        require(!cache
                     .get(masterai::CacheCategory::retrieval_result, keys.front())
                     .has_value(),
                "the oldest entry was not evicted once the category exceeded "
                "its byte cap");
        require(cache.get(masterai::CacheCategory::retrieval_result, keys.back())
                    .has_value(),
                "the most recently inserted entry was evicted before the "
                "oldest");
        const auto status = cache.status();
        const auto found =
            status.categories.find(masterai::CacheCategory::retrieval_result);
        require(found != status.categories.end() &&
                    found->second.used_bytes <=
                        small_policy.maximum_bytes_per_category &&
                    found->second.evictions > 0U,
                "the cache category exceeded its configured byte cap");
    }
}

// Phase 18: PromptSessionManager is process-lifetime, in-memory state with
// no disk/network dependency, so it is exercised directly rather than
// through a full HttpServer/fake-runner round trip -- the compatibility and
// eviction rules it enforces are independent of how the runner itself is
// launched (that part is covered separately by the --parallel launch-spec
// check below).
void test_phase_eighteen_prompt_session_reuse() {
    masterai::SessionFingerprint fingerprint;
    fingerprint.model_sha256 = "model-a-digest";
    fingerprint.backend_executable = "/opt/llama-server";
    fingerprint.architecture = "llama";
    fingerprint.context_length = 4096U;
    fingerprint.project_index_generation = 1U;

    // No prior turn for this chat: reuse must be refused, never guessed.
    {
        masterai::PromptSessionManager sessions(4U, 300U);
        const auto decision =
            sessions.try_reuse("chat-a", fingerprint, "turn one prompt");
        require(!decision.reuse,
                "a chat with no recorded session was offered reuse");
    }

    // Identical fingerprint and a literal-prefix-extending second turn: the
    // slot recorded after the first turn is offered back for the second.
    {
        masterai::PromptSessionManager sessions(4U, 300U);
        const std::string turn_one = "system+turn-one";
        const auto first_slot =
            sessions.record("chat-a", fingerprint, turn_one, std::nullopt);
        const std::string turn_two = turn_one + "+turn-two";
        const auto decision =
            sessions.try_reuse("chat-a", fingerprint, turn_two);
        require(decision.reuse && decision.slot_id == first_slot,
                "an identical-fingerprint, prefix-extending turn was not "
                "offered its chat's own warm slot");
    }

    // A fingerprint mismatch (e.g. the chat's model or project index
    // changed between turns) forces a fresh, uncached session.
    {
        masterai::PromptSessionManager sessions(4U, 300U);
        const std::string turn_one = "system+turn-one";
        sessions.record("chat-a", fingerprint, turn_one, std::nullopt);
        auto changed = fingerprint;
        changed.project_index_generation = 2U;
        const auto decision = sessions.try_reuse(
            "chat-a", changed, turn_one + "+turn-two");
        require(!decision.reuse,
                "a fingerprint mismatch (project reindex) was still offered "
                "reuse");
    }

    // A non-prefix edit -- simulating an edited/resubmitted earlier turn --
    // also forces a fresh session even though the fingerprint still
    // matches.
    {
        masterai::PromptSessionManager sessions(4U, 300U);
        sessions.record("chat-a", fingerprint, "system+original-turn-one",
                        std::nullopt);
        const auto decision = sessions.try_reuse(
            "chat-a", fingerprint, "system+edited-turn-one+turn-two");
        require(!decision.reuse,
                "an edited earlier turn (non-prefix) was still offered "
                "reuse");
    }

    // Slot-pool exhaustion evicts the least-recently-used chat rather than
    // blocking or crashing, and the evicted chat's own next lookup then
    // correctly misses.
    {
        masterai::PromptSessionManager sessions(2U, 300U);
        sessions.record("chat-1", fingerprint, "p1", std::nullopt);
        sessions.record("chat-2", fingerprint, "p2", std::nullopt);
        sessions.record("chat-3", fingerprint, "p3", std::nullopt);
        require(sessions.active_sessions() == 2U,
                "the session pool grew past its configured slot count");
        require(!sessions.try_reuse("chat-1", fingerprint, "p1x").reuse,
                "the least-recently-used chat was not evicted when the slot "
                "pool filled up");
        require(sessions.try_reuse("chat-3", fingerprint, "p3x").reuse,
                "the most recently recorded chat was evicted before the "
                "least-recently-used one");
    }

    // A cancelled/failed turn must never be recorded as reusable state: the
    // caller releases the chat's entry instead of calling record().
    {
        masterai::PromptSessionManager sessions(4U, 300U);
        sessions.record("chat-a", fingerprint, "system+turn-one", std::nullopt);
        sessions.release("chat-a");
        require(!sessions.try_reuse("chat-a", fingerprint, "system+turn-one"
                                                            "+turn-two")
                     .reuse,
                "a released (cancelled-turn) session was still offered "
                "reuse");
    }

    // reset() drops every entry at once (runner unload/restart -- every
    // slot's KV cache is gone with it).
    {
        masterai::PromptSessionManager sessions(4U, 300U);
        sessions.record("chat-a", fingerprint, "p", std::nullopt);
        sessions.reset();
        require(sessions.active_sessions() == 0U,
                "reset() did not clear every tracked session");
    }

    // The launch spec exposes as many llama.cpp server slots as
    // PromptSessionManager's own pool, so slot ids it hands out are always
    // addressable via id_slot.
    {
        TemporaryDirectory temporary;
        const auto model_directory = temporary.path() / "model";
        const auto model_file = model_directory / "model.gguf";
        write_text(model_file, "GGUF-phase18-fixture");
        masterai::ModelRecord model;
        model.directory = model_directory;
        model.state = masterai::ModelState::ready;
        model.manifest.id = "phase18-fixture";
        model.manifest.model_file = "model.gguf";
        model.manifest.model_size_bytes =
            std::filesystem::file_size(model_file);
        model.manifest.model_sha256 = masterai::sha256_file_hex(model_file);
        masterai::LlamaCppAdapter adapter(test_executable());
        const auto spec = adapter.build_launch_spec(model, 4096U, 8080U, 4U);
        const auto found =
            std::find(spec.arguments.begin(), spec.arguments.end(), "--parallel");
        require(found != spec.arguments.end() &&
                    std::next(found) != spec.arguments.end() &&
                    *std::next(found) == "4",
                "the launch spec did not expose the configured slot count "
                "via --parallel");
    }
}

void test_phase_nineteen_calibration() {
    // System-utilization sampling never throws and returns a plausible
    // percentage regardless of host load.
    const auto utilization = masterai::probe_system_utilization(20U);
    require(utilization.cpu_percent >= 0.0 && utilization.cpu_percent <= 100.0,
            "system CPU utilization sample was out of range");

    TemporaryDirectory temporary;
    const auto model_directory = temporary.path() / "model";
    const auto model_file = model_directory / "model.gguf";
    write_text(model_file, "GGUF-phase19-fixture");
    masterai::ModelRecord model;
    model.directory = model_directory;
    model.state = masterai::ModelState::ready;
    model.manifest.id = "phase19-fixture";
    model.manifest.model_file = "model.gguf";
    model.manifest.model_size_bytes = std::filesystem::file_size(model_file);
    model.manifest.model_sha256 = masterai::sha256_file_hex(model_file);

    const auto backend = fake_llama_executable();
    require(std::filesystem::is_regular_file(backend),
            "fake llama runner fixture is unavailable");
    masterai::HardwareInfo hardware;
    hardware.platform = "test-platform";
    hardware.architecture = "x86_64";
    hardware.logical_cpu_count = 4U;

    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::TuningProfileStore store(records);

    masterai::RunnerSupervisor supervisor(backend, temporary.path() / "runtime");
    masterai::CalibrationService calibration(supervisor, store, hardware,
                                             "backend-hash-one", "build-one");
    std::atomic_bool cancellation{false};
    const auto profile = calibration.calibrate(model, "balanced", 18090U,
                                               cancellation);
    require(profile.profile_name == "balanced" &&
                profile.model_sha256 == model.manifest.model_sha256 &&
                profile.backend_hash == "backend-hash-one" &&
                profile.build_id == "build-one",
            "calibration did not record its own host/model/backend/build "
            "identity");
    require(supervisor.metrics().state == masterai::RunnerState::unloaded,
            "calibration left the runner loaded instead of unloading it");

    // Re-resolving the same identity returns the just-persisted profile
    // rather than a fabricated safe default.
    const auto resolved =
        calibration.resolve(model.manifest.model_sha256, "balanced");
    require(resolved.cold_load_microseconds == profile.cold_load_microseconds &&
                resolved.calibrated_at_epoch_seconds ==
                    profile.calibrated_at_epoch_seconds,
            "resolve() did not return the persisted calibration for a "
            "matching identity");

    // A restart-surviving TuningProfileStore over the same RecordStore still
    // finds the persisted profile.
    masterai::TuningProfileStore restored_store(records);
    require(restored_store
                .find(profile.host_hash, model.manifest.model_sha256,
                     "backend-hash-one", "build-one")
                .has_value(),
            "persisted tuning profile was not restored across a simulated "
            "restart");

    // A backend change invalidates the profile: resolve() falls back to a
    // safe default instead of returning stale evidence.
    masterai::CalibrationService other_backend(supervisor, store, hardware,
                                               "backend-hash-two", "build-one");
    const auto fallback =
        other_backend.resolve(model.manifest.model_sha256, "minimal");
    require(fallback.backend_hash.empty() && fallback.profile_name == "minimal" &&
                fallback.cold_load_microseconds == 0U,
            "a backend-hash change did not invalidate the stale profile and "
            "fall back to a safe default");

    // safe_default_profile never fabricates a measurement and rejects an
    // unknown profile name rather than silently defaulting.
    const auto minimal_default = masterai::safe_default_profile("minimal");
    require(minimal_default.recommended_context_length == 2048U &&
                minimal_default.cold_load_microseconds == 0U,
            "minimal safe-default profile did not match its documented "
            "starting point");
    bool unknown_rejected = false;
    try {
        static_cast<void>(masterai::safe_default_profile("nonexistent"));
    } catch (const std::exception&) {
        unknown_rejected = true;
    }
    require(unknown_rejected, "an unknown calibration profile name was accepted");
}

void test_phase_twenty_advanced_optimizations_disabled() {
    masterai::AdvancedOptimizationRegistry registry;
    const auto features = registry.features();
    require(!features.empty(), "the advanced-optimization registry was empty");
    for (const auto& feature : features) {
        require(!feature.enabled,
                "an advanced-optimization feature defaulted to enabled");
        require(!feature.evidence.has_value(),
                "an advanced-optimization feature had evidence before any "
                "was recorded");
    }
    require(!registry.has_evidence("speculative_decoding"),
            "has_evidence() reported evidence before any was recorded");

    masterai::AdvancedOptimizationEvidence evidence;
    evidence.feature_name = "speculative_decoding";
    evidence.baseline_description = "single-model decoding";
    evidence.changed_setting = "draft-model speculative decoding enabled";
    evidence.host_hash = std::string(64U, 'a');
    evidence.model_sha256 = std::string(64U, 'b');
    evidence.backend_hash = "backend-hash";
    registry.record_evidence("speculative_decoding", evidence);
    require(registry.has_evidence("speculative_decoding"),
            "recorded evidence was not retained");
    const auto after = registry.features();
    const auto found = std::find_if(
        after.begin(), after.end(), [](const auto& feature) {
            return feature.name == "speculative_decoding";
        });
    require(found != after.end() && !found->enabled,
            "recording evidence enabled a Phase 20 feature; admission must "
            "remain a separate, later decision");

    bool unknown_rejected = false;
    try {
        registry.record_evidence("nonexistent_feature", evidence);
    } catch (const std::exception&) {
        unknown_rejected = true;
    }
    require(unknown_rejected,
            "recording evidence for an unknown feature name was accepted");
}

}  // namespace

int main() {
    try {
        const auto run = [](const char* name, void (*test)()) {
            std::clog << "Running " << name << "...\n";
            test();
        };
        run("json", test_json);
        run("sessions", test_sessions);
        run("configuration", test_configuration_and_intranet_policy);
        run("record recovery and identity", test_record_recovery_users_and_persistent_sessions);
        run("OS secret store", test_os_secret_store);
        run("OS identity boundary", test_os_identity_boundary);
        run("model suitability", test_suitability);
        run("unverified model load block", test_unverified_model_cannot_launch);
        run("verified model promotion", test_verified_model_promotion_and_load_recheck);
        run("path containment", test_path_containment);
        run("runner supervisor", test_phase_four_runner_supervisor);
        run("chat and projects", test_phase_five_chat_and_projects);
        run("chat legacy migration", test_phase_five_chat_legacy_migration);
        run("download policy", test_phase_six_download_policy);
        run("concurrent downloads", test_phase_sixteen_concurrent_downloads);
        run("benchmarks", test_phase_seven_benchmarks);
        run("MCP inbound", test_phase_eight_mcp_inbound);
        run("MCP outbound policy", test_phase_nine_mcp_policy);
        run("IDE integrations", test_phase_ten_ide_integrations);
        run("operations hardening", test_phase_eleven_operations);
        run("performance probe", test_phase_twelve_performance_probe);
        run("query measurement", test_phase_thirteen_query_measurement);
        run("bounded memory", test_phase_fourteen_bounded_memory);
        run("disk-backed indexing", test_phase_fifteen_disk_backed_indexing);
        run("live project watcher", test_phase_fifteen_project_watcher);
        run("deadline-bound retrieval", test_phase_sixteen_deadline_bound_retrieval);
        run("security-partitioned cache",
            test_phase_seventeen_security_partitioned_cache);
        run("prompt-prefix session reuse",
            test_phase_eighteen_prompt_session_reuse);
        run("adaptive calibration", test_phase_nineteen_calibration);
        run("advanced optimizations gated",
            test_phase_twenty_advanced_optimizations_disabled);
        std::cout << "MasterAI core tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "MasterAI core test failure: " << exception.what() << '\n';
        return 1;
    }
}
