// Thin Visual Studio host adapter for MasterAI's backend-neutral local API.
// This package owns IDE presentation and DPAPI bearer storage only; model
// execution, policy, validation, and persistence remain in native MasterAI.

using System;
using System.ComponentModel.Design;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;

namespace MasterAI.VisualStudio
{
    internal static class CommandIds
    {
        internal static readonly Guid Set = new Guid("25d8e543-bc64-45fd-ab89-7b69b95c2be9");
        internal const int OpenChat = 0x0100;
        internal const int StoreToken = 0x0101;
        internal const int Validate = 0x0102;
        internal const int Cancel = 0x0103;
    }

    [Guid("9ef9175b-e56d-47cf-b4f8-8bf79231ef68")]
    public sealed class MasterAIChatWindow : ToolWindowPane
    {
        public MasterAIChatWindow() : base(null)
        {
            Caption = "MasterAI Chat";
            Content = new WebBrowser { Source = new Uri(MasterAIPackage.ServerUrl + "/app") };
        }
    }

    [PackageRegistration(UseManagedResourcesOnly = true, AllowsBackgroundLoading = true)]
    [InstalledProductRegistration("MasterAI", "Backend-neutral local AI client", "0.1.0")]
    [ProvideMenuResource("Menus.ctmenu", 1)]
    [ProvideToolWindow(typeof(MasterAIChatWindow))]
    [ProvideAutoLoad(UIContextGuids80.NoSolution, PackageAutoLoadFlags.BackgroundLoad)]
    [ProvideAutoLoad(UIContextGuids80.SolutionExists, PackageAutoLoadFlags.BackgroundLoad)]
    [Guid("5c063d0b-6938-43e7-aeca-4224344b6af2")]
    public sealed class MasterAIPackage : AsyncPackage
    {
        internal static string ServerUrl =>
            (Environment.GetEnvironmentVariable("MASTERAI_IDE_URL") ??
             "http://127.0.0.1:7070").TrimEnd('/');

        private static readonly string TokenPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "MasterAI", "visual-studio-token.bin");
        private CancellationTokenSource activeRequest;

        protected override async Task InitializeAsync(
            CancellationToken cancellationToken,
            IProgress<ServiceProgressData> progress)
        {
            await JoinableTaskFactory.SwitchToMainThreadAsync(cancellationToken);
            var service = await GetServiceAsync(typeof(IMenuCommandService))
                as OleMenuCommandService;
            if (service == null) return;
            service.AddCommand(new MenuCommand(
                (s, e) => JoinableTaskFactory.RunAsync(ShowChatAsync),
                new CommandID(CommandIds.Set, CommandIds.OpenChat)));
            service.AddCommand(new MenuCommand(
                (s, e) => StoreToken(),
                new CommandID(CommandIds.Set, CommandIds.StoreToken)));
            service.AddCommand(new MenuCommand(
                (s, e) => JoinableTaskFactory.RunAsync(ValidateAsync),
                new CommandID(CommandIds.Set, CommandIds.Validate)));
            service.AddCommand(new MenuCommand(
                (s, e) => activeRequest?.Cancel(),
                new CommandID(CommandIds.Set, CommandIds.Cancel)));
            if (Environment.GetEnvironmentVariable(
                    "MASTERAI_VALIDATE_ON_STARTUP") == "1") {
                await ValidateNativeProfileAsync();
            }
        }

        // Runs the native visual-studio MCP profile without a shell and writes
        // only protocol/tool-count evidence to LocalAppData. The token remains
        // inside MasterAI's existing current-user OS secret alias.
        private static async Task ValidateNativeProfileAsync()
        {
            var stage = "configuration";
            var executable = Environment.GetEnvironmentVariable("MASTERAI_EXE");
            var settings = Environment.GetEnvironmentVariable("MASTERAI_SETTINGS");
            var evidencePath = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "MasterAI", "visual-studio-host-validation.json");
            Directory.CreateDirectory(Path.GetDirectoryName(evidencePath));
            try {
                if (string.IsNullOrWhiteSpace(executable) ||
                    string.IsNullOrWhiteSpace(settings) ||
                    settings.Contains("\""))
                    throw new InvalidOperationException(
                        "MASTERAI_EXE and MASTERAI_SETTINGS are required.");
                stage = "start";
                var start = new ProcessStartInfo {
                    FileName = executable,
                    Arguments = "mcp-stdio \"" + settings + "\" visual-studio",
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardInput = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true
                };
                using (var process = Process.Start(start)) {
                    if (process == null) throw new InvalidOperationException(
                        "Native MCP profile did not start.");
                    try {
                        stage = "request";
                        await process.StandardInput.WriteLineAsync(
                            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"MasterAI Visual Studio\",\"version\":\"0.1.0\"}}}");
                        await process.StandardInput.WriteLineAsync(
                            "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}");
                        stage = "response";
                        var initializeTask = process.StandardOutput.ReadLineAsync();
                        if (await Task.WhenAny(initializeTask,
                                Task.Delay(TimeSpan.FromSeconds(15))) != initializeTask)
                            throw new TimeoutException("Native MCP validation timed out.");
                        var initialized = await initializeTask;
                        var toolsTask = process.StandardOutput.ReadLineAsync();
                        if (await Task.WhenAny(toolsTask,
                                Task.Delay(TimeSpan.FromSeconds(15))) != toolsTask)
                            throw new TimeoutException("Native MCP validation timed out.");
                        var tools = await toolsTask;
                        if (initialized == null || tools == null ||
                            !initialized.Contains("\"protocolVersion\":\"2025-11-25\"") ||
                            !tools.Contains("\"tools\":["))
                            throw new InvalidOperationException(
                                "Native MCP profile returned an unexpected contract.");
                        stage = "evidence";
                        var count = tools.Split(new[] { "\"name\":" },
                                                StringSplitOptions.None).Length - 1;
                        File.WriteAllText(evidencePath,
                            "{\"ok\":true,\"protocolVersion\":\"2025-11-25\",\"toolCount\":" + count + "}",
                            Encoding.UTF8);
                    } finally {
                        if (!process.HasExited) process.Kill();
                    }
                }
            } catch (Exception error) {
                File.WriteAllText(evidencePath,
                    "{\"ok\":false,\"error\":\"" +
                    error.GetType().Name + "\",\"stage\":\"" +
                    stage + "\"}", Encoding.UTF8);
            }
        }

        // Opens the server-owned authenticated chat page inside a VS tool
        // window, preserving one authoritative browser/chat implementation.
        private async Task ShowChatAsync()
        {
            await JoinableTaskFactory.SwitchToMainThreadAsync();
            var window = await ShowToolWindowAsync(
                typeof(MasterAIChatWindow), 0, true, DisposalToken);
            if (window?.Frame == null)
                throw new InvalidOperationException(
                    "MasterAI chat window could not be created.");
        }

        // Accepts a scoped token without echo and writes only its
        // current-user DPAPI ciphertext beneath LocalAppData.
        private static void StoreToken()
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            var box = new PasswordBox { MinWidth = 420, Margin = new Thickness(12) };
            var dialog = new Window {
                Title = "MasterAI scoped token", Content = box,
                SizeToContent = SizeToContent.WidthAndHeight,
                WindowStartupLocation = WindowStartupLocation.CenterScreen
            };
            box.KeyDown += (sender, args) => {
                if (args.Key == System.Windows.Input.Key.Enter) {
                    dialog.DialogResult = true;
                    dialog.Close();
                }
            };
            if (dialog.ShowDialog() != true || box.Password.Length < 16) return;
            Directory.CreateDirectory(Path.GetDirectoryName(TokenPath));
            var plaintext = Encoding.UTF8.GetBytes(box.Password);
            try {
                File.WriteAllBytes(TokenPath, ProtectedData.Protect(
                    plaintext, null, DataProtectionScope.CurrentUser));
            } finally {
                Array.Clear(plaintext, 0, plaintext.Length);
                box.Password = string.Empty;
            }
        }

        // Decrypts the token only for one bounded loopback capability request,
        // verifies the read-only diff contract, then clears plaintext bytes.
        private async Task ValidateAsync()
        {
            activeRequest?.Cancel();
            activeRequest = new CancellationTokenSource(TimeSpan.FromSeconds(15));
            try {
                Uri uri;
                if (!Uri.TryCreate(ServerUrl, UriKind.Absolute, out uri) ||
                    !uri.IsLoopback ||
                    (uri.Scheme != "http" && uri.Scheme != "https"))
                    throw new InvalidOperationException(
                        "MASTERAI_IDE_URL must be an explicit loopback HTTP(S) URL.");
                if (!File.Exists(TokenPath))
                    throw new InvalidOperationException(
                        "Store a scoped MasterAI token first.");
                var plaintext = ProtectedData.Unprotect(
                    File.ReadAllBytes(TokenPath), null,
                    DataProtectionScope.CurrentUser);
                try {
                    using (var client = new HttpClient {
                        BaseAddress = uri, Timeout = TimeSpan.FromSeconds(15)
                    }) {
                        client.DefaultRequestHeaders.Authorization =
                            new AuthenticationHeaderValue(
                                "Bearer", Encoding.UTF8.GetString(plaintext));
                        var response = await client.GetAsync(
                            "/api/v1/ide/capabilities", activeRequest.Token);
                        response.EnsureSuccessStatusCode();
                        var body = await response.Content.ReadAsStringAsync();
                        if (!body.Contains("\"appliesChanges\":false"))
                            throw new InvalidOperationException(
                                "Read-only diff-preview contract is missing.");
                    }
                } finally {
                    Array.Clear(plaintext, 0, plaintext.Length);
                }
                VsShellUtilities.ShowMessageBox(
                    this, "Connected to MasterAI's backend-neutral API.",
                    "MasterAI", OLEMSGICON.OLEMSGICON_INFO,
                    OLEMSGBUTTON.OLEMSGBUTTON_OK,
                    OLEMSGDEFBUTTON.OLEMSGDEFBUTTON_FIRST);
            } catch (Exception error) {
                VsShellUtilities.ShowMessageBox(
                    this, error.Message, "MasterAI connection failed",
                    OLEMSGICON.OLEMSGICON_CRITICAL,
                    OLEMSGBUTTON.OLEMSGBUTTON_OK,
                    OLEMSGDEFBUTTON.OLEMSGDEFBUTTON_FIRST);
            } finally {
                activeRequest?.Dispose();
                activeRequest = null;
            }
        }
    }
}
