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
    // omitted rather than truncating a kept chunk's text. Phase 24: each
    // candidate's text now lives once in a shared arena buffer, addressed by
    // a ChunkReference, and is only materialized for candidates the budgeter
    // actually admits.
    std::vector<masterai::RetrievalCandidate> ranked;
    std::string arena;
    for (unsigned int index = 0U; index < 5U; ++index) {
        masterai::IndexChunk chunk;
        chunk.id = "chunk-" + std::to_string(index);
        chunk.relative_path = "src/big.cpp";
        chunk.offset = index * 4096ULL;
        chunk.language = "cpp";
        chunk.digest = "digest-" + std::to_string(index);
        masterai::ChunkReference reference;
        reference.segment_key = "test-arena";
        reference.offset = arena.size();
        reference.length = 3000U;
        arena += std::string(3000U, 'x');
        masterai::RetrievalDisclosureEntry disclosure;
        disclosure.source = "exact_text";
        disclosure.relative_path = chunk.relative_path;
        disclosure.offset = chunk.offset;
        disclosure.index_generation = 1U;
        disclosure.score = 5.0 - static_cast<double>(index);
        disclosure.reason = "synthetic";
        ranked.push_back({chunk, reference, disclosure});
    }
    masterai::SharedBuffer arena_buffer =
        masterai::SharedBuffer::copy_from(arena.data(), arena.size());
    masterai::BufferView arena_view(arena_buffer, 0U, arena.size());
    const auto budgeted = masterai::ContextBudgeter::apply(
        ranked, arena_view, "exact_text", false, "", 7000U, 6U, 20U);
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

// Phase 22: hierarchical categories, segmented (probationary/protected/
// pinned) eviction, and negative caching layered on Phase 17's CacheManager.
void test_phase_twentytwo_hierarchical_cache() {
    TemporaryDirectory temporary;
    const auto hardware = masterai::probe_hardware(temporary.path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware,
        512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);

    masterai::CacheKey base_key;
    base_key.user_id = "user-a";
    base_key.project_id = "project-a";
    base_key.version_tag = "v1";
    base_key.index_generation = 1U;

    // A pinned entry survives a capacity-driven flood that would otherwise
    // evict it, and returns to normal eviction eligibility once unpinned.
    {
        masterai::CachePolicy small_policy;
        small_policy.maximum_bytes_per_category = 300U;
        masterai::CacheManager cache(temporary.path() / "pin-cache", memory,
                                     small_policy);
        auto pinned_key = base_key;
        pinned_key.canonical_identity = "pin-fixture";
        cache.put(masterai::CacheCategory::source_chunk, pinned_key,
                 std::string(100U, 'p'));
        cache.pin(masterai::CacheCategory::source_chunk, pinned_key);
        for (unsigned int index = 0U; index < 6U; ++index) {
            auto flood_key = base_key;
            flood_key.canonical_identity = "flood-" + std::to_string(index);
            cache.put(masterai::CacheCategory::source_chunk, flood_key,
                     std::string(100U, 'f'));
        }
        require(cache.get(masterai::CacheCategory::source_chunk, pinned_key)
                    .has_value(),
                "a pinned entry was evicted by a capacity-driven flood");
        cache.unpin(masterai::CacheCategory::source_chunk, pinned_key);
        const auto status_after_unpin = cache.status();
        const auto found = status_after_unpin.categories.find(
            masterai::CacheCategory::source_chunk);
        require(found != status_after_unpin.categories.end() &&
                    found->second.pinned_entries == 0U,
                "unpin() did not remove the entry from the pinned tier");
    }

    // Segmented eviction: an entry accessed twice is promoted to the
    // protected tier and survives a subsequent flood of never-reused
    // probationary entries that would otherwise fill the category.
    {
        masterai::CachePolicy small_policy;
        small_policy.maximum_bytes_per_category = 500U;
        masterai::CacheManager cache(temporary.path() / "protect-cache",
                                     memory, small_policy);
        auto hot_key = base_key;
        hot_key.canonical_identity = "hot-fixture";
        cache.put(masterai::CacheCategory::symbol, hot_key,
                 std::string(100U, 'h'));
        require(cache.get(masterai::CacheCategory::symbol, hot_key)
                    .has_value(),
                "setup lost the hot entry before it could be promoted");
        const auto status_after_promotion = cache.status();
        const auto promoted = status_after_promotion.categories.find(
            masterai::CacheCategory::symbol);
        require(promoted != status_after_promotion.categories.end() &&
                    promoted->second.protected_entries == 1U,
                "a twice-accessed entry was not promoted to the protected tier");
        for (unsigned int index = 0U; index < 8U; ++index) {
            auto flood_key = base_key;
            flood_key.canonical_identity = "scan-" + std::to_string(index);
            cache.put(masterai::CacheCategory::symbol, flood_key,
                     std::string(100U, 's'));
        }
        require(cache.get(masterai::CacheCategory::symbol, hot_key).has_value(),
                "a one-time scan evicted the protected working set");
    }

    // Negative caching: a marker records "known absent" until it expires,
    // and never masks a normal positive entry written for the same key.
    {
        masterai::CacheManager cache(temporary.path() / "negative-cache",
                                     memory, masterai::CachePolicy{});
        auto miss_key = base_key;
        miss_key.canonical_identity = "negative-fixture";
        require(!cache.is_negative(masterai::CacheCategory::mcp_resource,
                                   miss_key),
                "a key with nothing ever recorded reported as negatively cached");
        cache.put_negative(masterai::CacheCategory::mcp_resource, miss_key,
                           std::chrono::seconds(3600));
        require(cache.is_negative(masterai::CacheCategory::mcp_resource,
                                  miss_key),
                "put_negative() did not make is_negative() report true");
        require(!cache.get(masterai::CacheCategory::mcp_resource, miss_key)
                     .has_value(),
                "a negative marker was served through get() as a positive hit");
        cache.put_negative(masterai::CacheCategory::mcp_resource, miss_key,
                           std::chrono::seconds(0));
        require(!cache.is_negative(masterai::CacheCategory::mcp_resource,
                                   miss_key),
                "an expired negative marker was still reported as negative");
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

// Phase 30A: hardware.acceleratorPolicy round-trips through save/load,
// rejects an unknown value, forces CalibrationService::resolve()/calibrate()
// to recommend zero GPU layers under cpu_only even with a capable GPU
// present, rejects a stale persisted profile that recommends GPU offload,
// and makes build_launch_spec() both refuse a nonzero gpu_layers request and
// populate the GPU-hiding runner environment.
void test_phase_thirty_a_cpu_only_accelerator_policy() {
    TemporaryDirectory temporary;
    const auto settings = temporary.path() / "settings.json";
    auto configuration = masterai::ConfigurationManager::safe_defaults();
    configuration.runtime_root = temporary.path() / "runtime";
    configuration.models_root = temporary.path() / "models";
    configuration.accelerator_policy = "cpu_only";
    masterai::ConfigurationManager::save_atomic(configuration, settings);
    const auto loaded = masterai::ConfigurationManager::load(settings);
    require(loaded.accelerator_policy == "cpu_only",
            "acceleratorPolicy did not round-trip through save/load");

    auto bad = configuration;
    bad.accelerator_policy = "not_a_real_policy";
    bool policy_rejected = false;
    try {
        masterai::ConfigurationManager::validate(bad);
    } catch (const std::exception&) {
        policy_rejected = true;
    }
    require(policy_rejected, "an unknown accelerator policy value was accepted");

    // A capable GPU host, but resolve()/calibrate() must still recommend
    // zero GPU layers end to end when the service was constructed cpu_only.
    masterai::HardwareInfo cuda_host;
    cuda_host.platform = "test-platform";
    cuda_host.architecture = "x86_64";
    cuda_host.logical_cpu_count = 8U;
    cuda_host.gpu_backends = {"cuda"};
    cuda_host.gpu_memory_mib = 24U * 1024U;

    const auto model_directory = temporary.path() / "model";
    const auto model_file = model_directory / "model.gguf";
    write_text(model_file, "GGUF-phase30a-fixture");
    masterai::ModelRecord model;
    model.directory = model_directory;
    model.state = masterai::ModelState::ready;
    model.manifest.id = "phase30a-fixture";
    model.manifest.model_file = "model.gguf";
    model.manifest.model_size_bytes = std::filesystem::file_size(model_file);
    model.manifest.model_sha256 = masterai::sha256_file_hex(model_file);

    const auto backend = fake_llama_executable();
    require(std::filesystem::is_regular_file(backend),
            "fake llama runner fixture is unavailable");
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::TuningProfileStore store(records);
    masterai::RunnerSupervisor supervisor(backend, temporary.path() / "runtime");

    bool invalid_construction_rejected = false;
    try {
        masterai::CalibrationService bad_service(
            supervisor, store, cuda_host, "backend-hash-cpu-only", "build-one",
            "not_a_real_policy");
    } catch (const std::exception&) {
        invalid_construction_rejected = true;
    }
    require(invalid_construction_rejected,
            "CalibrationService accepted an unknown accelerator policy");

    masterai::CalibrationService cpu_only_calibration(
        supervisor, store, cuda_host, "backend-hash-cpu-only", "build-one",
        "cpu_only");
    const auto resolved = cpu_only_calibration.resolve(
        model.manifest.model_sha256, "balanced", nullptr, 0U,
        model.manifest.model_size_bytes, "");
    require(resolved.recommended_gpu_layers == 0U,
            "cpu_only resolve() recommended nonzero GPU layers on a capable "
            "GPU host");

    std::atomic_bool cancellation{false};
    const auto calibrated = cpu_only_calibration.calibrate(
        model, "balanced", 18190U, cancellation);
    require(calibrated.recommended_gpu_layers == 0U,
            "cpu_only calibrate() measured and recommended nonzero GPU "
            "layers on a capable GPU host");

    // A profile persisted under an auto/GPU-permitting policy that
    // recommends GPU offload must be rejected, not silently zeroed, when
    // resolved again under cpu_only.
    masterai::CalibrationService auto_calibration(
        supervisor, store, cuda_host, "backend-hash-auto", "build-one",
        "auto");
    const auto auto_calibrated = auto_calibration.calibrate(
        model, "balanced", 18191U, cancellation);
    require(auto_calibrated.recommended_gpu_layers > 0U,
            "auto-policy calibration on a capable GPU host did not "
            "recommend GPU offload");
    masterai::CalibrationService cpu_only_same_backend(
        supervisor, store, cuda_host, "backend-hash-auto", "build-one",
        "cpu_only");
    bool stale_gpu_profile_rejected = false;
    try {
        static_cast<void>(cpu_only_same_backend.resolve(
            model.manifest.model_sha256, "balanced"));
    } catch (const std::exception&) {
        stale_gpu_profile_rejected = true;
    }
    require(stale_gpu_profile_rejected,
            "resolve() silently returned a persisted GPU-offload profile "
            "under cpu_only instead of rejecting it");

    // build_launch_spec(): cpu_only with a nonzero gpu_layers request throws
    // (belt-and-suspenders fail-closed), and a zero-gpu_layers cpu_only
    // launch never emits --n-gpu-layers and always carries the GPU-hiding
    // environment overrides.
    masterai::LlamaCppAdapter adapter(backend);
    masterai::LaunchTuning gpu_tuning;
    gpu_tuning.gpu_layers = masterai::kGpuLayersOffloadAll;
    bool nonzero_gpu_rejected = false;
    try {
        static_cast<void>(adapter.build_launch_spec(
            model, 2048U, 18192U, 1U, gpu_tuning, "cpu_only"));
    } catch (const std::exception&) {
        nonzero_gpu_rejected = true;
    }
    require(nonzero_gpu_rejected,
            "build_launch_spec() launched with GPU layers requested under "
            "cpu_only instead of refusing");

    masterai::LaunchTuning cpu_tuning;
    const auto cpu_spec = adapter.build_launch_spec(model, 2048U, 18193U, 1U,
                                                     cpu_tuning, "cpu_only");
    require(std::find(cpu_spec.arguments.begin(), cpu_spec.arguments.end(),
                      "--n-gpu-layers") == cpu_spec.arguments.end(),
            "cpu_only launch spec emitted --n-gpu-layers");
    require(cpu_spec.environment.count("CUDA_VISIBLE_DEVICES") == 1U &&
                cpu_spec.environment.at("CUDA_VISIBLE_DEVICES") == "-1",
            "cpu_only launch spec did not hide CUDA devices from the runner "
            "process environment");
    require(cpu_spec.environment.count("HIP_VISIBLE_DEVICES") == 1U,
            "cpu_only launch spec did not hide HIP devices from the runner "
            "process environment");

    // The default ("auto") policy reproduces pre-Phase-30A behavior exactly:
    // no environment overrides are added.
    const auto auto_spec = adapter.build_launch_spec(model, 2048U, 18194U, 1U,
                                                      cpu_tuning);
    require(auto_spec.environment.empty(),
            "the default accelerator policy added runner environment "
            "overrides that did not exist before Phase 30A");
}

// Phase 30A deliverable 2: exercises the same MemoryBudgetManager primitive
// admit_runner_weights() (server.cpp) is built from -- a
// MemoryCategory::runner_weights reservation sized from model weight bytes
// plus the fixed compute-buffer/KV heuristics -- confirming a model whose
// weights alone would blow the configured ceiling is rejected with a
// concrete diagnostic before any runner process would ever be started, and
// that a model comfortably inside the ceiling is admitted and later
// releasable. Also covers assess_model()'s RAM-only suitability triage,
// which admit_runner_weights() consults first as a fast-fail check.
void test_phase_thirty_a_runner_weights_admission() {
    auto hardware = masterai::probe_hardware(std::filesystem::current_path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware,
        512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);

    masterai::MemoryEstimate oversized;
    oversized.weights_bytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    oversized.runtime_buffer_bytes = 256ULL * 1024ULL * 1024ULL;
    oversized.kv_bytes_per_sequence = 128ULL * 1024ULL * 1024ULL;
    oversized.sequences = 1U;
    oversized.safety_margin_bytes = 32ULL * 1024ULL * 1024ULL;
    const auto rejected = memory.reserve(
        masterai::MemoryCategory::runner_weights, oversized, true);
    require(!rejected.admitted && !rejected.diagnostic.empty(),
            "an oversized model's weights were admitted past the configured "
            "ceiling");

    masterai::MemoryEstimate small;
    small.weights_bytes = 64ULL * 1024ULL * 1024ULL;
    small.runtime_buffer_bytes = 256ULL * 1024ULL * 1024ULL;
    small.kv_bytes_per_sequence = 128ULL * 1024ULL * 1024ULL;
    small.sequences = 1U;
    small.safety_margin_bytes = 32ULL * 1024ULL * 1024ULL;
    const auto admitted = memory.reserve(
        masterai::MemoryCategory::runner_weights, small, true);
    require(admitted.admitted && !admitted.lease_id.empty(),
            "a comfortably-sized model was rejected before runner start");
    memory.release(admitted.lease_id);
    require(memory.status().reserved_bytes == 0U,
            "releasing the runner_weights lease on unload did not restore "
            "headroom for the next model");

    masterai::ModelManifest manifest;
    manifest.minimum_ram_mib = hardware.total_ram_mib * 4U;
    manifest.recommended_ram_mib = hardware.total_ram_mib * 4U;
    const auto suitability =
        masterai::assess_model(manifest, hardware, 0U);
    require(suitability.rating == masterai::Suitability::unsupported,
            "assess_model() did not flag a model far exceeding host RAM as "
            "unsupported");
}

// Phase 30A deliverable 4: MemorySweeper is the one background thread that
// turns Phase 26's WarmModelTracker::apply_idle_timeout()/
// RunnerSupervisor::apply_idle_timeout() and Phase 14's
// MemoryBudgetManager::sample() pressure actions into something that
// actually runs unattended (see masterai.hpp's MemorySweeper comment).
// Confirms a loaded runner left idle past idle_unload_seconds is unloaded
// automatically, the caller's on_idle_unload callback fires exactly once so
// it can release its own runner_weights lease, and prompt session state is
// reset alongside it.
void test_phase_thirty_a_memory_sweeper_idle_unload() {
    TemporaryDirectory temporary;
    const auto model_directory = temporary.path() / "model";
    const auto model_file = model_directory / "model.gguf";
    write_text(model_file, "GGUF-sweeper-fixture");
    masterai::ModelRecord model;
    model.directory = model_directory;
    model.state = masterai::ModelState::ready;
    model.manifest.id = "sweeper-fixture";
    model.manifest.model_file = "model.gguf";
    model.manifest.model_size_bytes = std::filesystem::file_size(model_file);
    model.manifest.model_sha256 = masterai::sha256_file_hex(model_file);

    const auto backend = fake_llama_executable();
    require(std::filesystem::is_regular_file(backend),
            "fake llama runner fixture is unavailable");
    masterai::RunnerSupervisor supervisor(backend, temporary.path() / "runtime");
    supervisor.load(model, 4096U, 18195U, 5U);
    require(supervisor.metrics().state == masterai::RunnerState::ready,
            "sweeper fixture runner did not become ready");

    auto hardware = masterai::probe_hardware(std::filesystem::current_path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware,
        512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    // keep_idle_model=false on the minimal profile (see policy_for()) is
    // exactly the "no persistent idle runner by default" behavior this test
    // exercises -- the sweep unloads on idle timeout unconditionally rather
    // than only once real memory pressure appears.
    require(!policy.keep_idle_model,
            "test assumes the minimal profile does not keep an idle model "
            "warm by default");
    masterai::MemoryBudgetManager memory(policy, hardware);
    masterai::PromptSessionManager prompt_sessions(4U, 300U);

    std::atomic<unsigned int> idle_unload_calls{0U};
    {
        // idle_unload_seconds=0: the very first sweep tick after the load
        // above already satisfies "idle for at least 0 seconds".
        masterai::MemorySweeper sweeper(
            memory, &supervisor, nullptr, &prompt_sessions, 0U,
            [&idle_unload_calls]() { ++idle_unload_calls; });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (supervisor.metrics().state != masterai::RunnerState::unloaded &&
              std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    require(supervisor.metrics().state == masterai::RunnerState::unloaded,
            "MemorySweeper did not unload a runner idle past "
            "idle_unload_seconds");
    require(idle_unload_calls.load() == 1U,
            "MemorySweeper's on_idle_unload callback did not fire exactly "
            "once");
    require(prompt_sessions.active_sessions() == 0U,
            "MemorySweeper's idle unload did not reset prompt session state");
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

// Phase 21: the read coalescer is pure/deterministic, so this drives it
// directly rather than through a real backend -- two adjacent 4KB ranges on
// the same file must become a single merged physical read, while a range on
// a different file must never be folded into it.
void test_phase_twentyone_coalescing_merges_adjacent() {
    const std::filesystem::path same_file = "manifest.idx";
    const std::filesystem::path other_file = "other.idx";
    std::vector<masterai::CoalescedReadRequest> requests;
    requests.push_back({same_file, 0U, 4096U});
    requests.push_back({same_file, 4096U, 4096U});
    requests.push_back({other_file, 0U, 128U});

    const auto plans = masterai::coalesce_read_requests(requests);
    require(plans.size() == 2U,
            "adjacent same-file requests were not merged into one plan");

    const auto merged = std::find_if(
        plans.begin(), plans.end(),
        [&](const masterai::CoalescedReadPlan& plan) { return plan.path == same_file; });
    require(merged != plans.end(), "merged plan for the shared file was missing");
    require(merged->offset == 0U && merged->length == 8192U,
            "merged plan did not cover the full adjacent byte range");
    require(merged->members.size() == 2U,
            "merged plan lost track of one of its original requests");

    const auto separate = std::find_if(
        plans.begin(), plans.end(),
        [&](const masterai::CoalescedReadPlan& plan) { return plan.path == other_file; });
    require(separate != plans.end() && separate->members.size() == 1U,
            "a request on a different file was incorrectly folded into another file's plan");
}

// Phase 21: a request cancelled before it is ever dispatched must release
// its buffer and report cancelled=true with no bytes -- never publish
// whatever was in the (never-written) buffer.
void test_phase_twentyone_cancellation_releases_buffer() {
    TemporaryDirectory temp;
    const auto path = temp.path() / "cancel_target.bin";
    write_text(path, std::string(4096U, 'x'));

    const auto profile = masterai::probe_storage_latency(temp.path(), "fixed");
    auto reader = masterai::make_async_file_reader(profile);
    require(reader != nullptr, "async reader failed to construct for cancellation test");

    masterai::AsyncReadCancellationToken token;
    token.cancel();  // cancel before the read is even issued
    const auto result = reader->read_range(path, 0U, 4096U, token);
    require(result.cancelled, "a pre-cancelled read did not report cancelled");
    require(!result.succeeded, "a pre-cancelled read reported success");
    require(result.buffer.empty(),
            "a cancelled read published a non-empty buffer instead of releasing it");
}

// Phase 21: a profile shaped like a spinning HDD (high measured latency, or
// explicitly non-fan-out) must resolve to the serialized (depth 1) queue
// policy, and non-local media (network/removable) must stay serialized
// regardless of measured latency, matching the "denied-by-default fan-out"
// requirement for active model-weight reads.
void test_phase_twentyone_hdd_profile_stays_sequential() {
    masterai::StorageLatencyProfile hdd_profile;
    hdd_profile.storage_class = "fixed";
    hdd_profile.measured_read_latency_us = 6000.0;  // spinning-disk-shaped
    hdd_profile.sequential = true;
    require(masterai::adaptive_queue_depth(hdd_profile) == 1U,
            "an HDD-shaped storage profile did not stay sequential-only");

    masterai::StorageLatencyProfile network_profile;
    network_profile.storage_class = "network";
    network_profile.measured_read_latency_us = 50.0;  // fast, but non-local
    network_profile.sequential = false;
    require(masterai::adaptive_queue_depth(network_profile) == 1U,
            "a network storage profile allowed fan-out despite low latency");

    masterai::StorageLatencyProfile nvme_profile;
    nvme_profile.storage_class = "fixed";
    nvme_profile.measured_read_latency_us = 80.0;  // NVMe-shaped
    nvme_profile.sequential = false;
    require(masterai::adaptive_queue_depth(nvme_profile) > 1U,
            "a fast local NVMe-shaped profile was incorrectly denied fan-out");
}

// Phase 21: read_file_bytes() must transparently fall back to a direct
// blocking read -- byte-identical to the pre-Phase-21 path -- whenever the
// async reader is unavailable (e.g. async init failed and the caller was
// handed nullptr), which is the exact fallback contract models.cpp and
// indexing.cpp depend on.
void test_phase_twentyone_fallback_to_blocking_path() {
    TemporaryDirectory temp;
    const auto path = temp.path() / "fallback_target.txt";
    const std::string expected = "phase twenty-one fallback content";
    write_text(path, expected);

    const std::string content =
        masterai::read_file_bytes(nullptr, path, 0U, expected.size());
    require(content == expected,
            "read_file_bytes() with a null reader did not match a direct blocking read");

    // Also exercise a real (working) reader end-to-end to confirm the async
    // path itself returns the same bytes as the fallback when it succeeds.
    const auto profile = masterai::probe_storage_latency(temp.path(), "fixed");
    auto reader = masterai::make_async_file_reader(profile);
    require(reader != nullptr, "async reader failed to construct for the fallback comparison");
    const std::string async_content =
        masterai::read_file_bytes(reader.get(), path, 0U, expected.size());
    require(async_content == expected,
            "the async read path returned different bytes than the blocking fallback");
}

// Phase 30: SharedBuffer/BufferView correctness -- a view constructed from
// a buffer must read exactly the bytes at [offset, offset+length), and the
// underlying storage must stay alive (readable) through a BufferView even
// after every SharedBuffer that named it directly has gone out of scope,
// which is the whole point of reference-counted immutable sharing.
void test_phase_thirty_shared_buffer_view_and_refcount() {
    std::vector<std::uint8_t> bytes = {'h', 'e', 'l', 'l', 'o', ',', ' ',
                                       'w', 'o', 'r', 'l', 'd'};
    masterai::BufferView view;
    {
        // The owning SharedBuffer is deliberately scoped to die before the
        // view is used below -- BufferView must keep the storage alive via
        // its own internal SharedBuffer copy, not the caller's variable.
        masterai::SharedBuffer owner(bytes);
        require(owner.use_count() == 1, "a freshly constructed SharedBuffer had unexpected refcount");
        view = masterai::BufferView(owner, 7U, 5U, "utf-8");
        require(owner.use_count() == 2,
                "constructing a BufferView did not retain the owning SharedBuffer");
    }
    require(view.size() == 5U, "BufferView length did not match the requested window");
    require(view.encoding() == "utf-8", "BufferView did not retain its encoding metadata");
    require(view.to_string() == "world",
            "BufferView read the wrong bytes after its original owner variable went out of scope");

    // copy_from() must produce an independent buffer with identical bytes.
    const masterai::SharedBuffer copied =
        masterai::SharedBuffer::copy_from(bytes.data(), bytes.size());
    require(copied.size() == bytes.size(), "SharedBuffer::copy_from produced the wrong size");
    require(std::memcmp(copied.data(), bytes.data(), bytes.size()) == 0,
            "SharedBuffer::copy_from did not copy the exact bytes");

    // An out-of-range window must clamp to empty rather than read past the
    // buffer.
    const masterai::BufferView overrun(copied, 100U, 5U);
    require(overrun.empty(), "an out-of-range BufferView window was not clamped to empty");

    // ChunkReference::materialize() must round-trip through a BufferView.
    masterai::ChunkReference chunk;
    chunk.segment_key = "test-segment";
    chunk.offset = 0U;
    chunk.length = 5U;
    const masterai::BufferView segment(copied, 0U, copied.size());
    require(chunk.materialize(segment) == "hello",
            "ChunkReference::materialize did not return the referenced bytes");
}

// Phase 30: MappedBufferView must expose exactly the mapped byte range from
// a real file, using the platform mapping backend (Win32 CreateFileMappingW
// on this build target).
void test_phase_thirty_mapped_buffer_view_reads_file() {
    TemporaryDirectory temp;
    const auto path = temp.path() / "mapped_source.bin";
    const std::string content = "the quick brown fox jumps over the lazy dog";
    write_text(path, content);

    masterai::MappedBufferView whole(path);
    require(whole.size() == content.size(),
            "MappedBufferView with length=0 did not map the whole file");
    require(std::string(reinterpret_cast<const char*>(whole.data()), whole.size()) == content,
            "MappedBufferView did not expose the file's exact bytes");

    masterai::MappedBufferView window(path, 4U, 5U);
    require(window.size() == 5U, "MappedBufferView did not honor the requested length");
    require(std::string(reinterpret_cast<const char*>(window.data()), window.size()) == "quick",
            "MappedBufferView offset window returned the wrong bytes");

    bool rejected_out_of_range = false;
    try {
        masterai::MappedBufferView bad(path, content.size() + 10U, 1U);
    } catch (const std::exception&) {
        rejected_out_of_range = true;
    }
    require(rejected_out_of_range,
            "MappedBufferView accepted an offset beyond end of file instead of throwing");
}

// Phase 30: RequestArena must serve bump allocations up to its capacity,
// fail (nullptr / throw, per the documented policy) past exhaustion, and --
// in this debug build -- an ArenaHandle issued before reset()/destruction
// must become detectably invalid afterward instead of silently pointing at
// reused/freed memory.
void test_phase_thirty_request_arena_allocation_and_poison() {
    masterai::RequestArena arena(64U);
    void* first = arena.allocate(32U);
    require(first != nullptr, "RequestArena rejected an allocation within capacity");
    void* second = arena.allocate(32U);
    require(second != nullptr, "RequestArena rejected a second allocation within capacity");
    void* overflow = arena.allocate(1U);
    require(overflow == nullptr,
            "RequestArena returned non-null past its capacity instead of following the "
            "documented nullptr-on-exhaustion policy");

    bool threw = false;
    try {
        arena.allocate_or_throw(1U);
    } catch (const masterai::ArenaExhaustedError&) {
        threw = true;
    }
    require(threw, "RequestArena::allocate_or_throw did not throw on exhaustion");

    // Poison check: a handle issued before reset() must go invalid after.
    masterai::RequestArena poison_arena(64U);
    auto handle = masterai::allocate_handle<std::uint64_t>(poison_arena);
    require(handle.valid(), "a freshly issued ArenaHandle reported invalid");
    *handle = 42ULL;
    require(*handle == 42ULL, "ArenaHandle did not read back a value written through it");
    poison_arena.reset();
    require(!handle.valid(),
            "ArenaHandle stayed valid after its arena was reset() -- debug poison check failed "
            "to trigger on a stale generation");

    // Poison check: a handle must also go invalid once the arena that
    // issued it is destroyed, without ever dereferencing the (freed) arena.
    masterai::ArenaHandle<std::uint64_t> outlived_handle;
    {
        masterai::RequestArena short_lived_arena(64U);
        outlived_handle = masterai::allocate_handle<std::uint64_t>(short_lived_arena);
        require(outlived_handle.valid(), "a freshly issued ArenaHandle reported invalid");
    }
    require(!outlived_handle.valid(),
            "ArenaHandle stayed valid after its arena was destroyed -- debug poison check did "
            "not trigger");
}

// Phase 30: FixedSizePool basic acquire/release/shrink behavior -- pointers
// stay stable while live, bytes_reserved() reflects grown-block footprint
// (not just live objects), and shrink() releases only fully-unused blocks
// without disturbing objects still in use.
void test_phase_thirty_fixed_size_pool_alloc_release_shrink() {
    masterai::FixedSizePool<std::string> pool(4U);  // small block size to force multiple blocks
    require(pool.bytes_reserved() == 0U, "a freshly constructed pool reported nonzero reserved bytes");

    std::vector<std::string*> handles;
    for (int i = 0; i < 4; ++i) {
        handles.push_back(pool.acquire("item-" + std::to_string(i)));
    }
    require(pool.live_count() == 4U, "pool live_count did not match the number of acquired objects");
    require(pool.bytes_reserved() == 4U * sizeof(std::string),
            "pool bytes_reserved did not match one fully-used block's footprint");
    require(*handles[2] == "item-2", "pool acquire() did not construct the object with the given args");

    // Force growth into a second block.
    std::string* fifth = pool.acquire("item-4");
    require(pool.live_count() == 5U, "pool live_count did not grow after a fifth acquisition");
    require(pool.bytes_reserved() == 8U * sizeof(std::string),
            "pool bytes_reserved did not grow by a full block after exceeding block_capacity");

    // shrink() must not touch the first (still fully live) block, and must
    // do nothing while the second block still has a live object.
    require(pool.shrink() == 0U,
            "shrink() released a block that still had a live object in it");

    pool.release(fifth);
    require(pool.live_count() == 4U, "pool live_count did not drop after release()");
    require(pool.shrink() == 1U,
            "shrink() did not release the now fully-unused second block");
    require(pool.bytes_reserved() == 4U * sizeof(std::string),
            "pool bytes_reserved did not shrink after releasing an empty block");

    // The first block's objects must remain valid and unaffected by the
    // shrink() call above.
    require(*handles[0] == "item-0" && *handles[3] == "item-3",
            "shrink() disturbed objects still live in a block that was kept");

    for (auto* handle : handles) pool.release(handle);
    require(pool.live_count() == 0U, "pool live_count was nonzero after releasing every object");
    require(pool.shrink() == 1U, "shrink() did not release the final now-empty block");
    require(pool.bytes_reserved() == 0U,
            "pool bytes_reserved was nonzero after shrinking away every block");
}

// Phase 30: BudgetTrackedPool must actually call MemoryBudgetManager::reserve()
// on every real block growth and MemoryBudgetManager::release() on every real
// shrink -- proving FixedSizePool::on_reserved_bytes_changed's live-accounting
// hook does what its comment promises, independent of any real consumer.
void test_phase_thirty_fixed_size_pool_registers_with_budget_manager() {
    const auto hardware = masterai::probe_hardware(std::filesystem::current_path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware, 512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);

    // Careful: memory.status() returns MemoryStatus by value, so calling it
    // twice and comparing an iterator from one temporary's category_bytes
    // map against .end() of a *different* temporary's map is undefined
    // behavior (they are unrelated container instances) -- capture one
    // snapshot and query it once.
    const auto initial_status = memory.status();
    require(initial_status.category_bytes.find(
                masterai::MemoryCategory::retrieval_index_cache) ==
                initial_status.category_bytes.end(),
            "a freshly constructed MemoryBudgetManager already had a "
            "retrieval_index_cache entry");

    {
        masterai::BudgetTrackedPool<std::string> tracked(
            memory, masterai::MemoryCategory::retrieval_index_cache,
            /*interactive=*/true, /*block_capacity=*/2U);
        require(tracked.lease_id().empty(),
                "a freshly constructed BudgetTrackedPool already held a lease");

        // First acquire() forces the pool's first block growth (block
        // capacity 2, so this both allocates block 0 and fills its first
        // slot) -- the on_reserved_bytes_changed callback must fire and
        // reserve() must actually be called (not merely tracked internally).
        auto* alpha = tracked.pool().acquire("alpha");
        require(!tracked.lease_id().empty(),
                "pool growth did not register a MemoryBudgetManager lease");
        require(memory.status().category_bytes.at(
                    masterai::MemoryCategory::retrieval_index_cache) ==
                    tracked.pool().bytes_reserved(),
                "registered category bytes did not match the pool's real "
                "bytes_reserved() after growth");

        // A second acquire() recycles block 0's remaining free slot -- no
        // growth, so the lease must not churn (still the same footprint).
        auto* beta = tracked.pool().acquire("beta");
        require(memory.status().category_bytes.at(
                    masterai::MemoryCategory::retrieval_index_cache) ==
                    tracked.pool().bytes_reserved(),
                "an acquire() that only recycled a free slot changed the "
                "registered footprint");

        // A third acquire() forces a second block (block 0 is now full) --
        // growth again, so the registered footprint must grow with it.
        // gamma ends up the sole live object in block 1 -- block 1 keeps a
        // free slot, block 0 is completely full (alpha + beta).
        auto* gamma = tracked.pool().acquire("gamma");
        const auto grown_bytes = memory.status().category_bytes.at(
            masterai::MemoryCategory::retrieval_index_cache);
        require(grown_bytes == tracked.pool().bytes_reserved() && grown_bytes > 0U,
                "registered category bytes did not grow with a second block");

        // Releasing both of block 0's objects (alpha, beta) empties it
        // completely while leaving block 1 (gamma, still live) untouched --
        // shrink() must release exactly the one now-empty block, and the
        // registration must shrink by exactly that block's footprint.
        tracked.pool().release(alpha);
        tracked.pool().release(beta);
        require(tracked.pool().shrink() == 1U,
                "shrink() did not release the now-empty first block");
        require(memory.status().category_bytes.at(
                    masterai::MemoryCategory::retrieval_index_cache) ==
                    tracked.pool().bytes_reserved(),
                "registered category bytes did not shrink after shrink() "
                "released a block");
        require(*gamma == "gamma", "surviving pool object was disturbed by "
                                   "an unrelated block's shrink()");
    }
    // BudgetTrackedPool's destructor must release its outstanding lease even
    // though the pool still had one live object (gamma) when it went out of
    // scope -- an unreleased lease here would be a permanent, invisible
    // memory-budget leak.
    require(memory.status().category_bytes.at(
                masterai::MemoryCategory::retrieval_index_cache) == 0U,
            "BudgetTrackedPool destruction did not release its outstanding "
            "MemoryBudgetManager lease");
}

// Phase 30: json_escape_bytes() (the byte-vector escape used by
// server.cpp's zero-copy streaming token path) must produce byte-for-byte
// identical output to json_escape() (the std::string escape every ordinary
// one-shot JSON response still uses) across control characters, the
// characters JSON itself requires escaping, and ordinary text -- otherwise
// switching the streaming path to the byte-vector form would silently
// change what a client receives.
void test_phase_thirty_json_escape_bytes_matches_string_escape() {
    const std::vector<std::string> fixtures = {
        "",
        "plain ascii text",
        "line one\nline two\ttabbed",
        "quote \" and backslash \\ together",
        "control chars: \x01\x02\x1f end",
        "carriage\rreturn and \bbackspace and \fformfeed",
        std::string(500U, 'x') + "\n\"\\",
    };
    for (const auto& fixture : fixtures) {
        const auto string_escaped = masterai::server_internal::json_escape(fixture);
        const auto byte_escaped = masterai::server_internal::json_escape_bytes(fixture);
        const std::string byte_escaped_as_string(byte_escaped.begin(), byte_escaped.end());
        require(string_escaped == byte_escaped_as_string,
                "json_escape_bytes() diverged from json_escape() for a "
                "streaming-path fixture -- the two escape paths must stay "
                "byte-identical");
    }
}

// Phase 30: server.cpp's streaming token path moves json_escape_bytes()'s
// output directly into a SharedBuffer instead of copying it into a new
// std::string for JSON-envelope concatenation the way the pre-Phase-30 code
// did (see server.cpp's token-send call site and send_chunk_parts()).
// std::vector's move constructor is required by the standard to transfer
// ownership of the existing heap allocation rather than reallocate and
// copy, so if this really is a move (not a disguised copy), the
// SharedBuffer's data pointer must be the exact same address the vector
// held before the move -- a concrete, pointer-identity proof of "fewer
// copies" rather than an assertion about internal call counts.
void test_phase_thirty_streaming_escape_moves_into_shared_buffer_without_copy() {
    const std::string chunk =
        "line one\nline two\twith \"quotes\" and \\backslash\\ and control \x01 char";
    auto bytes = masterai::server_internal::json_escape_bytes(chunk);
    const auto* original_data_pointer = bytes.data();
    const auto original_size = bytes.size();
    require(original_size > 0U, "escape fixture produced no bytes");

    masterai::SharedBuffer buffer(std::move(bytes));
    require(buffer.data() == original_data_pointer && buffer.size() == original_size,
            "moving json_escape_bytes() output into a SharedBuffer copied "
            "the payload instead of taking ownership of its existing "
            "allocation -- the streaming path would no longer be zero-copy");

    masterai::BufferView view(buffer, 0U, buffer.size());
    require(view.to_string() == masterai::server_internal::json_escape(chunk),
            "BufferView materialized from the moved SharedBuffer did not "
            "match json_escape()'s output byte-for-byte");
}

// Phase 25: weighted-fair priority scheduling and backpressure.
void test_phase_twentyfive_scheduler_weighted_fairness_and_backpressure() {
    // Cancellation/shutdown work always preempts weighted selection.
    {
        masterai::RequestScheduler scheduler;
        const auto chat = scheduler.admit(masterai::SchedulingClass::interactive_chat);
        const auto cancel =
            scheduler.admit(masterai::SchedulingClass::cancellation_shutdown);
        require(chat.admitted && cancel.admitted,
                "scheduler refused ordinary admission under no pressure");
        const auto next = scheduler.next_ready();
        require(next.has_value() &&
                    next->klass == masterai::SchedulingClass::cancellation_shutdown,
                "cancellation_shutdown work was not selected ahead of "
                "interactive_chat");
    }

    // Per-class queue-depth backpressure: exceeding a class's own bound is
    // refused without touching any other class.
    {
        std::map<masterai::SchedulingClass, masterai::SchedulingClassPolicy>
            policies = masterai::default_scheduling_policies();
        policies[masterai::SchedulingClass::maintenance].max_queue_depth = 2U;
        masterai::RequestScheduler scheduler(policies);
        require(scheduler.admit(masterai::SchedulingClass::maintenance).admitted,
                "first maintenance admission was unexpectedly refused");
        require(scheduler.admit(masterai::SchedulingClass::maintenance).admitted,
                "second maintenance admission was unexpectedly refused");
        const auto third = scheduler.admit(masterai::SchedulingClass::maintenance);
        require(!third.admitted,
                "maintenance admission exceeded its configured queue depth");
    }

    // Global backpressure rejects/evicts low-priority background work
    // first, never a higher-or-equal-priority class's queued work.
    {
        masterai::RequestScheduler scheduler(masterai::default_scheduling_policies(),
                                             1U);
        const auto background =
            scheduler.admit(masterai::SchedulingClass::maintenance);
        require(background.admitted, "seed maintenance admission failed");
        const auto interactive =
            scheduler.admit(masterai::SchedulingClass::interactive_chat, 0U);
        require(interactive.admitted,
                "interactive_chat request was refused instead of preempting "
                "lower-priority queued maintenance work");
        require(interactive.preempted.has_value() &&
                    interactive.preempted->klass ==
                        masterai::SchedulingClass::maintenance,
                "global backpressure did not evict the lower-priority queued "
                "maintenance ticket to make room");
        const auto status = scheduler.status();
        const auto maintenance_status =
            status.find(masterai::SchedulingClass::maintenance);
        require(maintenance_status != status.end() &&
                    maintenance_status->second.queued == 0U,
                "preempted maintenance ticket was still reported as queued");
    }

    // A higher-weight class is dequeued more often than a lower-weight one
    // under sustained contention (weighted fairness, not strict priority
    // starvation of the lighter class).
    {
        std::map<masterai::SchedulingClass, masterai::SchedulingClassPolicy>
            policies = masterai::default_scheduling_policies();
        policies[masterai::SchedulingClass::interactive_chat] = {
            8U, 128U, std::chrono::minutes(5), 128U, 0U};
        policies[masterai::SchedulingClass::maintenance] = {
            1U, 128U, std::chrono::minutes(5), 128U, 0U};
        masterai::RequestScheduler scheduler(policies);
        for (unsigned int i = 0U; i < 40U; ++i) {
            scheduler.admit(masterai::SchedulingClass::interactive_chat);
            scheduler.admit(masterai::SchedulingClass::maintenance);
        }
        unsigned int chat_selected = 0U;
        unsigned int maintenance_selected = 0U;
        for (unsigned int i = 0U; i < 80U; ++i) {
            const auto next = scheduler.next_ready();
            require(next.has_value(), "scheduler ran dry before all work was drained");
            if (next->klass == masterai::SchedulingClass::interactive_chat) {
                ++chat_selected;
            } else if (next->klass == masterai::SchedulingClass::maintenance) {
                ++maintenance_selected;
            }
            scheduler.complete(*next);
        }
        require(chat_selected == 40U && maintenance_selected == 40U,
                "weighted scheduler dropped work instead of eventually "
                "draining every admitted ticket");
        require(chat_selected > 0U && maintenance_selected > 0U,
                "the lower-weight class was starved outright rather than "
                "merely selected less often");
    }

    // Cancellation removes queued work immediately and it is never dequeued.
    {
        masterai::RequestScheduler scheduler;
        const auto admission =
            scheduler.admit(masterai::SchedulingClass::user_background_job);
        require(admission.admitted && admission.ticket.has_value(),
                "seed admission for the cancellation case failed");
        require(scheduler.cancel(*admission.ticket),
                "cancel() reported failure for a still-queued ticket");
        require(!scheduler.cancel(*admission.ticket),
                "cancel() succeeded twice for the same ticket");
    }
}

// Phase 27: per-slot KV accounting, bounded growth, and deterministic
// eviction order.
void test_phase_twentyseven_kv_cache_accounting_and_eviction() {
    TemporaryDirectory temporary;
    const auto hardware = masterai::probe_hardware(temporary.path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware, 512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);

    // Bounded growth steps: growth is rounded up to the step size and never
    // exceeds the configured hard per-slot ceiling.
    {
        masterai::KvCacheManager kv(memory, /*hard_max=*/1024ULL * 1024ULL,
                                    /*step=*/64ULL * 1024ULL);
        masterai::KvSlotAccounting initial;
        initial.slot_id = 1U;
        initial.bytes_reserved = 10ULL * 1024ULL;
        const auto reservation = kv.reserve(initial);
        require(reservation.admitted && reservation.granted_bytes == 64ULL * 1024ULL,
                "initial KV reservation was not rounded up to the growth step");

        const auto grown = kv.grow(1U, 500ULL * 1024ULL);
        require(grown.admitted && grown.granted_bytes % (64ULL * 1024ULL) == 0U,
                "grow() did not stay aligned to the configured growth step");

        const auto refused = kv.grow(1U, 10ULL * 1024ULL * 1024ULL);
        require(!refused.admitted,
                "grow() admitted a request that would exceed the hard "
                "per-slot memory ceiling");
        const auto after = kv.find(1U);
        require(after.has_value() && after->bytes_reserved == grown.granted_bytes,
                "a refused grow() call still mutated the slot's reservation");
    }

    // Deterministic eviction order: failed/cancelled slots are evicted
    // before an idle private slot, which is evicted before a pinned slot
    // (never touched).
    {
        masterai::KvCacheManager kv(memory, /*hard_max=*/1024ULL * 1024ULL,
                                    /*step=*/4096ULL);
        masterai::KvSlotAccounting pinned;
        pinned.slot_id = 10U;
        pinned.bytes_reserved = 4096U;
        pinned.pinned = true;
        pinned.state = masterai::KvSlotState::idle;
        require(kv.reserve(pinned).admitted, "pinned slot seed reservation failed");

        masterai::KvSlotAccounting idle_slot;
        idle_slot.slot_id = 11U;
        idle_slot.bytes_reserved = 4096U;
        idle_slot.state = masterai::KvSlotState::idle;
        idle_slot.reuse_count = 5U;
        require(kv.reserve(idle_slot).admitted, "idle slot seed reservation failed");

        masterai::KvSlotAccounting failed_slot;
        failed_slot.slot_id = 12U;
        failed_slot.bytes_reserved = 4096U;
        failed_slot.state = masterai::KvSlotState::failed_cancelled;
        require(kv.reserve(failed_slot).admitted,
                "failed slot seed reservation failed");

        const auto first_victim = kv.evict_one();
        require(first_victim.has_value() && *first_victim == 12U,
                "a failed/cancelled slot was not evicted before an idle slot");
        kv.release(12U);

        const auto second_victim = kv.evict_one();
        require(second_victim.has_value() && *second_victim == 11U,
                "an idle private slot was not evicted before a pinned slot");
        kv.release(11U);

        const auto third_victim = kv.evict_one();
        require(!third_victim.has_value(),
                "evict_one() selected a pinned slot instead of refusing "
                "(safe rejection)");
    }

    // Reduced-precision KV never self-enables from recorded evidence.
    {
        masterai::KvCacheManager kv(memory, 1024ULL * 1024ULL, 4096ULL);
        require(kv.precision_admitted(masterai::KvPrecision::full),
                "full precision was unexpectedly refused");
        require(!kv.precision_admitted(masterai::KvPrecision::half),
                "half precision was admitted without backend validation");
        masterai::KvPrecisionEvidence evidence;
        evidence.precision = masterai::KvPrecision::half;
        evidence.quality_parity_verified = true;
        kv.record_precision_evidence(evidence);
        require(!kv.precision_admitted(masterai::KvPrecision::half),
                "recording evidence alone enabled a reduced-precision policy");
    }
}

// Phase 28: hardware topology probing and thread-placement recommendation.
void test_phase_twentyeight_topology_and_placement_recommendation() {
    const auto topology = masterai::probe_hardware_topology();
    require(topology.logical_core_count > 0U,
            "topology probe reported zero logical cores");
    require(topology.numa_node_count >= 1U,
            "topology probe reported zero NUMA nodes");

    // A synthetic single-node topology always falls back to normal OS
    // scheduling regardless of policy.
    masterai::HardwareTopology single_node;
    single_node.multi_node = false;
    single_node.numa_node_count = 1U;
    masterai::TopologyAffinityPolicy enabled_policy;
    enabled_policy.numa_local_placement_enabled = true;
    enabled_policy.hybrid_core_policy_enabled = true;
    const auto single_node_recommendation = masterai::recommend_thread_placement(
        single_node, masterai::ThreadClass::inference_compute, enabled_policy, false);
    require(!single_node_recommendation.preferred_numa_node.has_value(),
            "a single-node host was given a NUMA placement recommendation");

    // A synthetic multi-node, hybrid-core topology: latency-sensitive
    // classes stay on performance cores; background classes prefer
    // efficiency cores once hybrid policy is enabled.
    masterai::HardwareTopology multi_node;
    multi_node.multi_node = true;
    multi_node.numa_node_count = 2U;
    multi_node.hybrid_cores = true;
    multi_node.performance_core_count = 4U;
    multi_node.efficiency_core_count = 4U;

    const auto interactive_recommendation = masterai::recommend_thread_placement(
        multi_node, masterai::ThreadClass::inference_compute, enabled_policy, false);
    require(interactive_recommendation.preferred_numa_node.has_value() &&
                !interactive_recommendation.prefer_efficiency_core,
            "a latency-sensitive class was moved off performance cores");

    const auto background_recommendation = masterai::recommend_thread_placement(
        multi_node, masterai::ThreadClass::indexing, enabled_policy, false);
    require(background_recommendation.prefer_efficiency_core,
            "a background class was not moved to efficiency cores under "
            "hybrid policy");

    masterai::TopologyAffinityPolicy disabled_policy;
    const auto disabled_recommendation = masterai::recommend_thread_placement(
        multi_node, masterai::ThreadClass::inference_compute, disabled_policy, false);
    require(!disabled_recommendation.preferred_numa_node.has_value() &&
                !disabled_recommendation.prefer_efficiency_core,
            "topology affinity was applied even though policy disabled it");
}

// Phase 29: model tiering, routing, and cascade inference.
void test_phase_twentynine_model_tiering_and_cascade() {
    std::map<masterai::ModelTier, std::vector<std::string>> tiers;
    tiers[masterai::ModelTier::compact_router] = {"router-tiny"};
    tiers[masterai::ModelTier::small_fast] = {"fast-1b"};
    tiers[masterai::ModelTier::medium_general] = {"general-8b"};
    tiers[masterai::ModelTier::large_specialist] = {"specialist-70b"};
    masterai::ModelRouter router(tiers);

    // A plain draft-quality request routes to the small tier, never
    // straight to the largest resident model.
    {
        masterai::RoutingSignals signals;
        signals.requested_quality = "draft";
        signals.available_ram_mib = 999999U;
        const auto assignment = router.select_initial_tier(signals);
        require(assignment.has_value() &&
                    assignment->tier == masterai::ModelTier::small_fast,
                "a draft-quality request was not routed to the small tier");
    }

    // A high-quality request escalates the initial choice to the large
    // specialist tier.
    {
        masterai::RoutingSignals signals;
        signals.requested_quality = "high";
        signals.available_ram_mib = 999999U;
        const auto assignment = router.select_initial_tier(signals);
        require(assignment.has_value() &&
                    assignment->tier == masterai::ModelTier::large_specialist,
                "a high-quality request was not routed to the large "
                "specialist tier");
    }

    // Constrained available RAM downgrades the selected tier instead of
    // routing to a model that cannot fit.
    {
        masterai::RoutingSignals signals;
        signals.requested_quality = "high";
        signals.available_ram_mib = 1024U;
        const auto assignment = router.select_initial_tier(signals);
        require(assignment.has_value() &&
                    assignment->tier != masterai::ModelTier::large_specialist,
                "a memory-constrained host was still routed to the large "
                "specialist tier");
    }

    // An explicit user pin wins regardless of other signals.
    {
        masterai::RoutingSignals signals;
        signals.requested_quality = "draft";
        signals.user_pinned_model_id = "specialist-70b";
        const auto assignment = router.select_initial_tier(signals);
        require(assignment.has_value() && assignment->model_id == "specialist-70b",
                "an explicit user pin was not honoured over routing signals");
    }

    // Cascade escalation on failed deterministic validation, and safe
    // refusal once already at the largest tier.
    {
        masterai::CascadeStageOutcome outcome;
        outcome.tier = masterai::ModelTier::small_fast;
        outcome.succeeded = true;
        outcome.confidence = 0.95;
        outcome.deterministic_validation_failed = true;
        const auto decision = masterai::ModelRouter::evaluate_cascade(outcome);
        require(decision.escalate &&
                    decision.reason ==
                        masterai::EscalationReason::failed_deterministic_validation &&
                    decision.next_tier.has_value() &&
                    *decision.next_tier == masterai::ModelTier::medium_general,
                "failed deterministic validation did not escalate to the "
                "next tier");

        masterai::CascadeStageOutcome at_ceiling;
        at_ceiling.tier = masterai::ModelTier::large_specialist;
        at_ceiling.succeeded = false;
        const auto ceiling_decision =
            masterai::ModelRouter::evaluate_cascade(at_ceiling);
        require(!ceiling_decision.escalate && !ceiling_decision.next_tier.has_value(),
                "cascade escalation was granted past the largest tier "
                "instead of safely refusing");
    }

    // No escalation when the stage succeeded with high confidence and no
    // triggering condition fired.
    {
        masterai::CascadeStageOutcome clean;
        clean.tier = masterai::ModelTier::small_fast;
        clean.succeeded = true;
        clean.confidence = 0.99;
        const auto decision = masterai::ModelRouter::evaluate_cascade(clean);
        require(!decision.escalate &&
                    decision.reason == masterai::EscalationReason::none,
                "a clean, high-confidence stage outcome was escalated anyway");
    }

    // Resident-model profile validation.
    {
        require(masterai::ModelRouter::resident_set_within_profile(
                    masterai::ResidentModelProfile::minimal,
                    {masterai::ModelTier::small_fast}),
                "minimal profile rejected a single resident generation model");
        require(!masterai::ModelRouter::resident_set_within_profile(
                    masterai::ResidentModelProfile::minimal,
                    {masterai::ModelTier::small_fast,
                     masterai::ModelTier::medium_general}),
                "minimal profile accepted two simultaneously resident "
                "generation models");
        require(masterai::ModelRouter::resident_set_within_profile(
                    masterai::ResidentModelProfile::balanced,
                    {masterai::ModelTier::medium_general,
                     masterai::ModelTier::compact_router}),
                "balanced profile rejected one generation model plus one "
                "compact router model");
        require(!masterai::ModelRouter::resident_set_within_profile(
                    masterai::ResidentModelProfile::balanced,
                    {masterai::ModelTier::medium_general,
                     masterai::ModelTier::large_specialist}),
                "balanced profile accepted two simultaneously resident "
                "generation models");
    }
}

// Phase 23: a repeated tokenize() call for identical text/model/policy must
// be served from CacheManager instead of the runner's /tokenize HTTP round
// trip. Proven here by unloading the runner (so any HTTP attempt would
// throw "runner is not ready") between the two calls -- the second call can
// only succeed by having actually skipped the runner.
void test_phase_twentythree_tokenization_cache_hit_avoids_retokenize() {
    TemporaryDirectory temporary;
    const auto hardware = masterai::probe_hardware(temporary.path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware, 512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);
    masterai::CachePolicy cache_policy;
    cache_policy.maximum_bytes_per_category = 1024ULL * 1024ULL;
    masterai::CacheManager cache(temporary.path() / "cache", memory, cache_policy);

    const auto model_directory = temporary.path() / "model";
    std::filesystem::create_directories(model_directory);
    const auto model_file = model_directory / "model.gguf";
    write_text(model_file, "GGUF-tokcache-fixture");
    masterai::ModelRecord model;
    model.directory = model_directory;
    model.manifest.id = "tokcache-fixture";
    model.manifest.model_file = "model.gguf";
    model.manifest.model_size_bytes = std::filesystem::file_size(model_file);
    model.manifest.model_sha256 = masterai::sha256_file_hex(model_file);
    model.state = masterai::ModelState::ready;
    const auto backend = fake_llama_executable();
    masterai::RunnerSupervisor supervisor(backend, temporary.path() / "runtime");
    supervisor.load(model, 4096U, 18090U, 5U);
    supervisor.set_tokenization_cache(&cache);

    require(supervisor.tokenize("one two three") == 3U,
            "first tokenize() call (a cache miss) did not reach the runner");
    supervisor.unload(0U);
    require(supervisor.metrics().state == masterai::RunnerState::unloaded,
            "runner did not unload before the cache-hit check");
    require(supervisor.tokenize("one two three") == 3U,
            "a cache hit did not avoid the runner HTTP round trip -- the "
            "runner is unloaded, so this could only succeed from cache");

    bool threw_for_uncached_text = false;
    try {
        static_cast<void>(supervisor.tokenize("a different uncached string"));
    } catch (const std::exception&) {
        threw_for_uncached_text = true;
    }
    require(threw_for_uncached_text,
            "tokenizing different, never-cached text against an unloaded "
            "runner unexpectedly succeeded -- the cache-hit check above is "
            "not actually proving anything if this can happen");
}

// Phase 23: cached (process-lifetime-compiled) chat-template output must be
// byte-for-byte identical to the old repeated std::string += concatenation
// it replaces.
void test_phase_twentythree_segmented_assembly_byte_identical() {
    const masterai::ChatWrapTemplate tmpl{
        "<|system|>\n", "<|end|>\n", "<|user|>\n", "<|end|>\n",
        "<|assistant|>\n", "<|end|>\n", "<|assistant|>\n", "<|end|>"};
    std::vector<masterai::ChatMessage> history;
    history.push_back(masterai::ChatMessage{masterai::ChatRole::system, "be terse", 0U});
    history.push_back(masterai::ChatMessage{masterai::ChatRole::user, "hello", 0U});
    history.push_back(masterai::ChatMessage{masterai::ChatRole::assistant, "hi there", 0U});
    const std::string latest = "what's next?";

    // Reference implementation: mirrors server.cpp's pre-Phase-23
    // assemble_chat_prompt() body exactly (plain concatenation).
    std::string expected;
    for (const auto& message : history) {
        switch (message.role) {
            case masterai::ChatRole::system:
                expected += tmpl.system_prefix + message.content + tmpl.system_suffix;
                break;
            case masterai::ChatRole::user:
                expected += tmpl.user_prefix + message.content + tmpl.user_suffix;
                break;
            case masterai::ChatRole::assistant:
                expected += tmpl.assistant_prefix + message.content + tmpl.assistant_suffix;
                break;
        }
    }
    expected += tmpl.user_prefix + latest + tmpl.user_suffix;
    expected += tmpl.generation_prompt;

    const auto& plan =
        masterai::compiled_chat_template("phi3-test-architecture", tmpl);
    const auto segments =
        masterai::assemble_chat_prompt_segments(plan, history, latest);
    const auto actual = masterai::materialize_prompt(segments);
    require(actual == expected,
            "segmented prompt assembly diverged from the old "
            "concatenation-based output");

    // A repeat call for the same architecture must return the same cached
    // plan instance (process-lifetime cache, not rebuilt every call) and
    // still produce byte-identical output.
    const auto& plan_again =
        masterai::compiled_chat_template("phi3-test-architecture", tmpl);
    require(&plan_again == &plan,
            "compiled_chat_template() did not return the cached plan on a "
            "repeat call for the same architecture");
    const auto segments_again =
        masterai::assemble_chat_prompt_segments(plan_again, history, latest);
    require(masterai::materialize_prompt(segments_again) == expected,
            "cached-template output was not byte-identical to the uncached "
            "run");
}

// Phase 23: a clean, fingerprint-matching literal-prefix extension reports
// reuse == true along with the exact reusable-prefix byte count, a
// divergence_offset at the boundary of the reused prefix, no invalidation
// reason, and the configured ceiling.
void test_phase_twentythree_session_reuse_reports_prefix_fields_on_success() {
    masterai::PromptSessionManager sessions(4U, 300U);
    masterai::SessionFingerprint fingerprint;
    fingerprint.model_sha256 = "model-digest";
    fingerprint.backend_executable = "/opt/llama-server";
    fingerprint.architecture = "llama";
    fingerprint.context_length = 4096U;
    fingerprint.project_index_generation = 1U;
    const std::string turn_one = "system+turn-one";
    const auto slot = sessions.record("chat-a", fingerprint, turn_one, std::nullopt);
    const std::string turn_two = turn_one + "+turn-two";
    const auto decision = sessions.try_reuse("chat-a", fingerprint, turn_two);
    require(decision.reuse && decision.slot_id == slot,
            "a valid prefix-extending turn was not granted reuse of its warm slot");
    require(decision.invalidation_reason ==
                masterai::SessionInvalidationReason::none,
            "a granted reuse decision carried a non-none invalidation reason");
    require(decision.reusable_prefix_bytes == turn_one.size(),
            "reusable_prefix_bytes did not equal the recorded prior prompt's length");
    require(decision.divergence_offset == turn_one.size(),
            "divergence_offset did not mark the boundary of the reused prefix");
    require(decision.prefix_byte_ceiling ==
                masterai::PromptSessionManager::kDefaultMaxRetainedPrefixBytes,
            "decision did not report the default configured prefix byte ceiling");
}

// Phase 23: editing an earlier chat turn (a non-prefix divergence) must be
// reported with SessionInvalidationReason::prefix_diverged and the exact
// byte offset where the two prompts actually stop matching -- not just a
// bare refusal.
void test_phase_twentythree_session_reuse_edit_reports_divergence_offset() {
    masterai::PromptSessionManager sessions(4U, 300U);
    masterai::SessionFingerprint fingerprint;
    fingerprint.model_sha256 = "model-digest";
    fingerprint.backend_executable = "/opt/llama-server";
    fingerprint.architecture = "llama";
    fingerprint.context_length = 4096U;
    fingerprint.project_index_generation = 1U;

    const std::string turn_one = "SYS:be terse|USR:hello";
    sessions.record("chat-a", fingerprint, turn_one, std::nullopt);
    // The earlier user turn ("hello") is edited to "goodbye" before a new
    // turn is appended -- a non-prefix edit, not a clean extension.
    const std::string edited = "SYS:be terse|USR:goodbye|USR:what now?";
    const auto decision = sessions.try_reuse("chat-a", fingerprint, edited);
    require(!decision.reuse, "an edited earlier turn was still granted reuse");
    require(decision.invalidation_reason ==
                masterai::SessionInvalidationReason::prefix_diverged,
            "an edited earlier turn was not reported as prefix_diverged");

    std::size_t expected_divergence = 0U;
    while (expected_divergence < turn_one.size() &&
           expected_divergence < edited.size() &&
           turn_one[expected_divergence] == edited[expected_divergence]) {
        ++expected_divergence;
    }
    require(decision.divergence_offset == expected_divergence,
            "divergence_offset did not match the actual point of byte "
            "divergence between the recorded and edited prompts");
}

// Phase 23: a literal-prefix match that exceeds the configured maximum
// retained prefix byte ceiling must be refused (never fuzzy-truncated or
// silently allowed), with the ceiling-specific invalidation reason.
void test_phase_twentythree_session_reuse_prefix_ceiling_enforced() {
    // A tiny 8-byte ceiling so an ordinary short recorded prompt already
    // exceeds it -- proves try_reuse() refuses purely on ceiling grounds
    // even though the underlying prefix match is perfectly valid.
    masterai::PromptSessionManager sessions(4U, 300U, 8U);
    masterai::SessionFingerprint fingerprint;
    fingerprint.model_sha256 = "model-digest";
    fingerprint.backend_executable = "/opt/llama-server";
    fingerprint.architecture = "llama";
    fingerprint.context_length = 4096U;
    fingerprint.project_index_generation = 1U;

    const std::string turn_one = "this recorded prompt is longer than the ceiling";
    sessions.record("chat-a", fingerprint, turn_one, std::nullopt);
    const auto decision =
        sessions.try_reuse("chat-a", fingerprint, turn_one + "+more");
    require(!decision.reuse,
            "reuse was granted despite exceeding the configured prefix byte "
            "ceiling");
    require(decision.invalidation_reason ==
                masterai::SessionInvalidationReason::prefix_ceiling_exceeded,
            "a ceiling overrun was not reported with the correct "
            "invalidation reason");
    require(decision.prefix_byte_ceiling == 8U,
            "decision did not report the configured ceiling");
    require(decision.reusable_prefix_bytes == turn_one.size(),
            "reusable_prefix_bytes did not reflect the full valid prefix "
            "match even though reuse was refused for exceeding the ceiling");
}

// Phase 23: the intern table accepts and stably dedupes short, repeated
// identifiers, and structurally rejects anything long enough to plausibly
// be arbitrary user content or file text rather than a role/route/JSON-key
// style identifier.
void test_phase_twentythree_intern_table_restricted_to_short_identifiers() {
    const auto& first = masterai::intern_identifier("assistant");
    const auto& second = masterai::intern_identifier("assistant");
    require(&first == &second,
            "interning the same short identifier twice returned different "
            "backing storage");
    require(first == "assistant",
            "interned value did not match the input text");

    bool threw = false;
    try {
        static_cast<void>(masterai::intern_identifier(
            std::string(masterai::kMaxInternedLength + 1U, 'x')));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw,
            "interning oversized text (a stand-in for arbitrary user/file "
            "content) was accepted instead of structurally rejected");
}

// Phase 24 fixture: builds a small on-disk project with a distinctive
// shared identifier and waits for it to settle to IndexJobState::ready,
// mirroring test_phase_sixteen_deadline_bound_retrieval's setup.
void build_ready_retrieval_project(const std::filesystem::path& project_root,
                                   masterai::ProjectIndexService& service,
                                   const masterai::ProjectRecord& project) {
    std::filesystem::create_directories(project_root / "src");
    write_text(project_root / "src" / "one.cpp",
               "// one\nint phase24_marker_symbol = 41;\n");
    require(service.request_rebuild(project),
            "Phase 24 retrieval fixture index rebuild was not admitted");
    for (unsigned int attempt = 0U; attempt < 200U; ++attempt) {
        const auto status = service.status(project.id);
        if (status && status->state == masterai::IndexJobState::ready) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(false, "Phase 24 retrieval fixture index did not settle");
}

// Deterministic request classification: path-looking tokens win over every
// other signal, then diagnostic wording, then explicit doc/completion
// wording, then a bare identifier defaults to symbol explanation, and plain
// prose with none of these falls back to generic lexical search.
void test_phase_twentyfour_request_classification() {
    require(masterai::classify_retrieval_request("open src/one.cpp please") ==
                masterai::RetrievalRequestClassification::navigation,
            "a query containing a path-looking token was not classified as navigation");
    require(masterai::classify_retrieval_request(
                "error: undefined reference to marker_symbol") ==
                masterai::RetrievalRequestClassification::symbol_explanation,
            "a diagnostic-looking query was not classified as symbol explanation");
    require(masterai::classify_retrieval_request(
                "how to use the readme documentation") ==
                masterai::RetrievalRequestClassification::documentation,
            "a documentation-intent query was not classified as documentation");
    require(masterai::classify_retrieval_request("please complete this function") ==
                masterai::RetrievalRequestClassification::completion,
            "a completion-intent query was not classified as completion");
    require(masterai::classify_retrieval_request("what does marker_symbol do") ==
                masterai::RetrievalRequestClassification::symbol_explanation,
            "a bare identifier-shaped query was not classified as symbol explanation");
    require(masterai::classify_retrieval_request("hello there friend") ==
                masterai::RetrievalRequestClassification::generic_lexical,
            "plain prose with no shape signal was not classified as generic lexical");
}

// Declared-but-disabled strategies: every strategy without an adapter must
// report exactly itself with a "no adapter" reason, the enabled set must
// not appear in the disabled list, and RetrievalPlanner must stamp both the
// classification and the disabled-strategy reasons onto every outcome so a
// caller can push them onto QueryTrace.
void test_phase_twentyfour_disabled_strategies_recorded_on_trace() {
    const auto disabled = masterai::disabled_retrieval_strategy_reasons();
    require(disabled.size() == 7U,
            "the declared-but-disabled retrieval strategy count drifted from "
            "the seven strategies with no adapter");
    for (const auto& entry : disabled) {
        require(!masterai::retrieval_strategy_has_adapter(entry.first),
                "a strategy with a working adapter was reported as disabled");
        require(entry.second.find("no adapter") != std::string::npos,
                "a disabled strategy's skip reason did not explain why");
    }
    require(masterai::retrieval_strategy_has_adapter(
                masterai::RetrievalStrategy::exact_symbol) &&
                masterai::retrieval_strategy_has_adapter(
                    masterai::RetrievalStrategy::filename_path) &&
                masterai::retrieval_strategy_has_adapter(
                    masterai::RetrievalStrategy::recent_change),
            "an enabled Phase 24 strategy was incorrectly reported as having no adapter");

    TemporaryDirectory temporary;
    const auto project_root = temporary.path() / "disabled-strategy-project";
    const auto hardware = masterai::probe_hardware(temporary.path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware, 512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);
    masterai::ProjectIndexService service(
        temporary.path() / "disabled-strategy-indexes", memory, 2U);
    masterai::ProjectRecord project{"phase24-disabled", "Phase 24 Disabled", project_root};
    build_ready_retrieval_project(project_root, service, project);

    masterai::RetrievalPlanner planner(service);
    masterai::RetrievalRequest request;
    request.project = project;
    request.query_text = "phase24_marker_symbol";
    request.deadline = std::chrono::milliseconds(2000);
    const auto outcome = planner.retrieve(request);
    require(!outcome.classification.empty(),
            "retrieve() did not stamp a request classification onto its outcome");
    require(outcome.disabled_strategy_reasons.size() == 7U,
            "retrieve() did not stamp every disabled strategy's skip reason onto its outcome");

    masterai::QueryCoordinator queries;
    const auto query_id = queries.begin("user", project.id, "model");
    queries.record_classification(query_id, outcome.classification);
    queries.record_retrieval_strategy_skips(query_id, outcome.disabled_strategy_reasons);
    queries.finish(query_id, masterai::QueryStatus::completed);
    const auto trace = queries.find(query_id);
    require(trace && trace->request_classification == outcome.classification &&
                trace->disabled_retrieval_strategies.size() == 7U,
            "a successful finish() lost the recorded classification/disabled strategies");
    const auto json = masterai::QueryCoordinator::to_json(*trace);
    require(json.find("\"requestClassification\":") != std::string::npos &&
                json.find("\"disabledRetrievalStrategies\":[") != std::string::npos,
            "query trace JSON omitted the classification/disabled strategy fields");
}

// Staged fan-out: a query the cheap exact-symbol stage alone can satisfy
// must never reach the lexical/path stages -- observable both as the
// reported strategy staying "exact_symbol" (not "hybrid") and as no
// disclosure entry ever carrying a "filename_path" or "lexical" source.
void test_phase_twentyfour_staged_fanout_skips_later_stages() {
    TemporaryDirectory temporary;
    const auto project_root = temporary.path() / "staged-fanout-project";
    const auto hardware = masterai::probe_hardware(temporary.path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware, 512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);
    masterai::ProjectIndexService service(
        temporary.path() / "staged-fanout-indexes", memory, 2U);
    masterai::ProjectRecord project{"phase24-staged", "Phase 24 Staged", project_root};
    build_ready_retrieval_project(project_root, service, project);

    masterai::RetrievalPlanner planner(service);
    masterai::RetrievalRequest request;
    request.project = project;
    request.query_text = "please explain phase24_marker_symbol";
    request.deadline = std::chrono::milliseconds(2000);
    const auto outcome = planner.retrieve(request);
    require(outcome.strategy == "exact_symbol",
            "a query the cheap symbol stage alone could satisfy still ran a "
            "later, more expensive stage");
    for (const auto& entry : outcome.disclosure) {
        require(entry.source != "filename_path" && entry.source != "lexical",
                "staged fan-out ran a later stage even though the first "
                "sufficient stage already found evidence");
    }
}

// Duplicate evidence: a single chunk containing two distinct identifiers
// that both appear in the query text is found by two separate exact-symbol
// search tasks (one per identifier token) within stage 1 -- fusion by
// canonical chunk id must collapse that into exactly one candidate, so the
// shared arena buffer materializes the chunk's bytes exactly once rather
// than once per corroborating hit.
void test_phase_twentyfour_duplicate_chunks_materialize_once() {
    TemporaryDirectory temporary;
    const auto project_root = temporary.path() / "dedup-project";
    std::filesystem::create_directories(project_root / "src");
    write_text(project_root / "src" / "one.cpp",
               "// one\nint phase24_dedup_alpha = 1;\n"
               "int phase24_dedup_beta = 2;\n");
    const auto hardware = masterai::probe_hardware(temporary.path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware, 512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);
    masterai::ProjectIndexService service(
        temporary.path() / "dedup-indexes", memory, 2U);
    masterai::ProjectRecord project{"phase24-dedup", "Phase 24 Dedup", project_root};
    require(service.request_rebuild(project),
            "Phase 24 dedup fixture index rebuild was not admitted");
    for (unsigned int attempt = 0U; attempt < 200U; ++attempt) {
        const auto status = service.status(project.id);
        if (status && status->state == masterai::IndexJobState::ready) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    masterai::RetrievalPlanner planner(service);
    masterai::RetrievalRequest request;
    request.project = project;
    // Both identifiers live in the same 4KB chunk, so the two separate
    // exact-symbol search tasks this query text produces both return the
    // exact same chunk id.
    request.query_text = "phase24_dedup_alpha phase24_dedup_beta";
    request.deadline = std::chrono::milliseconds(2000);
    const auto outcome = planner.retrieve(request);

    require(outcome.strategy == "exact_symbol",
            "the dedup fixture query unexpectedly needed a later, more "
            "expensive stage");
    std::size_t included_count = 0U;
    for (const auto& entry : outcome.disclosure) {
        if (entry.included) ++included_count;
    }
    require(outcome.disclosure.size() == 1U && included_count == 1U,
            "a chunk found by two corroborating exact-symbol searches fused "
            "into more than one candidate instead of one corroborated one");
    std::size_t occurrences = 0U;
    std::size_t position = 0U;
    while ((position = outcome.context_text.find("phase24_dedup_alpha", position)) !=
           std::string::npos) {
        ++occurrences;
        position += 1U;
    }
    require(occurrences == 1U,
            "the fused chunk's bytes were materialized more than once into "
            "the assembled context text");
}

// Shared in-flight futures: genuinely concurrent identical requests (same
// project/requester/policy-generation/settings) must join into exactly one
// underlying computation.
void test_phase_twentyfour_concurrent_identical_requests_join() {
    TemporaryDirectory temporary;
    const auto project_root = temporary.path() / "join-project";
    const auto hardware = masterai::probe_hardware(temporary.path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware, 512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);
    masterai::ProjectIndexService service(
        temporary.path() / "join-indexes", memory, 2U);
    masterai::ProjectRecord project{"phase24-join", "Phase 24 Join", project_root};
    build_ready_retrieval_project(project_root, service, project);

    masterai::RetrievalPlanner planner(service);
    masterai::RetrievalRequest request;
    request.project = project;
    request.requester_id = "user-join";
    request.policy_generation = 7U;
    request.query_text = "phase24_marker_symbol";
    request.deadline = std::chrono::milliseconds(2000);

    constexpr int thread_count = 8;
    std::atomic<int> ready_count{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    std::vector<masterai::RetrievalOutcome> results(thread_count);
    threads.reserve(thread_count);
    for (int index = 0; index < thread_count; ++index) {
        threads.emplace_back([&, index]() {
            ready_count.fetch_add(1);
            while (!go.load()) { /* spin until every thread is ready */ }
            results[static_cast<std::size_t>(index)] = planner.retrieve(request);
        });
    }
    while (ready_count.load() < thread_count) { /* wait for all threads */ }
    go.store(true);
    for (auto& thread : threads) thread.join();

    require(planner.uncached_invocation_count() == 1U,
            "concurrent identical retrieval requests did not join into a "
            "single in-flight computation");
    for (int index = 1; index < thread_count; ++index) {
        require(results[static_cast<std::size_t>(index)].context_text ==
                    results[0].context_text &&
                    results[static_cast<std::size_t>(index)].strategy ==
                        results[0].strategy,
                "joined in-flight requests returned divergent outcomes");
    }
}

// Hard correctness requirement: joining must never cross an authorization
// or project boundary. Two concurrent requests that differ only in
// requester_id (a stand-in for "different authenticated caller") must never
// join, even though every other field -- including the query text -- is
// identical.
void test_phase_twentyfour_mismatched_auth_does_not_join() {
    TemporaryDirectory temporary;
    const auto project_root = temporary.path() / "no-join-project";
    const auto hardware = masterai::probe_hardware(temporary.path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware, 512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);
    masterai::ProjectIndexService service(
        temporary.path() / "no-join-indexes", memory, 2U);
    masterai::ProjectRecord project{"phase24-no-join", "Phase 24 No Join", project_root};
    build_ready_retrieval_project(project_root, service, project);

    masterai::RetrievalPlanner planner(service);
    masterai::RetrievalRequest request_alice;
    request_alice.project = project;
    request_alice.requester_id = "alice";
    request_alice.policy_generation = 1U;
    request_alice.query_text = "phase24_marker_symbol";
    request_alice.deadline = std::chrono::milliseconds(2000);

    masterai::RetrievalRequest request_bob = request_alice;
    request_bob.requester_id = "bob";  // only the authorized identity differs

    std::atomic<int> ready_count{0};
    std::atomic<bool> go{false};
    masterai::RetrievalOutcome alice_result;
    masterai::RetrievalOutcome bob_result;
    std::thread alice_thread([&]() {
        ready_count.fetch_add(1);
        while (!go.load()) { /* spin */ }
        alice_result = planner.retrieve(request_alice);
    });
    std::thread bob_thread([&]() {
        ready_count.fetch_add(1);
        while (!go.load()) { /* spin */ }
        bob_result = planner.retrieve(request_bob);
    });
    while (ready_count.load() < 2) { /* wait for both threads */ }
    go.store(true);
    alice_thread.join();
    bob_thread.join();

    require(planner.uncached_invocation_count() == 2U,
            "requests authorized under different identities were incorrectly "
            "joined into a single in-flight computation");
    require(alice_result.context_text == bob_result.context_text,
            "mismatched-identity requests should still compute the same "
            "evidence independently, just never share the computation");
}

// Phase 30: RetrievalPlanner's fusion-candidate pool must actually register
// against MemoryBudgetManager when one is supplied (the real consumer this
// session wires up -- see retrieval.cpp's retrieve_uncached()), and must
// leave zero residue once retrieve() returns (its lease is call-scoped,
// released via PoolLeaseGuard on every return path) -- and, symmetrically, a
// planner constructed without a MemoryBudgetManager (the default, matching
// every pre-Phase-30 call site and test fixture) must never touch the
// category at all, nor change retrieval's own outcome.
void test_phase_thirty_retrieval_candidate_pool_registers_with_budget_manager() {
    TemporaryDirectory temporary;
    const auto project_root = temporary.path() / "pool-budget-project";
    const auto hardware = masterai::probe_hardware(temporary.path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware, 512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);
    masterai::ProjectIndexService service(
        temporary.path() / "pool-budget-indexes", memory, 2U);
    masterai::ProjectRecord project{"phase30-pool-budget", "Phase 30 Pool Budget",
                                    project_root};
    build_ready_retrieval_project(project_root, service, project);

    masterai::RetrievalPlanner unwired_planner(service);  // memory_ == nullptr
    masterai::RetrievalRequest request;
    request.project = project;
    request.query_text = "phase24_marker_symbol";
    const auto unwired_outcome = unwired_planner.retrieve(request);
    require(!unwired_outcome.disclosure.empty(),
            "fixture query returned no evidence for the unwired planner");
    // Same "single snapshot" caution as above: memory.status() returns
    // MemoryStatus by value, so this must not compare iterators from two
    // separately-called temporaries.
    const auto unwired_status = memory.status();
    require(unwired_status.category_bytes.find(
                masterai::MemoryCategory::retrieval_index_cache) ==
                unwired_status.category_bytes.end(),
            "a RetrievalPlanner constructed without a MemoryBudgetManager "
            "touched the category anyway");

    masterai::RetrievalPlanner wired_planner(service, &memory);
    const auto wired_outcome = wired_planner.retrieve(request);
    require(wired_outcome.disclosure.size() == unwired_outcome.disclosure.size() &&
                wired_outcome.context_text == unwired_outcome.context_text,
            "wiring a MemoryBudgetManager changed retrieval's own outcome");
    require(memory.status().category_bytes.at(
                masterai::MemoryCategory::retrieval_index_cache) == 0U,
            "the fusion-candidate pool's lease was not fully released once "
            "retrieve() returned");
}

// Phase 30: many concurrent retrieval requests, each pool-allocating its own
// call-scoped RetrievalCandidate set and registering/releasing its own
// MemoryBudgetManager lease, must never produce a dangling ChunkReference/
// BufferView (every returned outcome's materialized context text must
// contain exactly its own request's expected marker, never another
// request's) and must leave the shared MemoryBudgetManager's
// retrieval_index_cache category back at zero once every thread has
// finished, proving no lease leaked or double-released under concurrency.
// Builds on the same concurrent-thread harness shape as Phase 24's
// test_phase_twentyfour_concurrent_identical_requests_join, but with
// deliberately distinct per-thread query text so the in-flight join table
// never collapses these into one shared computation -- this test wants many
// genuinely independent pool lifetimes racing on the same manager.
void test_phase_thirty_concurrent_retrieval_no_dangling_view() {
    TemporaryDirectory temporary;
    const auto project_root = temporary.path() / "stress-project";
    std::filesystem::create_directories(project_root / "src");
    constexpr int file_count = 12;
    for (int i = 0; i < file_count; ++i) {
        write_text(project_root / "src" / ("file" + std::to_string(i) + ".cpp"),
                   "// file " + std::to_string(i) + "\n"
                   "int phase30_stress_marker_" + std::to_string(i) + " = " +
                       std::to_string(i) + ";\n");
    }
    const auto hardware = masterai::probe_hardware(temporary.path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware, 512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);
    masterai::ProjectIndexService service(
        temporary.path() / "stress-indexes", memory, 4U);
    masterai::ProjectRecord project{"phase30-stress", "Phase 30 Stress", project_root};
    require(service.request_rebuild(project),
            "Phase 30 stress fixture index rebuild was not admitted");
    for (unsigned int attempt = 0U; attempt < 200U; ++attempt) {
        const auto status = service.status(project.id);
        if (status && status->state == masterai::IndexJobState::ready) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    masterai::RetrievalPlanner planner(service, &memory);
    constexpr int thread_count = 12;
    std::vector<std::thread> threads;
    std::vector<masterai::RetrievalOutcome> results(thread_count);
    threads.reserve(thread_count);
    std::atomic<int> ready_count{0};
    std::atomic<bool> go{false};
    for (int index = 0; index < thread_count; ++index) {
        threads.emplace_back([&, index]() {
            masterai::RetrievalRequest request;
            request.project = project;
            request.requester_id = "stress-user-" + std::to_string(index);
            request.query_text =
                "phase30_stress_marker_" + std::to_string(index % file_count);
            ready_count.fetch_add(1);
            while (!go.load()) { /* spin until every thread is ready */ }
            results[static_cast<std::size_t>(index)] = planner.retrieve(request);
        });
    }
    while (ready_count.load() < thread_count) { /* wait for all threads */ }
    go.store(true);
    for (auto& thread : threads) thread.join();

    for (int index = 0; index < thread_count; ++index) {
        const auto& outcome = results[static_cast<std::size_t>(index)];
        require(!outcome.disclosure.empty(),
                "a concurrent retrieval request returned no evidence");
        const auto expected =
            "phase30_stress_marker_" + std::to_string(index % file_count);
        require(outcome.context_text.find(expected) != std::string::npos,
                "a concurrent request's materialized context text did not "
                "contain its own expected marker -- possible dangling view "
                "or cross-request corruption");
    }

    require(memory.status().category_bytes.at(
                masterai::MemoryCategory::retrieval_index_cache) == 0U,
            "concurrent retrieval left a residual MemoryBudgetManager "
            "reservation after every request finished");
}

// Phase 26: select_load_mode() responds to storage-latency/RAM evidence,
// pre-touch levels report their real backend-actionable set (only
// none/full), and CalibrationService::resolve()'s evidence-aware overload
// picks a load mode from that evidence while its pre-Phase-26 2-arg form
// stays byte-for-byte the same as before.
void test_phase_twentysix_load_mode_selection() {
    masterai::StorageLatencyProfile unknown_size_storage;
    unknown_size_storage.storage_class = "fixed";
    unknown_size_storage.measured_read_latency_us = 150.0;
    require(masterai::select_load_mode(unknown_size_storage,
                                       8ULL * 1024 * 1024 * 1024, 0U) ==
                masterai::ModelLoadMode::mapped,
            "an unknown model size did not fall back to the safe mapped default");

    require(masterai::select_load_mode(unknown_size_storage,
                                       1ULL * 1024 * 1024 * 1024,
                                       4ULL * 1024 * 1024 * 1024) ==
                masterai::ModelLoadMode::streamed,
            "insufficient RAM to hold the model did not select streamed mode");

    masterai::StorageLatencyProfile hdd_shaped;
    hdd_shaped.storage_class = "fixed";
    hdd_shaped.measured_read_latency_us = 6000.0;
    hdd_shaped.sequential = true;
    require(masterai::select_load_mode(hdd_shaped, 16ULL * 1024 * 1024 * 1024,
                                       2ULL * 1024 * 1024 * 1024) ==
                masterai::ModelLoadMode::resident,
            "ample RAM with HDD-shaped slow storage did not select resident");

    masterai::StorageLatencyProfile nvme_shaped;
    nvme_shaped.storage_class = "fixed";
    nvme_shaped.measured_read_latency_us = 80.0;
    nvme_shaped.sequential = false;
    require(masterai::select_load_mode(nvme_shaped, 16ULL * 1024 * 1024 * 1024,
                                       2ULL * 1024 * 1024 * 1024) ==
                masterai::ModelLoadMode::mapped,
            "ample RAM with NVMe-shaped fast storage did not stay at mapped");

    require(masterai::pre_touch_level_backend_actionable(
                masterai::PreTouchLevel::none) &&
                masterai::pre_touch_level_backend_actionable(
                    masterai::PreTouchLevel::full) &&
                !masterai::pre_touch_level_backend_actionable(
                    masterai::PreTouchLevel::metadata) &&
                !masterai::pre_touch_level_backend_actionable(
                    masterai::PreTouchLevel::first_use) &&
                !masterai::pre_touch_level_backend_actionable(
                    masterai::PreTouchLevel::layer_window),
            "pre-touch backend-actionable set was not exactly {none, full}");
    require(!masterai::pretouch_gap_reason(masterai::PreTouchLevel::none)
                 .has_value() &&
                !masterai::pretouch_gap_reason(masterai::PreTouchLevel::full)
                     .has_value() &&
                masterai::pretouch_gap_reason(masterai::PreTouchLevel::layer_window)
                    .has_value(),
            "pretouch_gap_reason() did not document the accepted-but-inert "
            "pre-touch levels");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::TuningProfileStore store(records);
    const auto backend = fake_llama_executable();
    require(std::filesystem::is_regular_file(backend),
            "fake llama runner fixture is unavailable");
    masterai::HardwareInfo hardware;
    hardware.platform = "test-platform";
    hardware.architecture = "x86_64";
    hardware.logical_cpu_count = 4U;
    masterai::RunnerSupervisor supervisor(backend, temporary.path() / "runtime");
    masterai::CalibrationService calibration(supervisor, store, hardware,
                                             "backend-hash-p26", "build-p26");
    const std::string model_sha256(64U, 'c');
    const auto resolved =
        calibration.resolve(model_sha256, "balanced", &hdd_shaped,
                            16ULL * 1024 * 1024 * 1024, 2ULL * 1024 * 1024 * 1024);
    require(resolved.recommended_load_mode == masterai::ModelLoadMode::resident &&
                resolved.recommended_pre_touch == masterai::PreTouchLevel::full &&
                resolved.recommended_allow_memory_lock &&
                resolved.recommended_allow_memory_map,
            "resolve() with evidence did not select resident/full and keep "
            "allow_memory_map/allow_memory_lock consistent with it");

    const auto no_evidence = calibration.resolve(model_sha256, "balanced");
    require(no_evidence.recommended_load_mode == masterai::ModelLoadMode::mapped &&
                no_evidence.recommended_pre_touch == masterai::PreTouchLevel::none,
            "resolve() without evidence (the pre-Phase-26 2-arg call) changed "
            "its default output");
}

// GPU-layer offload selection: select_gpu_layers() only ever recommends
// "offload everything" or "offload nothing" -- see the declaration in
// masterai.hpp for why a partial layer count cannot honestly be computed
// from a total model size alone -- and only when a compatible GPU backend
// and a real (non-zero) VRAM size are both known.
void test_gpu_layer_selection() {
    masterai::HardwareInfo no_gpu;
    require(masterai::select_gpu_layers(no_gpu, "", 4ULL * 1024 * 1024 * 1024) ==
                0U,
            "a host with no detected GPU backend recommended GPU offload");

    masterai::HardwareInfo cuda_host;
    cuda_host.gpu_backends = {"cuda"};
    cuda_host.gpu_memory_mib = 24U * 1024U;  // 24 GiB
    require(masterai::select_gpu_layers(cuda_host, "", 0U) == 0U,
            "an unknown model size did not fall back to no GPU offload");
    require(masterai::select_gpu_layers(cuda_host, "", 4ULL * 1024 * 1024 * 1024) ==
                masterai::kGpuLayersOffloadAll,
            "a model that comfortably fits in detected VRAM was not "
            "recommended for full GPU offload");
    require(masterai::select_gpu_layers(cuda_host, "",
                                        64ULL * 1024 * 1024 * 1024) == 0U,
            "a model far larger than detected VRAM was still recommended "
            "for GPU offload");
    require(masterai::select_gpu_layers(cuda_host, "vulkan",
                                        4ULL * 1024 * 1024 * 1024) == 0U,
            "a model requiring a GPU backend the host does not have was "
            "still recommended for GPU offload");

    masterai::HardwareInfo unknown_vram_host;
    unknown_vram_host.gpu_backends = {"cuda"};
    require(masterai::select_gpu_layers(unknown_vram_host, "",
                                        1ULL * 1024 * 1024 * 1024) == 0U,
            "an unknown (zero) VRAM size did not fall back to no GPU offload");
}

// Phase 26: WarmModelState legal graph, illegal-transition rejection, the
// RunnerState -> WarmModelState baseline translation table, and idle-timeout
// bookkeeping, all driven directly against WarmModelTracker.
void test_phase_twentysix_warm_state_transitions() {
    require(masterai::translate_runner_state(masterai::RunnerState::unloaded) ==
                    masterai::WarmModelState::Unloaded &&
                masterai::translate_runner_state(masterai::RunnerState::starting) ==
                    masterai::WarmModelState::MappingWeights &&
                masterai::translate_runner_state(masterai::RunnerState::ready) ==
                    masterai::WarmModelState::Ready &&
                masterai::translate_runner_state(masterai::RunnerState::busy) ==
                    masterai::WarmModelState::Busy &&
                masterai::translate_runner_state(masterai::RunnerState::stopping) ==
                    masterai::WarmModelState::Draining &&
                masterai::translate_runner_state(masterai::RunnerState::failed) ==
                    masterai::WarmModelState::Failed,
            "RunnerState -> WarmModelState baseline translation table drifted");

    masterai::WarmModelTracker tracker;
    require(tracker.current() == masterai::WarmModelState::Cold,
            "a fresh WarmModelTracker did not start Cold");
    tracker.enter(masterai::WarmModelState::LoadingMetadata);
    tracker.enter(masterai::WarmModelState::MappingWeights);
    tracker.enter(masterai::WarmModelState::InitialisingBackend);
    tracker.enter(masterai::WarmModelState::Warming);
    tracker.enter(masterai::WarmModelState::Ready);
    tracker.record_activity(1000U);
    tracker.enter(masterai::WarmModelState::Busy);
    tracker.enter(masterai::WarmModelState::Ready);

    require(!tracker.apply_idle_timeout(1000U + 599U, 600U) &&
                tracker.current() == masterai::WarmModelState::Ready,
            "idle timeout fired before the configured threshold elapsed");
    require(tracker.apply_idle_timeout(1000U + 600U, 600U) &&
                tracker.current() == masterai::WarmModelState::Idle,
            "idle timeout did not fire once the threshold elapsed");

    tracker.enter(masterai::WarmModelState::Busy);  // a request wakes it up
    tracker.enter(masterai::WarmModelState::Draining);
    tracker.enter(masterai::WarmModelState::Evicting);
    tracker.enter(masterai::WarmModelState::Unloaded);
    tracker.enter(masterai::WarmModelState::LoadingMetadata);  // reload

    masterai::WarmModelTracker illegal;
    bool rejected = false;
    try {
        illegal.enter(masterai::WarmModelState::Ready);  // skips the pipeline
    } catch (const std::logic_error&) {
        rejected = true;
    }
    require(rejected, "an illegal warm-state transition (Cold -> Ready) was accepted");
    require(illegal.current() == masterai::WarmModelState::Cold,
            "a rejected transition still mutated the tracker's state");
    require(masterai::warm_state_transition_allowed(
                masterai::WarmModelState::Ready, masterai::WarmModelState::Ready),
            "a self-transition was not treated as legal");
}

// Phase 26 regression guard: every existing RunnerState-based check (the
// same pattern server.cpp's ensure_model_loaded() and the Phase 4/19 tests
// already use) must keep behaving identically once WarmModelTracker is
// wired into RunnerSupervisor's load()/generate()/unload() -- this also
// exercises apply_idle_timeout() through the real supervisor, showing idle
// unload is predictable and never disturbs RunnerState.
void test_phase_twentysix_runner_state_regression() {
    TemporaryDirectory temporary;
    const auto model_directory = temporary.path() / "model";
    std::filesystem::create_directories(model_directory);
    const auto model_file = model_directory / "model.gguf";
    write_text(model_file, "GGUF-phase26-fixture");
    masterai::ModelRecord model;
    model.directory = model_directory;
    model.manifest.id = "phase26-fixture";
    model.manifest.model_file = "model.gguf";
    model.manifest.model_size_bytes = std::filesystem::file_size(model_file);
    model.manifest.model_sha256 = masterai::sha256_file_hex(model_file);
    model.state = masterai::ModelState::ready;
    const auto backend = fake_llama_executable();
    require(std::filesystem::is_regular_file(backend),
            "fake llama runner fixture is unavailable");

    masterai::RunnerSupervisor supervisor(backend, temporary.path() / "runtime");
    require(supervisor.metrics().state == masterai::RunnerState::unloaded,
            "a fresh supervisor was not RunnerState::unloaded");
    require(supervisor.metrics().warm_state == masterai::WarmModelState::Cold,
            "a fresh supervisor was not WarmModelState::Cold");

    supervisor.load(model, 4096U, 18096U, 5U);
    require(supervisor.metrics().state == masterai::RunnerState::ready,
            "RunnerState regression: load() did not reach ready");
    require(supervisor.metrics().warm_state == masterai::WarmModelState::Ready,
            "warm_state did not reach Ready alongside RunnerState::ready");

    std::atomic_bool cancellation{false};
    const auto generated =
        supervisor.generate("return value", {}, {}, cancellation);
    require(generated.text == "return value;",
            "regression fixture generation produced unexpected text");
    require(supervisor.metrics().state == masterai::RunnerState::ready,
            "RunnerState regression: generate() left the state incorrect");
    require(supervisor.metrics().requests_completed == 1U,
            "RunnerState regression: completion metrics were not recorded");

    require(!supervisor.apply_idle_timeout(0U, 0U),
            "apply_idle_timeout fired against a now_epoch_seconds earlier "
            "than the last recorded activity");
    const auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    require(supervisor.apply_idle_timeout(now + 10000U, 1U),
            "idle timeout did not fire against a plausible future timestamp");
    require(supervisor.metrics().state == masterai::RunnerState::ready &&
                supervisor.metrics().warm_state == masterai::WarmModelState::Idle,
            "idle timeout must move warm_state without ever disturbing "
            "RunnerState, which the rest of the codebase still reads");

    supervisor.unload(0U);
    require(supervisor.metrics().state == masterai::RunnerState::unloaded,
            "RunnerState regression: unload() did not reach unloaded");
    require(supervisor.metrics().warm_state == masterai::WarmModelState::Unloaded,
            "warm_state did not reach Unloaded alongside RunnerState::unloaded");
}

// Phase 26: run_cancellable_warmup() yields on a token cancelled before or
// during the run (no further steps execute once cancellation is observed,
// so a caller's per-step resource acquisition never leaks past that point)
// and on sustained MemoryBudgetManager pressure, without ever needing a
// second worker-pool implementation.
void test_phase_twentysix_cancelled_warmup_releases_resources() {
    auto hardware = masterai::probe_hardware(std::filesystem::current_path());
    auto policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware, 512ULL * 1024ULL * 1024ULL);
    policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager memory(policy, hardware);

    masterai::WarmupCancellationToken pre_cancelled;
    pre_cancelled.cancel();
    int pre_cancelled_steps = 0;
    const auto outcome_a = masterai::run_cancellable_warmup(
        [&]() { ++pre_cancelled_steps; return false; }, pre_cancelled, memory);
    require(outcome_a == masterai::WarmupOutcome::cancelled &&
                pre_cancelled_steps == 0,
            "a pre-cancelled warm-up token still ran a step");

    masterai::WarmupCancellationToken token;
    std::vector<int> acquired;
    std::mutex acquired_mutex;
    std::thread canceller([&]() {
        while (true) {
            {
                std::lock_guard<std::mutex> lock(acquired_mutex);
                if (acquired.size() >= 3U) break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        token.cancel();
    });
    const auto outcome_b = masterai::run_cancellable_warmup(
        [&]() {
            {
                std::lock_guard<std::mutex> lock(acquired_mutex);
                acquired.push_back(static_cast<int>(acquired.size()));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return false;  // only cancellation/pressure end this run
        },
        token, memory, 1000U);
    canceller.join();
    require(outcome_b == masterai::WarmupOutcome::cancelled,
            "mid-run cancellation was not observed between steps");
    const auto acquired_at_stop = acquired.size();
    require(acquired_at_stop >= 3U, "the canceller never observed any acquisitions");
    // Simulates the caller's cleanup of whatever the cancelled steps
    // acquired -- nothing outstanding remains, and (since the loop only
    // checks between steps, never mid-step) no further acquisition raced
    // past the observed cancellation.
    acquired.clear();
    require(acquired.empty(), "cancelled warm-up left resources unreleased");

    auto pressured_policy = masterai::MemoryBudgetManager::policy_for(
        masterai::ResourceProfile::minimal, hardware, 512ULL * 1024ULL * 1024ULL);
    pressured_policy.minimum_os_reserve_bytes = 1U;
    masterai::MemoryBudgetManager pressured(pressured_policy, hardware);
    masterai::MemoryEstimate estimate;
    // 470 MiB of 512 MiB (~92%) crosses this policy's default critical_percent
    // (92) as well as high_percent (82), so permits_background_work() (which
    // only requires pressure < high) is reliably false regardless of the
    // host's real available RAM.
    estimate.weights_bytes = 470ULL * 1024ULL * 1024ULL;
    const auto admitted = pressured.reserve(
        masterai::MemoryCategory::runner_weights, estimate, true);
    require(admitted.admitted, "test setup failed to reserve toward pressure");
    pressured.sample();
    require(!pressured.permits_background_work(),
            "test setup did not reach a pressure level that blocks "
            "background work");

    masterai::WarmupCancellationToken uncancelled;
    int pressured_steps = 0;
    const auto outcome_c = masterai::run_cancellable_warmup(
        [&]() { ++pressured_steps; return false; }, uncancelled, pressured);
    require(outcome_c == masterai::WarmupOutcome::skipped_low_memory &&
                pressured_steps == 0,
            "warm-up did not yield under sustained memory pressure");
}

// Phase 26: use-prediction signals (recency, pin, project-preference count,
// waiting-request count) accumulate as plain recorded evidence and are
// reportable through the same "_json() free function" convention as
// tuning_profile_json(), not a new opaque ranking surface.
void test_phase_twentysix_use_prediction_signals() {
    masterai::ModelUsagePredictor predictor;
    require(predictor.snapshot().empty(), "a fresh predictor was not empty");
    predictor.record_use("model-a", 1000U);
    predictor.record_use("model-a", 2000U);
    predictor.set_pinned("model-a", true);
    predictor.set_waiting_request_count("model-a", 3U);
    predictor.record_use("model-b", 1500U);

    const auto snapshot = predictor.snapshot();
    require(snapshot.size() == 2U, "predictor did not track both recorded models");
    const auto model_a = std::find_if(
        snapshot.begin(), snapshot.end(),
        [](const masterai::ModelUsageSignals& signals) {
            return signals.model_id == "model-a";
        });
    require(model_a != snapshot.end() && model_a->last_used_epoch_seconds == 2000U &&
                model_a->pinned && model_a->project_preference_score == 2U &&
                model_a->waiting_request_count == 3U,
            "use-prediction signals did not accumulate recency/pin/preference/"
            "waiting evidence");

    const auto json = masterai::model_usage_signals_json(snapshot);
    require(json.find("\"modelId\":\"model-a\"") != std::string::npos &&
                json.find("\"pinned\":true") != std::string::npos &&
                json.find("\"waitingRequestCount\":3") != std::string::npos,
            "model_usage_signals_json did not report the recorded signals");
}

// Machine Learning foundation phase: the registry must report a real,
// honest empty state (every count zero, only Dashboard "available") rather
// than fabricating activity, and the permission it's gated behind must be
// administrator-only -- see MachineLearningRegistry's class comment in
// masterai.hpp and docs/PLAN.md "Machine Learning Abilities" section 3.
void test_machine_learning_foundation_dashboard() {
    require(masterai::role_allows(masterai::UserRole::administrator,
                                  "ml.dashboard.view") &&
                !masterai::role_allows(masterai::UserRole::developer,
                                       "ml.dashboard.view") &&
                !masterai::role_allows(masterai::UserRole::viewer,
                                       "ml.dashboard.view"),
            "ml.dashboard.view must be administrator-only");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::MLProjectStore empty_projects(records);
    masterai::ModelRegistryStore empty_models(records);
    masterai::TrainingJobStore empty_training_jobs(records);
    const masterai::MachineLearningRegistry registry;
    const auto dashboard =
        registry.dashboard(empty_projects, empty_models, empty_training_jobs);
    require(dashboard.enabled, "the Machine Learning module did not report enabled");
    require(dashboard.active_projects == 0U && dashboard.models_training == 0U &&
                dashboard.models_awaiting_evaluation == 0U &&
                dashboard.models_awaiting_approval == 0U &&
                dashboard.deployed_models == 0U &&
                dashboard.failed_training_jobs == 0U,
            "foundation-phase dashboard counts must start real and zero, not "
            "fabricated");
    require(!dashboard.interfaces.empty(),
            "the Machine Learning interface roadmap was empty");
    const auto dashboard_entry = std::find_if(
        dashboard.interfaces.begin(), dashboard.interfaces.end(),
        [](const masterai::MachineLearningInterface& interface) {
            return interface.key == "dashboard";
        });
    require(dashboard_entry != dashboard.interfaces.end() &&
                dashboard_entry->status == "available",
            "the Dashboard interface must report available");
    // Dashboard (Phase 37), Projects (Phase 38), Model Registry / Dataset
    // Manager (Phase 39), Subject Knowledge Manager (Phase 40), Data
    // Labeling / Data Preparation (Phase 41), and Training Jobs (Phase 42)
    // are the only interfaces with a real backing service so far; every
    // other roadmap entry from docs/PLAN.md "Machine Learning Abilities"
    // section 2 must still report planned rather than fabricating readiness
    // ahead of its own phase.
    for (const auto& interface : dashboard.interfaces) {
        if (interface.key == "dashboard" || interface.key == "projects" ||
            interface.key == "model-registry" ||
            interface.key == "dataset-manager" ||
            interface.key == "subject-knowledge" ||
            interface.key == "data-labeling" ||
            interface.key == "data-preparation" ||
            interface.key == "training-jobs") {
            continue;
        }
        require(interface.status == "planned",
                "every interface without its own phase must still report "
                "planned");
    }

    const auto json = masterai::machine_learning_dashboard_json(dashboard);
    require(json.find("\"enabled\":true") != std::string::npos &&
                json.find("\"activeProjects\":0") != std::string::npos &&
                json.find("\"key\":\"dashboard\"") != std::string::npos &&
                json.find("\"status\":\"available\"") != std::string::npos,
            "machine_learning_dashboard_json did not report the dashboard "
            "state");
}

// Phase 38: MLProjectStore must persist real projects (surviving a reload
// from the same RecordStore, matching every other store in this codebase),
// start every new project at draft, let an administrator move it through
// status, and feed a truthful active-project count into the dashboard --
// see MLProjectStore's class comment in masterai.hpp.
void test_machine_learning_projects_lifecycle() {
    require(masterai::role_allows(masterai::UserRole::administrator,
                                  "ml.projects.create") &&
                !masterai::role_allows(masterai::UserRole::developer,
                                       "ml.projects.create") &&
                !masterai::role_allows(masterai::UserRole::viewer,
                                       "ml.projects.view"),
            "ml.projects.* permissions must be administrator-only");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::MLProjectStore projects(records);
    const auto project = projects.create(
        "administrator-1", "C++ code analysis model",
        "Reviews C++ diffs for defects.", "Reduce review turnaround time.",
        "software engineering", "code-review");
    require(!project.id.empty() && project.status == masterai::MLProjectStatus::draft &&
                project.owner_id == "administrator-1",
            "a newly created ML project must start at draft with its owner "
            "recorded");
    require(projects.list().size() == 1U,
            "the created project was not visible in list()");

    require(projects.set_status(project.id, masterai::MLProjectStatus::training),
            "set_status() rejected a known project id");
    require(projects.find(project.id)->status == masterai::MLProjectStatus::training,
            "set_status() did not persist the new status");
    require(!projects.set_status("nonexistent-project",
                                 masterai::MLProjectStatus::training),
            "set_status() must no-op for an unknown project id, not throw");

    // A fresh MLProjectStore over the same RecordStore must see the same
    // project after the process restarts -- restore() is what makes that
    // true, mirroring ChatStore's own reload guarantee.
    masterai::MLProjectStore reloaded(records);
    const auto reloaded_project = reloaded.find(project.id);
    require(reloaded_project.has_value() &&
                reloaded_project->name == "C++ code analysis model" &&
                reloaded_project->status == masterai::MLProjectStatus::training,
            "MLProjectStore did not restore a persisted project after reload");

    const masterai::MachineLearningRegistry registry;
    masterai::ModelRegistryStore models(records);
    masterai::TrainingJobStore training_jobs(records);
    require(registry.dashboard(reloaded, models, training_jobs)
                    .active_projects == 1U,
            "the dashboard's active-project count did not reflect a real, "
            "non-archived project");

    require(projects.set_status(project.id, masterai::MLProjectStatus::archived),
            "set_status() rejected archiving a known project id");
    require(registry.dashboard(projects, models, training_jobs)
                    .active_projects == 0U,
            "an archived project must not count as active");

    require(projects.remove(project.id), "remove() rejected a known project id");
    require(projects.list().empty(), "remove() did not delete the project");
    require(!projects.remove(project.id),
            "remove() must no-op for an already-removed project id, not throw");

    const auto json = masterai::ml_project_json(project);
    require(json.find("\"name\":\"C++ code analysis model\"") != std::string::npos &&
                json.find("\"status\":\"draft\"") != std::string::npos,
            "ml_project_json did not report the project's own fields");
}

// Phase 39: ModelRegistryStore must persist entries (surviving a reload),
// start every new entry at imported, let an administrator move it through
// states, refuse to jump straight to production without an approved/staging
// entry (docs/PLAN.md "Machine Learning Abilities" section 7's rule), and
// feed real training/evaluation/production counts into the dashboard.
void test_machine_learning_model_registry_lifecycle() {
    require(masterai::role_allows(masterai::UserRole::administrator,
                                  "ml.models.import") &&
                !masterai::role_allows(masterai::UserRole::developer,
                                       "ml.models.import") &&
                !masterai::role_allows(masterai::UserRole::viewer,
                                       "ml.models.view"),
            "ml.models.* permissions must be administrator-only");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::ModelRegistryStore models(records);
    const auto entry = models.create("administrator-1", "cpp-review-model",
                                     "C++ Review Model", "0.1.0", "llama",
                                     "code-review", "gguf", "internal",
                                     "MIT");
    require(!entry.id.empty() &&
                entry.state == masterai::ModelRegistryState::imported &&
                entry.owner_id == "administrator-1",
            "a newly created model registry entry must start at imported "
            "with its owner recorded");
    require(models.list().size() == 1U,
            "the created model was not visible in list()");

    bool production_rejected = false;
    try {
        models.set_state(entry.id, masterai::ModelRegistryState::production);
    } catch (const std::exception&) {
        production_rejected = true;
    }
    require(production_rejected,
            "set_state() must reject production before approval");
    require(models.find(entry.id)->state == masterai::ModelRegistryState::imported,
            "a rejected state transition must not have mutated the entry");

    require(models.set_state(entry.id, masterai::ModelRegistryState::training),
            "set_state() rejected a known model id");
    require(models.set_state(entry.id, masterai::ModelRegistryState::evaluation),
            "set_state() rejected a known transition to evaluation");
    require(models.set_state(entry.id, masterai::ModelRegistryState::approved),
            "set_state() rejected a known transition to approved");
    require(models.set_state(entry.id, masterai::ModelRegistryState::production),
            "set_state() rejected production after approval");
    require(!models.set_state("nonexistent-model",
                              masterai::ModelRegistryState::approved),
            "set_state() must no-op for an unknown model id, not throw");

    masterai::ModelRegistryStore reloaded(records);
    const auto reloaded_entry = reloaded.find(entry.id);
    require(reloaded_entry.has_value() &&
                reloaded_entry->name == "cpp-review-model" &&
                reloaded_entry->state == masterai::ModelRegistryState::production,
            "ModelRegistryStore did not restore a persisted entry after "
            "reload");

    masterai::MLProjectStore projects(records);
    masterai::TrainingJobStore training_jobs(records);
    const masterai::MachineLearningRegistry registry;
    require(registry.dashboard(projects, reloaded, training_jobs)
                    .deployed_models == 1U,
            "the dashboard's deployed-model count did not reflect a real "
            "production entry");

    require(models.remove(entry.id), "remove() rejected a known model id");
    require(models.list().empty(), "remove() did not delete the model");
    require(!models.remove(entry.id),
            "remove() must no-op for an already-removed model id, not throw");

    const auto json = masterai::model_registry_entry_json(entry);
    require(json.find("\"name\":\"cpp-review-model\"") != std::string::npos &&
                json.find("\"state\":\"imported\"") != std::string::npos,
            "model_registry_entry_json did not report the entry's own "
            "fields");
}

// Phase 39: DatasetStore must persist datasets (surviving a reload), start
// every new dataset at pending approval, let an administrator approve or
// reject it, and report those fields in JSON -- see docs/PLAN.md "Machine
// Learning Abilities" section 10.
void test_machine_learning_dataset_manager_lifecycle() {
    require(masterai::role_allows(masterai::UserRole::administrator,
                                  "ml.datasets.import") &&
                !masterai::role_allows(masterai::UserRole::developer,
                                       "ml.datasets.import") &&
                !masterai::role_allows(masterai::UserRole::viewer,
                                       "ml.datasets.view"),
            "ml.datasets.* permissions must be administrator-only");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::DatasetStore datasets(records);
    const auto dataset = datasets.create(
        "administrator-1", "cpp-review-transcripts",
        "Support transcripts about C++ code review.", "software engineering",
        "internal support tickets", "internal-only", "jsonl");
    require(!dataset.id.empty() &&
                dataset.approval_status == masterai::DatasetApprovalStatus::pending &&
                dataset.owner_id == "administrator-1",
            "a newly created dataset must start pending with its owner "
            "recorded");
    require(datasets.list().size() == 1U,
            "the created dataset was not visible in list()");

    require(datasets.set_approval_status(
                dataset.id, masterai::DatasetApprovalStatus::approved),
            "set_approval_status() rejected a known dataset id");
    require(datasets.find(dataset.id)->approval_status ==
                masterai::DatasetApprovalStatus::approved,
            "set_approval_status() did not persist the new status");
    require(!datasets.set_approval_status(
                "nonexistent-dataset", masterai::DatasetApprovalStatus::approved),
            "set_approval_status() must no-op for an unknown dataset id, "
            "not throw");

    masterai::DatasetStore reloaded(records);
    const auto reloaded_dataset = reloaded.find(dataset.id);
    require(reloaded_dataset.has_value() &&
                reloaded_dataset->name == "cpp-review-transcripts" &&
                reloaded_dataset->approval_status ==
                    masterai::DatasetApprovalStatus::approved,
            "DatasetStore did not restore a persisted dataset after reload");

    require(datasets.remove(dataset.id), "remove() rejected a known dataset id");
    require(datasets.list().empty(), "remove() did not delete the dataset");
    require(!datasets.remove(dataset.id),
            "remove() must no-op for an already-removed dataset id, not "
            "throw");

    const auto json = masterai::dataset_json(dataset);
    require(json.find("\"name\":\"cpp-review-transcripts\"") != std::string::npos &&
                json.find("\"approvalStatus\":\"pending\"") != std::string::npos,
            "dataset_json did not report the dataset's own fields");
}

void test_machine_learning_subject_knowledge_manager_lifecycle() {
    require(masterai::role_allows(masterai::UserRole::administrator,
                                  "ml.subjects.create") &&
                !masterai::role_allows(masterai::UserRole::developer,
                                       "ml.subjects.create") &&
                !masterai::role_allows(masterai::UserRole::viewer,
                                       "ml.subjects.view"),
            "ml.subjects.* permissions must be administrator-only");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::SubjectPackageStore subjects(records);
    const auto package = subjects.create(
        "administrator-1", "cpp17-programming",
        "Core C++17 language and standard library.",
        "Language features and stdlib; excludes build systems.",
        "internal engineers");
    require(!package.id.empty() &&
                package.review_status == masterai::SubjectReviewStatus::draft &&
                package.owner_id == "administrator-1",
            "a newly created subject package must start draft with its "
            "owner recorded");
    require(subjects.list().size() == 1U,
            "the created subject package was not visible in list()");

    require(subjects.set_review_status(
                package.id, masterai::SubjectReviewStatus::in_review),
            "set_review_status() rejected a known subject package id");
    require(subjects.find(package.id)->review_status ==
                masterai::SubjectReviewStatus::in_review,
            "set_review_status() did not persist the new status");
    require(!subjects.set_review_status(
                "nonexistent-subject", masterai::SubjectReviewStatus::approved),
            "set_review_status() must no-op for an unknown subject package "
            "id, not throw");

    masterai::SubjectPackageStore reloaded(records);
    const auto reloaded_package = reloaded.find(package.id);
    require(reloaded_package.has_value() &&
                reloaded_package->name == "cpp17-programming" &&
                reloaded_package->review_status ==
                    masterai::SubjectReviewStatus::in_review,
            "SubjectPackageStore did not restore a persisted subject "
            "package after reload");

    require(subjects.remove(package.id),
            "remove() rejected a known subject package id");
    require(subjects.list().empty(), "remove() did not delete the subject package");
    require(!subjects.remove(package.id),
            "remove() must no-op for an already-removed subject package id, "
            "not throw");

    const auto json = masterai::subject_package_json(package);
    require(json.find("\"name\":\"cpp17-programming\"") != std::string::npos &&
                json.find("\"reviewStatus\":\"draft\"") != std::string::npos,
            "subject_package_json did not report the package's own fields");
}

// Phase 41: LabelTaskStore must persist labeling tasks (surviving a
// reload), start every new task at queued against the given dataset, let an
// administrator move it through status, and report those fields in JSON --
// see docs/PLAN.md "Machine Learning Abilities" section 14.
void test_machine_learning_data_labeling_lifecycle() {
    require(masterai::role_allows(masterai::UserRole::administrator,
                                  "ml.labels.manage") &&
                !masterai::role_allows(masterai::UserRole::developer,
                                       "ml.labels.manage") &&
                !masterai::role_allows(masterai::UserRole::viewer,
                                       "ml.labels.view"),
            "ml.labels.* permissions must be administrator-only");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::LabelTaskStore tasks(records);
    const auto task = tasks.create(
        "administrator-1", "dataset-1", "cpp-transcripts-labeling",
        "Classify transcripts by support category.", "text_category",
        "reviewer-1");
    require(!task.id.empty() &&
                task.dataset_id == "dataset-1" &&
                task.status == masterai::LabelTaskStatus::queued &&
                task.owner_id == "administrator-1",
            "a newly created label task must start queued against its "
            "dataset with its owner recorded");
    require(tasks.list().size() == 1U,
            "the created label task was not visible in list()");

    bool rejected_empty_dataset = false;
    try {
        tasks.create("administrator-1", "", "no dataset", "", "", "");
    } catch (const std::invalid_argument&) {
        rejected_empty_dataset = true;
    }
    require(rejected_empty_dataset,
            "create() must reject a label task with no target dataset id");

    require(tasks.set_status(task.id, masterai::LabelTaskStatus::in_progress),
            "set_status() rejected a known label task id");
    require(tasks.find(task.id)->status ==
                masterai::LabelTaskStatus::in_progress,
            "set_status() did not persist the new status");
    require(!tasks.set_status("nonexistent-task",
                              masterai::LabelTaskStatus::completed),
            "set_status() must no-op for an unknown label task id, not "
            "throw");

    masterai::LabelTaskStore reloaded(records);
    const auto reloaded_task = reloaded.find(task.id);
    require(reloaded_task.has_value() &&
                reloaded_task->name == "cpp-transcripts-labeling" &&
                reloaded_task->status == masterai::LabelTaskStatus::in_progress,
            "LabelTaskStore did not restore a persisted label task after "
            "reload");

    require(tasks.remove(task.id), "remove() rejected a known label task id");
    require(tasks.list().empty(), "remove() did not delete the label task");
    require(!tasks.remove(task.id),
            "remove() must no-op for an already-removed label task id, not "
            "throw");

    const auto json = masterai::label_task_json(task);
    require(json.find("\"name\":\"cpp-transcripts-labeling\"") != std::string::npos &&
                json.find("\"status\":\"queued\"") != std::string::npos,
            "label_task_json did not report the task's own fields");
}

// Phase 41: DataPreparationJobStore must persist jobs (surviving a reload),
// start every new job at pending against the given dataset, let an
// administrator move it through status, and report those fields in JSON --
// see docs/PLAN.md "Machine Learning Abilities" section 15.
void test_machine_learning_data_preparation_lifecycle() {
    require(masterai::role_allows(masterai::UserRole::administrator,
                                  "ml.dataprep.manage") &&
                !masterai::role_allows(masterai::UserRole::developer,
                                       "ml.dataprep.manage") &&
                !masterai::role_allows(masterai::UserRole::viewer,
                                       "ml.dataprep.view"),
            "ml.dataprep.* permissions must be administrator-only");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::DataPreparationJobStore jobs(records);
    const auto job = jobs.create(
        "administrator-1", "dataset-1", "dedupe-transcripts",
        "Remove duplicate transcript records.", "remove_duplicates");
    require(!job.id.empty() &&
                job.dataset_id == "dataset-1" &&
                job.status == masterai::DataPreparationJobStatus::pending &&
                job.owner_id == "administrator-1",
            "a newly created preparation job must start pending against its "
            "dataset with its owner recorded");
    require(jobs.list().size() == 1U,
            "the created preparation job was not visible in list()");

    bool rejected_empty_dataset = false;
    try {
        jobs.create("administrator-1", "", "no dataset", "", "op");
    } catch (const std::invalid_argument&) {
        rejected_empty_dataset = true;
    }
    require(rejected_empty_dataset,
            "create() must reject a preparation job with no target dataset "
            "id");

    require(jobs.set_status(job.id,
                            masterai::DataPreparationJobStatus::running),
            "set_status() rejected a known preparation job id");
    require(jobs.find(job.id)->status ==
                masterai::DataPreparationJobStatus::running,
            "set_status() did not persist the new status");
    require(!jobs.set_status("nonexistent-job",
                             masterai::DataPreparationJobStatus::completed),
            "set_status() must no-op for an unknown preparation job id, not "
            "throw");

    masterai::DataPreparationJobStore reloaded(records);
    const auto reloaded_job = reloaded.find(job.id);
    require(reloaded_job.has_value() &&
                reloaded_job->name == "dedupe-transcripts" &&
                reloaded_job->status ==
                    masterai::DataPreparationJobStatus::running,
            "DataPreparationJobStore did not restore a persisted "
            "preparation job after reload");

    require(jobs.remove(job.id),
            "remove() rejected a known preparation job id");
    require(jobs.list().empty(), "remove() did not delete the preparation job");
    require(!jobs.remove(job.id),
            "remove() must no-op for an already-removed preparation job id, "
            "not throw");

    const auto json = masterai::data_preparation_job_json(job);
    require(json.find("\"name\":\"dedupe-transcripts\"") != std::string::npos &&
                json.find("\"status\":\"pending\"") != std::string::npos,
            "data_preparation_job_json did not report the job's own "
            "fields");
}

// Phase 42: TrainingJobStore must persist training jobs (surviving a
// reload), start every new job at draft against the given project and
// dataset, let an administrator move it through status, and report those
// fields in JSON -- see docs/PLAN.md "Machine Learning Abilities" section
// 16. Also verifies the dashboard's failedTrainingJobs count now reflects
// real training jobs instead of always reporting zero.
void test_machine_learning_training_jobs_lifecycle() {
    require(masterai::role_allows(masterai::UserRole::administrator,
                                  "ml.training.manage") &&
                !masterai::role_allows(masterai::UserRole::developer,
                                       "ml.training.manage") &&
                !masterai::role_allows(masterai::UserRole::viewer,
                                       "ml.training.view"),
            "ml.training.* permissions must be administrator-only");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::TrainingJobStore jobs(records);
    const auto job = jobs.create(
        "administrator-1", "project-1", "model-1", "dataset-1",
        "cpp-review-finetune", "Fine-tune on C++ review transcripts.",
        "fine_tuning");
    require(!job.id.empty() &&
                job.project_id == "project-1" &&
                job.dataset_id == "dataset-1" &&
                job.status == masterai::TrainingJobStatus::draft &&
                job.owner_id == "administrator-1",
            "a newly created training job must start draft against its "
            "project and dataset with its owner recorded");
    require(jobs.list().size() == 1U,
            "the created training job was not visible in list()");

    bool rejected_empty_project = false;
    try {
        jobs.create("administrator-1", "", "model-1", "dataset-1",
                   "no project", "", "");
    } catch (const std::invalid_argument&) {
        rejected_empty_project = true;
    }
    require(rejected_empty_project,
            "create() must reject a training job with no target project id");

    bool rejected_empty_dataset = false;
    try {
        jobs.create("administrator-1", "project-1", "model-1", "",
                   "no dataset", "", "");
    } catch (const std::invalid_argument&) {
        rejected_empty_dataset = true;
    }
    require(rejected_empty_dataset,
            "create() must reject a training job with no target dataset id");

    require(jobs.set_status(job.id, masterai::TrainingJobStatus::running),
            "set_status() rejected a known training job id");
    require(jobs.find(job.id)->status == masterai::TrainingJobStatus::running,
            "set_status() did not persist the new status");
    require(!jobs.set_status("nonexistent-job",
                             masterai::TrainingJobStatus::completed),
            "set_status() must no-op for an unknown training job id, not "
            "throw");

    masterai::TrainingJobStore reloaded(records);
    const auto reloaded_job = reloaded.find(job.id);
    require(reloaded_job.has_value() &&
                reloaded_job->name == "cpp-review-finetune" &&
                reloaded_job->status == masterai::TrainingJobStatus::running,
            "TrainingJobStore did not restore a persisted training job "
            "after reload");

    require(jobs.set_status(job.id, masterai::TrainingJobStatus::failed),
            "set_status() rejected moving a known training job to failed");
    masterai::MLProjectStore projects(records);
    masterai::ModelRegistryStore models(records);
    const auto dashboard =
        masterai::MachineLearningRegistry().dashboard(projects, models, jobs);
    require(dashboard.failed_training_jobs == 1U,
            "dashboard() did not count a failed training job");

    require(jobs.remove(job.id),
            "remove() rejected a known training job id");
    require(jobs.list().empty(), "remove() did not delete the training job");
    require(!jobs.remove(job.id),
            "remove() must no-op for an already-removed training job id, "
            "not throw");

    const auto json = masterai::training_job_json(job);
    require(json.find("\"name\":\"cpp-review-finetune\"") != std::string::npos &&
                json.find("\"trainingType\":\"fine_tuning\"") != std::string::npos,
            "training_job_json did not report the job's own fields");
}

// Phase 43: EvaluationRunStore must persist evaluation runs (surviving a
// reload), start every new run at queued against the given model and
// benchmark dataset, let an administrator move it through status, and
// report those fields in JSON -- see docs/PLAN.md "Machine Learning
// Abilities" section 23.
void test_machine_learning_evaluation_lab_lifecycle() {
    require(masterai::role_allows(masterai::UserRole::administrator,
                                  "ml.evaluation.manage") &&
                !masterai::role_allows(masterai::UserRole::developer,
                                       "ml.evaluation.manage") &&
                !masterai::role_allows(masterai::UserRole::viewer,
                                       "ml.evaluation.view"),
            "ml.evaluation.* permissions must be administrator-only");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::EvaluationRunStore runs(records);
    const auto run = runs.create(
        "administrator-1", "model-1", "dataset-1",
        "cpp-review-accuracy", "Score the fine-tuned model's accuracy.",
        "accuracy");
    require(!run.id.empty() &&
                run.model_id == "model-1" &&
                run.dataset_id == "dataset-1" &&
                run.status == masterai::EvaluationRunStatus::queued &&
                run.owner_id == "administrator-1",
            "a newly created evaluation run must start queued against its "
            "model and dataset with its owner recorded");
    require(runs.list().size() == 1U,
            "the created evaluation run was not visible in list()");

    bool rejected_empty_model = false;
    try {
        runs.create("administrator-1", "", "dataset-1", "no model", "", "");
    } catch (const std::invalid_argument&) {
        rejected_empty_model = true;
    }
    require(rejected_empty_model,
            "create() must reject an evaluation run with no target model id");

    bool rejected_empty_dataset = false;
    try {
        runs.create("administrator-1", "model-1", "", "no dataset", "", "");
    } catch (const std::invalid_argument&) {
        rejected_empty_dataset = true;
    }
    require(rejected_empty_dataset,
            "create() must reject an evaluation run with no target dataset "
            "id");

    require(runs.set_status(run.id, masterai::EvaluationRunStatus::running),
            "set_status() rejected a known evaluation run id");
    require(runs.find(run.id)->status == masterai::EvaluationRunStatus::running,
            "set_status() did not persist the new status");
    require(!runs.set_status("nonexistent-run",
                             masterai::EvaluationRunStatus::completed),
            "set_status() must no-op for an unknown evaluation run id, not "
            "throw");

    masterai::EvaluationRunStore reloaded(records);
    const auto reloaded_run = reloaded.find(run.id);
    require(reloaded_run.has_value() &&
                reloaded_run->name == "cpp-review-accuracy" &&
                reloaded_run->status == masterai::EvaluationRunStatus::running,
            "EvaluationRunStore did not restore a persisted evaluation run "
            "after reload");

    require(runs.remove(run.id),
            "remove() rejected a known evaluation run id");
    require(runs.list().empty(), "remove() did not delete the evaluation run");
    require(!runs.remove(run.id),
            "remove() must no-op for an already-removed evaluation run id, "
            "not throw");

    const auto json = masterai::evaluation_run_json(run);
    require(json.find("\"name\":\"cpp-review-accuracy\"") != std::string::npos &&
                json.find("\"category\":\"accuracy\"") != std::string::npos,
            "evaluation_run_json did not report the run's own fields");
}

void test_machine_learning_experiment_tracking_lifecycle() {
    require(masterai::role_allows(masterai::UserRole::administrator,
                                  "ml.experiments.manage") &&
                !masterai::role_allows(masterai::UserRole::developer,
                                       "ml.experiments.manage") &&
                !masterai::role_allows(masterai::UserRole::viewer,
                                       "ml.experiments.view"),
            "ml.experiments.* permissions must be administrator-only");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::ExperimentStore experiments(records);
    const auto experiment = experiments.create(
        "administrator-1", "project-1", "model-1", "dataset-1",
        "lora-rank-sweep", "Compare LoRA rank 8 vs 16 on the fine-tune.");
    require(!experiment.id.empty() &&
                experiment.project_id == "project-1" &&
                experiment.model_id == "model-1" &&
                experiment.dataset_id == "dataset-1" &&
                experiment.status == masterai::ExperimentStatus::queued &&
                experiment.owner_id == "administrator-1",
            "a newly created experiment must start queued against its "
            "project and model with its owner recorded");
    require(experiments.list().size() == 1U,
            "the created experiment was not visible in list()");

    bool rejected_empty_project = false;
    try {
        experiments.create("administrator-1", "", "model-1", "dataset-1",
                           "no project", "");
    } catch (const std::invalid_argument&) {
        rejected_empty_project = true;
    }
    require(rejected_empty_project,
            "create() must reject an experiment with no target project id");

    bool rejected_empty_model = false;
    try {
        experiments.create("administrator-1", "project-1", "", "dataset-1",
                           "no model", "");
    } catch (const std::invalid_argument&) {
        rejected_empty_model = true;
    }
    require(rejected_empty_model,
            "create() must reject an experiment with no target model id");

    const auto no_dataset = experiments.create(
        "administrator-1", "project-1", "model-1", "", "no dataset needed",
        "");
    require(no_dataset.dataset_id.empty(),
            "create() must allow an experiment with no target dataset id");

    require(experiments.set_status(experiment.id,
                                   masterai::ExperimentStatus::running),
            "set_status() rejected a known experiment id");
    require(experiments.find(experiment.id)->status ==
                masterai::ExperimentStatus::running,
            "set_status() did not persist the new status");
    require(!experiments.set_status("nonexistent-experiment",
                                    masterai::ExperimentStatus::completed),
            "set_status() must no-op for an unknown experiment id, not "
            "throw");

    masterai::ExperimentStore reloaded(records);
    const auto reloaded_experiment = reloaded.find(experiment.id);
    require(reloaded_experiment.has_value() &&
                reloaded_experiment->name == "lora-rank-sweep" &&
                reloaded_experiment->status ==
                    masterai::ExperimentStatus::running,
            "ExperimentStore did not restore a persisted experiment after "
            "reload");

    require(experiments.remove(experiment.id),
            "remove() rejected a known experiment id");
    require(experiments.list().size() == 1U,
            "remove() did not delete the experiment");
    require(!experiments.remove(experiment.id),
            "remove() must no-op for an already-removed experiment id, not "
            "throw");

    const auto json = masterai::experiment_json(no_dataset);
    require(json.find("\"name\":\"no dataset needed\"") != std::string::npos &&
                json.find("\"datasetId\":\"\"") != std::string::npos,
            "experiment_json did not report the experiment's own fields");
}

void test_machine_learning_fine_tuning_lifecycle() {
    require(masterai::role_allows(masterai::UserRole::administrator,
                                  "ml.finetuning.manage") &&
                !masterai::role_allows(masterai::UserRole::developer,
                                       "ml.finetuning.manage") &&
                !masterai::role_allows(masterai::UserRole::viewer,
                                       "ml.finetuning.view"),
            "ml.finetuning.* permissions must be administrator-only");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::FineTuningJobStore jobs(records);
    const auto job = jobs.create("administrator-1", "project-1", "model-1",
                                 "dataset-1", "cpp-assistant-tune",
                                 "Adapt the base model for C++ code review.",
                                 "code_assistant");
    require(!job.id.empty() && job.project_id == "project-1" &&
                job.model_id == "model-1" && job.dataset_id == "dataset-1" &&
                job.method == "code_assistant" &&
                job.status == masterai::FineTuningJobStatus::draft &&
                job.owner_id == "administrator-1",
            "a newly created fine-tuning job must start draft against its "
            "base model and dataset with its owner recorded");
    require(jobs.list().size() == 1U,
            "the created fine-tuning job was not visible in list()");

    bool rejected_empty_model = false;
    try {
        jobs.create("administrator-1", "project-1", "", "dataset-1",
                   "no model", "", "");
    } catch (const std::invalid_argument&) {
        rejected_empty_model = true;
    }
    require(rejected_empty_model,
            "create() must reject a fine-tuning job with no base model id");

    bool rejected_empty_dataset = false;
    try {
        jobs.create("administrator-1", "project-1", "model-1", "",
                   "no dataset", "", "");
    } catch (const std::invalid_argument&) {
        rejected_empty_dataset = true;
    }
    require(rejected_empty_dataset,
            "create() must reject a fine-tuning job with no dataset id");

    const auto no_project = jobs.create("administrator-1", "", "model-1",
                                        "dataset-1", "no project needed", "",
                                        "");
    require(no_project.project_id.empty(),
            "create() must allow a fine-tuning job with no target project "
            "id");

    require(jobs.set_status(job.id, masterai::FineTuningJobStatus::running),
            "set_status() rejected a known fine-tuning job id");
    require(jobs.find(job.id)->status ==
                masterai::FineTuningJobStatus::running,
            "set_status() did not persist the new status");
    require(!jobs.set_status("nonexistent-fine-tuning-job",
                             masterai::FineTuningJobStatus::completed),
            "set_status() must no-op for an unknown fine-tuning job id, not "
            "throw");

    masterai::FineTuningJobStore reloaded(records);
    const auto reloaded_job = reloaded.find(job.id);
    require(reloaded_job.has_value() &&
                reloaded_job->name == "cpp-assistant-tune" &&
                reloaded_job->status == masterai::FineTuningJobStatus::running,
            "FineTuningJobStore did not restore a persisted fine-tuning job "
            "after reload");

    require(jobs.remove(job.id),
            "remove() rejected a known fine-tuning job id");
    require(jobs.list().size() == 1U,
            "remove() did not delete the fine-tuning job");
    require(!jobs.remove(job.id),
            "remove() must no-op for an already-removed fine-tuning job id, "
            "not throw");

    const auto json = masterai::fine_tuning_job_json(no_project);
    require(json.find("\"name\":\"no project needed\"") != std::string::npos &&
                json.find("\"projectId\":\"\"") != std::string::npos,
            "fine_tuning_job_json did not report the fine-tuning job's own "
            "fields");
}

void test_machine_learning_model_builder_lifecycle() {
    require(masterai::role_allows(masterai::UserRole::administrator,
                                  "ml.modelbuilder.manage") &&
                !masterai::role_allows(masterai::UserRole::developer,
                                       "ml.modelbuilder.manage") &&
                !masterai::role_allows(masterai::UserRole::viewer,
                                       "ml.modelbuilder.view"),
            "ml.modelbuilder.* permissions must be administrator-only");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::ModelBuilderConfigStore configs(records);
    const auto config = configs.create(
        "administrator-1", "project-1", "model-1", "cpp-assistant-build",
        "Adapt the base model into a new C++ assistant configuration.",
        "imported_base_model");
    require(!config.id.empty() && config.project_id == "project-1" &&
                config.base_model_id == "model-1" &&
                config.source_type == "imported_base_model" &&
                config.status == masterai::ModelBuilderConfigStatus::draft &&
                config.owner_id == "administrator-1",
            "a newly created model builder configuration must start draft "
            "against its project/base model with its owner recorded");
    require(configs.list().size() == 1U,
            "the created model builder configuration was not visible in "
            "list()");

    bool rejected_empty_source_type = false;
    try {
        configs.create("administrator-1", "project-1", "", "no source type",
                      "", "");
    } catch (const std::invalid_argument&) {
        rejected_empty_source_type = true;
    }
    require(rejected_empty_source_type,
            "create() must reject a model builder configuration with no "
            "source type");

    const auto no_project_or_base = configs.create(
        "administrator-1", "", "", "no project or base model needed", "",
        "template");
    require(no_project_or_base.project_id.empty() &&
                no_project_or_base.base_model_id.empty(),
            "create() must allow a model builder configuration with no "
            "target project id or base model id");

    require(configs.set_status(config.id,
                               masterai::ModelBuilderConfigStatus::ready),
            "set_status() rejected a known model builder configuration id");
    require(configs.find(config.id)->status ==
                masterai::ModelBuilderConfigStatus::ready,
            "set_status() did not persist the new status");
    require(!configs.set_status(
                "nonexistent-model-builder-config",
                masterai::ModelBuilderConfigStatus::submitted),
            "set_status() must no-op for an unknown model builder "
            "configuration id, not throw");

    masterai::ModelBuilderConfigStore reloaded(records);
    const auto reloaded_config = reloaded.find(config.id);
    require(reloaded_config.has_value() &&
                reloaded_config->name == "cpp-assistant-build" &&
                reloaded_config->status ==
                    masterai::ModelBuilderConfigStatus::ready,
            "ModelBuilderConfigStore did not restore a persisted model "
            "builder configuration after reload");

    require(configs.remove(config.id),
            "remove() rejected a known model builder configuration id");
    require(configs.list().size() == 1U,
            "remove() did not delete the model builder configuration");
    require(!configs.remove(config.id),
            "remove() must no-op for an already-removed model builder "
            "configuration id, not throw");

    const auto json = masterai::model_builder_config_json(no_project_or_base);
    require(json.find("\"name\":\"no project or base model needed\"") !=
                    std::string::npos &&
                json.find("\"projectId\":\"\"") != std::string::npos &&
                json.find("\"baseModelId\":\"\"") != std::string::npos,
            "model_builder_config_json did not report the model builder "
            "configuration's own fields");
}

void test_machine_learning_instruction_training_lifecycle() {
    require(masterai::role_allows(masterai::UserRole::administrator,
                                  "ml.instructions.manage") &&
                !masterai::role_allows(masterai::UserRole::developer,
                                       "ml.instructions.manage") &&
                !masterai::role_allows(masterai::UserRole::viewer,
                                       "ml.instructions.view"),
            "ml.instructions.* permissions must be administrator-only");

    TemporaryDirectory temporary;
    masterai::RecordStore records(temporary.path() / "database");
    records.open();
    masterai::InstructionExampleStore examples(records);
    const auto example = examples.create(
        "administrator-1", "dataset-1", "cpp-review-example",
        "Ask the model to review a C++ diff for undefined behavior.",
        "cpp_code_review");
    require(!example.id.empty() && example.dataset_id == "dataset-1" &&
                example.subject_classification == "cpp_code_review" &&
                example.status == masterai::InstructionExampleStatus::draft &&
                example.owner_id == "administrator-1",
            "a newly created instruction example must start draft against "
            "its target dataset with its owner recorded");
    require(examples.list().size() == 1U,
            "the created instruction example was not visible in list()");

    bool rejected_empty_dataset = false;
    try {
        examples.create("administrator-1", "", "no dataset", "", "");
    } catch (const std::invalid_argument&) {
        rejected_empty_dataset = true;
    }
    require(rejected_empty_dataset,
            "create() must reject an instruction example with no target "
            "dataset id");

    require(examples.set_status(example.id,
                                masterai::InstructionExampleStatus::approved),
            "set_status() rejected a known instruction example id");
    require(examples.find(example.id)->status ==
                masterai::InstructionExampleStatus::approved,
            "set_status() did not persist the new status");
    require(!examples.set_status(
                "nonexistent-instruction-example",
                masterai::InstructionExampleStatus::rejected),
            "set_status() must no-op for an unknown instruction example id, "
            "not throw");

    masterai::InstructionExampleStore reloaded(records);
    const auto reloaded_example = reloaded.find(example.id);
    require(reloaded_example.has_value() &&
                reloaded_example->name == "cpp-review-example" &&
                reloaded_example->status ==
                    masterai::InstructionExampleStatus::approved,
            "InstructionExampleStore did not restore a persisted "
            "instruction example after reload");

    require(examples.remove(example.id),
            "remove() rejected a known instruction example id");
    require(examples.list().size() == 0U,
            "remove() did not delete the instruction example");
    require(!examples.remove(example.id),
            "remove() must no-op for an already-removed instruction example "
            "id, not throw");

    const auto json = masterai::instruction_example_json(example);
    require(json.find("\"name\":\"cpp-review-example\"") != std::string::npos &&
                json.find("\"subjectClassification\":\"cpp_code_review\"") !=
                    std::string::npos,
            "instruction_example_json did not report the instruction "
            "example's own fields");
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
        run("hierarchical cache admission/eviction",
            test_phase_twentytwo_hierarchical_cache);
        run("prompt-prefix session reuse",
            test_phase_eighteen_prompt_session_reuse);
        run("adaptive calibration", test_phase_nineteen_calibration);
        run("cpu_only accelerator policy fail-closed launch",
            test_phase_thirty_a_cpu_only_accelerator_policy);
        run("runner_weights pre-load admission",
            test_phase_thirty_a_runner_weights_admission);
        run("MemorySweeper idle unload",
            test_phase_thirty_a_memory_sweeper_idle_unload);
        run("weighted-fair scheduler and backpressure",
            test_phase_twentyfive_scheduler_weighted_fairness_and_backpressure);
        run("KV-cache accounting and deterministic eviction",
            test_phase_twentyseven_kv_cache_accounting_and_eviction);
        run("topology probing and placement recommendation",
            test_phase_twentyeight_topology_and_placement_recommendation);
        run("model tiering, routing, and cascade inference",
            test_phase_twentynine_model_tiering_and_cascade);
        run("advanced optimizations gated",
            test_phase_twenty_advanced_optimizations_disabled);
        run("async storage coalescing", test_phase_twentyone_coalescing_merges_adjacent);
        run("async storage cancellation", test_phase_twentyone_cancellation_releases_buffer);
        run("async storage HDD queue depth", test_phase_twentyone_hdd_profile_stays_sequential);
        run("async storage blocking fallback", test_phase_twentyone_fallback_to_blocking_path);
        run("shared buffer view and refcount",
            test_phase_thirty_shared_buffer_view_and_refcount);
        run("mapped buffer view reads file", test_phase_thirty_mapped_buffer_view_reads_file);
        run("request arena allocation and poison",
            test_phase_thirty_request_arena_allocation_and_poison);
        run("fixed size pool alloc release shrink",
            test_phase_thirty_fixed_size_pool_alloc_release_shrink);
        run("fixed size pool registers with budget manager",
            test_phase_thirty_fixed_size_pool_registers_with_budget_manager);
        run("json_escape_bytes matches json_escape byte-for-byte",
            test_phase_thirty_json_escape_bytes_matches_string_escape);
        run("streaming escape moves into SharedBuffer without copy",
            test_phase_thirty_streaming_escape_moves_into_shared_buffer_without_copy);
        run("tokenization cache hit avoids retokenize",
            test_phase_twentythree_tokenization_cache_hit_avoids_retokenize);
        run("segmented prompt assembly byte-identical",
            test_phase_twentythree_segmented_assembly_byte_identical);
        run("session reuse reports prefix fields on success",
            test_phase_twentythree_session_reuse_reports_prefix_fields_on_success);
        run("session reuse reports divergence offset on edit",
            test_phase_twentythree_session_reuse_edit_reports_divergence_offset);
        run("session reuse prefix ceiling enforced",
            test_phase_twentythree_session_reuse_prefix_ceiling_enforced);
        run("intern table restricted to short identifiers",
            test_phase_twentythree_intern_table_restricted_to_short_identifiers);
        run("retrieval request classification",
            test_phase_twentyfour_request_classification);
        run("disabled retrieval strategies recorded on trace",
            test_phase_twentyfour_disabled_strategies_recorded_on_trace);
        run("staged fan-out skips later stages",
            test_phase_twentyfour_staged_fanout_skips_later_stages);
        run("duplicate chunks materialize once",
            test_phase_twentyfour_duplicate_chunks_materialize_once);
        run("concurrent identical retrieval requests join",
            test_phase_twentyfour_concurrent_identical_requests_join);
        run("mismatched auth does not join in-flight retrieval",
            test_phase_twentyfour_mismatched_auth_does_not_join);
        run("retrieval candidate pool registers with budget manager",
            test_phase_thirty_retrieval_candidate_pool_registers_with_budget_manager);
        run("concurrent retrieval leaves no dangling view or budget residue",
            test_phase_thirty_concurrent_retrieval_no_dangling_view);
        run("load-mode selection responds to storage/RAM evidence",
            test_phase_twentysix_load_mode_selection);
        run("GPU-layer offload selection", test_gpu_layer_selection);
        run("warm-state legal transition graph",
            test_phase_twentysix_warm_state_transitions);
        run("RunnerState regression under WarmModelTracker",
            test_phase_twentysix_runner_state_regression);
        run("cancelled warm-up releases resources",
            test_phase_twentysix_cancelled_warmup_releases_resources);
        run("use-prediction signals reporting",
            test_phase_twentysix_use_prediction_signals);
        run("Machine Learning foundation dashboard",
            test_machine_learning_foundation_dashboard);
        run("Machine Learning projects lifecycle",
            test_machine_learning_projects_lifecycle);
        run("Machine Learning model registry lifecycle",
            test_machine_learning_model_registry_lifecycle);
        run("Machine Learning dataset manager lifecycle",
            test_machine_learning_dataset_manager_lifecycle);
        run("Machine Learning subject knowledge manager lifecycle",
            test_machine_learning_subject_knowledge_manager_lifecycle);
        run("Machine Learning data labeling lifecycle",
            test_machine_learning_data_labeling_lifecycle);
        run("Machine Learning data preparation lifecycle",
            test_machine_learning_data_preparation_lifecycle);
        run("Machine Learning training jobs lifecycle",
            test_machine_learning_training_jobs_lifecycle);
        run("Machine Learning evaluation lab lifecycle",
            test_machine_learning_evaluation_lab_lifecycle);
        run("Machine Learning experiment tracking lifecycle",
            test_machine_learning_experiment_tracking_lifecycle);
        run("Machine Learning fine-tuning lifecycle",
            test_machine_learning_fine_tuning_lifecycle);
        run("Machine Learning model builder lifecycle",
            test_machine_learning_model_builder_lifecycle);
        run("Machine Learning prompt and instruction training lifecycle",
            test_machine_learning_instruction_training_lifecycle);
        std::cout << "MasterAI core tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "MasterAI core test failure: " << exception.what() << '\n';
        return 1;
    }
}
