// Thin VS Code host adapter for MasterAI's backend-neutral local API.
// This unit owns IDE presentation and SecretStorage only; it never selects,
// launches, or configures an inference backend.

'use strict';

const vscode = require('vscode');
const childProcess = require('child_process');

const tokenKey = 'masterai.scopedBearerToken';
let activeController;

function configuration() {
  const config = vscode.workspace.getConfiguration('masterai');
  const serverUrl = String(config.get('serverUrl', '')).replace(/\/+$/, '');
  if (!/^https?:\/\/127\.0\.0\.1(?::\d+)?$/i.test(serverUrl) &&
      !/^https?:\/\/localhost(?::\d+)?$/i.test(serverUrl)) {
    throw new Error('masterai.serverUrl must be an explicit loopback HTTP(S) URL.');
  }
  return { serverUrl, projectId: String(config.get('projectId', '')).trim() };
}

async function request(context, path, options = {}) {
  const token = await context.secrets.get(tokenKey);
  if (!token) throw new Error('Store a scoped MasterAI token first.');
  activeController?.abort();
  activeController = new AbortController();
  try {
    const { serverUrl } = configuration();
    const response = await fetch(`${serverUrl}${path}`, {
      ...options,
      signal: activeController.signal,
      headers: {
        'Authorization': `Bearer ${token}`,
        'Content-Type': 'application/json',
        ...(options.headers || {})
      }
    });
    const text = await response.text();
    if (!response.ok) throw new Error(`HTTP ${response.status}: ${text}`);
    return text ? JSON.parse(text) : {};
  } finally {
    activeController = undefined;
  }
}

async function configureToken(context) {
  const token = await vscode.window.showInputBox({
    title: 'MasterAI scoped token',
    prompt: 'The token must be project-bound and include ide.connect.',
    password: true,
    ignoreFocusOut: true,
    validateInput: value => value.trim().length < 16 ? 'Token is incomplete.' : undefined
  });
  if (token === undefined) return;
  await context.secrets.store(tokenKey, token.trim());
  vscode.window.showInformationMessage('MasterAI token stored in VS Code SecretStorage.');
}

async function validateConnection(context) {
  const capabilities = await request(context, '/api/v1/ide/capabilities');
  if (capabilities?.diffPreview?.appliesChanges !== false) {
    throw new Error('Capability contract did not preserve read-only diff preview.');
  }
  vscode.window.showInformationMessage(`MasterAI connected (protocol ${capabilities.protocolVersion}).`);
}

// Launches the exact native secret-backed profile without a shell, negotiates
// MCP, and verifies tool discovery. No credential enters VS Code settings,
// process arguments, environment, output, or the validation record.
async function validateMcpProfile() {
  const config = vscode.workspace.getConfiguration('masterai');
  const executable = String(config.get('executable', '')).trim();
  const settings = String(config.get('settingsFile', '')).trim();
  if (!executable || !settings) throw new Error('Configure masterai.executable and masterai.settingsFile first.');
  return await new Promise((resolve, reject) => {
    const child = childProcess.spawn(executable, ['mcp-stdio', settings, 'vscode'], {
      shell: false, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe']
    });
    let stdout = '', stderr = '', settled = false;
    const timer = setTimeout(() => finish(new Error('Native MCP validation timed out.')), 15000);
    function finish(error, result) {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      child.kill();
      error ? reject(error) : resolve(result);
    }
    child.on('error', finish);
    child.stderr.on('data', data => { stderr += data.toString(); if (stderr.length > 4096) stderr = stderr.slice(-4096); });
    child.stdout.on('data', data => {
      stdout += data.toString();
      const lines = stdout.split(/\r?\n/).filter(Boolean);
      if (lines.length < 2) return;
      try {
        const initialized = JSON.parse(lines[0]);
        const tools = JSON.parse(lines[1]);
        if (initialized?.result?.protocolVersion !== '2025-11-25' || !Array.isArray(tools?.result?.tools)) {
          throw new Error('Native MCP profile returned an unexpected contract.');
        }
        finish(undefined, { protocolVersion: initialized.result.protocolVersion, toolCount: tools.result.tools.length });
      } catch (error) { finish(error); }
    });
    child.on('exit', code => {
      if (!settled) finish(new Error(`Native MCP profile exited ${code}: ${stderr}`));
    });
    child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'initialize', params: { protocolVersion: '2025-11-25', capabilities: {}, clientInfo: { name: 'MasterAI VS Code', version: '0.1.0' } } }) + '\n');
    child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id: 2, method: 'tools/list', params: {} }) + '\n');
  });
}

async function scanDiagnostics(context, collection) {
  const editor = vscode.window.activeTextEditor;
  if (!editor) throw new Error('Open a project file first.');
  const folder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
  if (!folder) throw new Error('The active file is not in the open workspace.');
  const relative = vscode.workspace.asRelativePath(editor.document.uri, false).replace(/\\/g, '/');
  const { projectId } = configuration();
  if (!projectId) throw new Error('Configure masterai.projectId first.');
  const result = await request(context, '/api/v1/ide/diagnostics', {
    method: 'POST', body: JSON.stringify({ projectId, path: relative })
  });
  const diagnostics = (result.diagnostics || []).map(item => {
    const line = Math.max(0, Number(item.line || 1) - 1);
    const severity = item.severity === 'warning'
      ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Information;
    const diagnostic = new vscode.Diagnostic(new vscode.Range(line, 0, line, 160), item.message, severity);
    diagnostic.code = item.code;
    diagnostic.source = 'MasterAI';
    return diagnostic;
  });
  collection.set(editor.document.uri, diagnostics);
  vscode.window.showInformationMessage(`MasterAI reported ${diagnostics.length} diagnostic(s).`);
}

async function previewDiff(context) {
  const editor = vscode.window.activeTextEditor;
  if (!editor) throw new Error('Open a unified diff first.');
  const { projectId } = configuration();
  if (!projectId) throw new Error('Configure masterai.projectId first.');
  const summary = await request(context, '/api/v1/ide/diff-preview', {
    method: 'POST', body: JSON.stringify({ projectId, unifiedDiff: editor.document.getText() })
  });
  vscode.window.showInformationMessage(
    `Read-only preview: ${summary.files} file(s), ${summary.hunks} hunk(s), +${summary.additions}/-${summary.deletions}.`
  );
}

function guarded(operation) {
  return async () => {
    try { await operation(); }
    catch (error) {
      if (error?.name !== 'AbortError') vscode.window.showErrorMessage(`MasterAI: ${error.message || error}`);
    }
  };
}

async function activate(context) {
  const diagnostics = vscode.languages.createDiagnosticCollection('masterai');
  context.subscriptions.push(
    diagnostics,
    vscode.commands.registerCommand('masterai.configureToken', guarded(() => configureToken(context))),
    vscode.commands.registerCommand('masterai.validateConnection', guarded(() => validateConnection(context))),
    vscode.commands.registerCommand('masterai.validateMcpProfile', guarded(async () => {
      const result = await validateMcpProfile();
      vscode.window.showInformationMessage(`MasterAI MCP connected (${result.protocolVersion}, ${result.toolCount} tools).`);
    })),
    vscode.commands.registerCommand('masterai.openChat', guarded(async () => {
      const { serverUrl } = configuration();
      await vscode.env.openExternal(vscode.Uri.parse(`${serverUrl}/app`));
    })),
    vscode.commands.registerCommand('masterai.scanDiagnostics', guarded(() => scanDiagnostics(context, diagnostics))),
    vscode.commands.registerCommand('masterai.previewDiff', guarded(() => previewDiff(context))),
    vscode.commands.registerCommand('masterai.cancel', () => activeController?.abort())
  );
  if (process.env.MASTERAI_VALIDATE_ON_STARTUP === '1') {
    let evidence;
    try { evidence = { ok: true, ...(await validateMcpProfile()) }; }
    catch (error) { evidence = { ok: false, error: String(error?.message || error) }; }
    await vscode.workspace.fs.createDirectory(context.globalStorageUri);
    await vscode.workspace.fs.writeFile(
      vscode.Uri.joinPath(context.globalStorageUri, 'host-validation.json'),
      Buffer.from(JSON.stringify(evidence), 'utf8'));
  }
}

function deactivate() { activeController?.abort(); }

module.exports = { activate, deactivate };
