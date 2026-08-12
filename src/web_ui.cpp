// MasterAI dependency-free loopback browser presentation.
//
// This unit contains only static HTML and JavaScript documents. Moving browser
// presentation out of the HTTP router keeps transport, policy, and UI concerns
// independently reviewable without introducing a web framework.
#include "server_internal.hpp"

// Small inline-SVG glyphs for the primary submit button on every form --
// mirrors the dynamically-built ICONS set in application_script() (used for
// row-toolbar buttons) but as C++ literals, since these buttons are baked
// into the static server-rendered HTML rather than assembled client-side.
// Every submit button gets one of these plus a title="" tooltip so no
// button on the page is icon-less or hint-less, matching the row-toolbar
// buttons' icon+title convention.
#define ICON_PLUS_SVG \
    "<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" " \
    "fill=\"currentColor\"><path d=\"M8 2a1 1 0 0 1 1 1v4h4a1 1 0 1 1 0 " \
    "2H9v4a1 1 0 1 1-2 0V9H3a1 1 0 1 1 0-2h4V3a1 1 0 0 1 1-1z\"/></svg>"
#define ICON_SAVE_SVG \
    "<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" " \
    "fill=\"currentColor\"><path d=\"M13.7 3.3a1 1 0 0 1 0 1.4l-7 7a1 1 " \
    "0 0 1-1.4 0l-3.5-3.5a1 1 0 1 1 1.4-1.4L6 9.6l6.3-6.3a1 1 0 0 1 1.4 " \
    "0z\"/></svg>"
#define ICON_UPLOAD_SVG \
    "<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" " \
    "fill=\"currentColor\"><path d=\"M7.5 1.5a.5.5 0 0 1 1 0v7.79l2.15-" \
    "2.15a.5.5 0 0 1 .7.71l-3 3a.5.5 0 0 1-.7 0l-3-3a.5.5 0 1 1 .7-.71L" \
    "7.5 9.29V1.5zM2 12.5a.5.5 0 0 1 .5-.5h11a.5.5 0 0 1 0 1h-11a.5.5 0 " \
    "0 1-.5-.5z\"/></svg>"
#define ICON_DOWNLOAD_SVG \
    "<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" " \
    "fill=\"currentColor\"><path d=\"M7.5 1.5a.5.5 0 0 1 1 0v7.29l2.15-" \
    "2.15a.5.5 0 1 1 .7.71l-3 3a.5.5 0 0 1-.7 0l-3-3a.5.5 0 1 1 .7-.71L" \
    "7.5 8.79V1.5zM2 12.5a.5.5 0 0 1 .5-.5h11a.5.5 0 0 1 0 1h-11a.5.5 0 " \
    "0 1-.5-.5z\"/></svg>"
#define ICON_PLAY_SVG \
    "<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" " \
    "fill=\"currentColor\"><path d=\"M4 2.5a.5.5 0 0 1 .77-.42l9 5.5a." \
    "5.5 0 0 1 0 .84l-9 5.5A.5.5 0 0 1 4 13.5v-11z\"/></svg>"

// Shared dark theme for both browser documents. A macro (not a function)
// because it expands into adjacent C++ string-literal concatenation inside
// each page's own <style> block, alongside that page's own overrides.
#define DARK_THEME_CSS \
    ":root{color-scheme:dark;--bg:#121214;--panel:#1c1c22;--panel-border:#2a2a33;" \
    "--text:#e8e8ec;--muted:#9a9aa5;--accent:#7c5cff;--accent-hover:#9179ff}" \
    "*{box-sizing:border-box}" \
    "body{font:15px/1.6 system-ui,-apple-system,'Segoe UI',sans-serif;" \
    "margin:auto;padding:1.5rem;background:var(--bg);color:var(--text)}" \
    "h1{background:linear-gradient(135deg,#7c5cff,#22d3ee);" \
    "-webkit-background-clip:text;background-clip:text;color:transparent;margin:0}" \
    "h2{margin-top:0;font-size:1.1rem}" \
    "h3{color:var(--muted);font-size:.85rem;text-transform:uppercase;" \
    "letter-spacing:.06em;margin-bottom:.5rem}" \
    "section{background:var(--panel);border:1px solid var(--panel-border);" \
    "border-radius:.75rem;padding:1.25rem;box-shadow:0 4px 24px rgba(0,0,0,.35)}" \
    "pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#0e0e11;" \
    "border:1px solid var(--panel-border);border-radius:.5rem;padding:.75rem;" \
    "color:#c8c8d4;font-size:.8rem;max-height:16rem;overflow:auto}" \
    "label{display:block;margin-top:.7rem;color:var(--muted);font-size:.8rem}" \
    "input,select,textarea,button{font:inherit;width:100%;box-sizing:border-box;" \
    "padding:.55rem;border-radius:.5rem;border:1px solid var(--panel-border);" \
    "background:#15151a;color:var(--text)}" \
    "input:focus,select:focus,textarea:focus{outline:none;border-color:var(--accent);" \
    "box-shadow:0 0 0 3px rgba(124,92,255,.25)}" \
    "button{margin-top:.75rem;background:var(--accent);border:none;color:#fff;" \
    "font-weight:600;cursor:pointer;transition:background .15s}" \
    "button:hover{background:var(--accent-hover)}" \
    "button svg{vertical-align:-2px;margin-right:.4rem}" \
    "a{color:var(--accent-hover)}" \
    "a:hover{text-decoration:none}" \
    "progress{width:100%;height:.6rem;margin-top:.6rem;accent-color:var(--accent)}" \
    "#actionStatus,#status{color:var(--muted);min-height:1.2em}" \
    /* Floating error bubble every action-failure catch block now raises \
       through showSystemError() instead of the easy-to-miss #actionStatus \
       line -- pinned to a corner and width-capped (not a full-width top \
       banner) so it can never blow out over the rest of the page, on the \
       Machine Learning screens or anywhere else. Has its own title bar \
       (a red gradient, with the close button pinned to its far right) \
       above a body panel that carries the actual error text in light \
       rose on a dark maroon background, so the message itself stays \
       legible while the title bar signals severity. Auto-fades to solid \
       black over the last .4s before hiding itself 10 seconds after the \
       most recent showSystemError() call (see that function) -- the \
       default timeout for any error that is not dismissed sooner; its \
       close and copy buttons both act immediately instead of waiting on \
       that timer. */ \
    "#systemErrorBanner{position:fixed;bottom:1rem;right:1rem;z-index:9999;" \
    "width:min(24rem,calc(100vw - 2rem));padding:0;" \
    "border:1px solid #7a0d0d;border-radius:.75rem;overflow:hidden;" \
    "box-shadow:0 6px 20px rgba(0,0,0,.45);" \
    "display:flex;flex-direction:column;" \
    "transition:border-color .4s ease}" \
    "#systemErrorBanner.systemErrorFading{border-color:#000}" \
    "#systemErrorBanner .systemErrorTitleBar{display:flex;align-items:center;" \
    "justify-content:space-between;gap:.75rem;padding:.55rem .75rem;" \
    "background:linear-gradient(135deg,#8a1414,#c92a2a 55%,#5c0a0a);" \
    "color:#fff2f2;transition:background-color .4s ease}" \
    "#systemErrorBanner.systemErrorFading .systemErrorTitleBar{background:#000}" \
    "#systemErrorBanner .systemErrorTitle{font-weight:800;letter-spacing:.03em}" \
    "#systemErrorBanner .systemErrorClose{width:auto;margin:0;padding:0 .3rem;" \
    "background:transparent;border:none;color:#fff2f2;font-weight:700;" \
    "font-size:1.15rem;line-height:1;cursor:pointer}" \
    "#systemErrorBanner .systemErrorClose:hover{color:#ffd6d6}" \
    "#systemErrorBanner .systemErrorBody{flex:1;overflow-wrap:anywhere;" \
    "max-height:12rem;overflow:auto;padding:.75rem 1rem;" \
    "background:#5a0d0d;color:#ffd9d9;font-weight:600;" \
    "transition:background-color .4s ease,color .4s ease}" \
    "#systemErrorBanner.systemErrorFading .systemErrorBody{background:#000;" \
    "color:#000}" \
    "#systemErrorBanner .systemErrorActions{display:flex;justify-content:flex-end;" \
    "padding:.5rem .75rem .75rem;background:#5a0d0d;" \
    "transition:background-color .4s ease}" \
    "#systemErrorBanner.systemErrorFading .systemErrorActions{background:#000}" \
    "#systemErrorBanner .systemErrorCopy{width:auto;margin:0;" \
    "padding:.3rem .7rem;background:transparent;border:1px solid #ffd9d9;" \
    "color:#ffd9d9;font-size:.75rem;border-radius:.4rem;cursor:pointer}" \
    "#systemErrorBanner .systemErrorCopy:hover{background:rgba(255,217,217,.15)}" \
    ".checkboxLabel{display:flex;align-items:center;gap:.5rem}" \
    ".checkboxLabel input{width:auto}"

namespace masterai::server_internal {
// Supplies the small fetch/streaming client used by both browser documents.
std::string application_script() {
    return
        "const q=s=>document.querySelector(s);let csrf='',generation=null,"
        "lastAppliedPreset=null;const downloadSizes={};"
        // Populated in load() from GET /api/v1/models' own architecture
        // field, keyed by model id -- lets the composer's model settings
        // panel (see openModelSettingsPanel()) tell whether the selected
        // model can actually honor a reasoning/thinking directive or only
        // ever the sampling-preset fallback (see modelSupportsReasoning()).
        "let MODEL_ARCHS={};"
        "const REASONING_ARCHS=['qwen','gpt-oss'];"
        "function modelSupportsReasoning(modelId){"
        "const arch=(MODEL_ARCHS[modelId]||'').toLowerCase();"
        "return REASONING_ARCHS.some(a=>arch.includes(a));}"
        // Per-model effort/thinking preference, persisted client-side (no
        // server-side account state needed for a browser-local UI
        // preference) so the same model reopens with whatever was last
        // chosen for it, in this chat or any other.
        "function loadModelSettings(modelId){"
        "const fallback={effort:'medium',thinking:'off'};if(!modelId)return fallback;"
        "try{const all=JSON.parse(localStorage.getItem('modelSettings')||'{}');"
        "return all[modelId]||fallback;}catch(x){return fallback;}}"
        "function saveModelSettings(modelId,settings){if(!modelId)return;"
        "let all={};try{all=JSON.parse(localStorage.getItem('modelSettings')||"
        "'{}');}catch(x){console.warn('saveModelSettings: corrupt "
        "modelSettings, resetting',x);}"
        "all[modelId]=settings;localStorage.setItem('modelSettings',"
        "JSON.stringify(all));}"
        "function saveCurrentModelSettings(){const select=q('#chatModel');"
        "if(!select||!select.value)return;"
        "saveModelSettings(select.value,"
        "{effort:q('#modelEffort').value,thinking:q('#modelThinking').value});}"
        // Refreshes the settings panel's two controls (and its note) for
        // whatever model is currently selected, without changing whether
        // the panel is shown -- called whenever the picker's value changes
        // programmatically (a fresh chat's default model, an existing chat
        // being reopened) so the panel is ready with the right values
        // without popping open on its own. openModelSettingsPanel() below
        // is the user-facing entry point that also shows it.
        "function refreshModelSettingsPanel(){"
        "const select=q('#chatModel'),panel=q('#modelSettingsPanel');"
        "if(!select||!panel||!select.value)return;"
        "const saved=loadModelSettings(select.value);"
        "q('#modelEffort').value=saved.effort;q('#modelThinking').value=saved.thinking;"
        "const note=q('#modelSettingsNote');"
        "if(note)note.textContent=modelSupportsReasoning(select.value)?"
        "'This model supports explicit reasoning -- these settings are sent "
        "to it as reasoning instructions.':"
        "'This model has no reasoning mode of its own -- these settings "
        "instead adjust its sampling (temperature and reply length).';}"
        // Opens the settings panel for the currently selected model --
        // called only from explicit user action (the gear button, or the
        // user changing the model picker themselves).
        "function openModelSettingsPanel(){"
        "refreshModelSettingsPanel();"
        "const panel=q('#modelSettingsPanel');if(panel)panel.hidden=false;}"
        // Files attached to the message currently being composed, and the
        // project id they were (or will be) uploaded against -- an existing
        // chat's project is fixed and known once openChat() loads it, but a
        // brand-new chat only has one once the composer's own project picker
        // has a value, so attachFiles() below re-reads that picker each time
        // rather than caching it once.
        "let attachedFiles=[],openedProjectId='';"
        // Last chat list rendered by renderChatList(), kept around so a
        // brand-new chat can be shown in the sidebar immediately (see
        // streamMessage()) instead of waiting on the next full load()
        // round trip, which the composer only fires once more anyway.
        "let lastChats=[];"
        "function currentProjectId(){return openedProjectId||"
        "(q('#chatProject')?q('#chatProject').value:'');}"
        // Tracks which download ids already have a pollDownloadProgress
        // interval running, keyed by id, so a job left in the 'transferring'
        // state across a page reload (or a second renderDownloads call from
        // another action) gets exactly one poll loop instead of zero or
        // several stacking up.
        "const activePolls={};"
        "async function api(path,method='GET',body){const h={};"
        "if(body)h['Content-Type']='application/json';if(csrf)h['X-CSRF-Token']=csrf;"
        "const r=await fetch(path,{method,headers:h,body:body?JSON.stringify(body):undefined});"
        "const t=await r.text();if(!r.ok)throw new Error(t||r.status);"
        "return t?JSON.parse(t):{};}"
        // Each workspace section is served at its own URL (see the /app
        // route handlers in server.cpp), so only one section's own list/form
        // elements ever exist in the DOM at a time. load() below still
        // builds one shared Promise.all for every possible section, so
        // fetchFor() skips the network round trip entirely -- resolving
        // straight to the render function's own empty-state fallback --
        // whenever this page doesn't contain the element that fetch's data
        // would render into. Without this, every single page load (chat
        // included) fired all ~20 Machine Learning list/dashboard requests
        // regardless of which section (if any) was showing, which was
        // enough on its own to trip the server's shared rate_limit_per_minute
        // budget after a handful of page navigations.
        "function fetchFor(elementId,path,fallback){"
        "return q(elementId)?api(path).catch(()=>fallback):"
        "Promise.resolve(fallback);}"
        // Foreign-key fields use named choices instead of asking an
        // administrator to copy opaque ids out of another table.
        "function fillMlSelect(selector,items,prompt,label){const el=q(selector);"
        "if(!el)return;const selected=el.value;el.innerHTML='';"
        "const empty=document.createElement('option');empty.value='';"
        "empty.textContent=prompt;el.appendChild(empty);"
        "for(const item of items){const option=document.createElement('option');"
        "option.value=item.id;option.textContent=label(item);el.appendChild(option);}"
        "if([...el.options].some(x=>x.value===selected))el.value=selected;}"
        // Every action-failure catch block across the app raises through
        // here instead of quietly setting the small #actionStatus line --
        // a floating bubble pinned to a bottom corner, width-capped and
        // scrollable so a long message is contained rather than blowing out
        // over the rest of the page (the Machine Learning screens' own
        // failures included). Its own title bar -- a red gradient, titled
        // 'SYSTEM ERROR!', with the close (x) button pinned to its far
        // right -- sits above a body panel that carries the actual error
        // text in light rose on a dark maroon background, so the message
        // stays legible while the title bar signals severity. A copy button
        // lets the exact message be pasted elsewhere (bug reports, chat
        // with an administrator) without retyping it. Unless closed sooner
        // (the x button), the whole bubble fades to solid black over its
        // last .4 seconds and then closes 10 seconds after this call -- the
        // default timeout for any error left on screen. Reuses one bubble
        // element (created lazily) so a second failure while the first is
        // still showing just replaces the message and restarts the timer
        // rather than stacking bubbles.
        "function showSystemError(message){let el=q('#systemErrorBanner');"
        "if(!el){el=document.createElement('div');el.id='systemErrorBanner';"
        "const titleBar=document.createElement('div');"
        "titleBar.className='systemErrorTitleBar';"
        "const title=document.createElement('div');"
        "title.className='systemErrorTitle';title.textContent='SYSTEM ERROR!';"
        "const close=document.createElement('button');close.type='button';"
        "close.className='systemErrorClose';close.textContent='\\u00d7';"
        "close.setAttribute('aria-label','Dismiss error');"
        "close.addEventListener('click',()=>hideSystemError(el));"
        "titleBar.append(title,close);"
        "const body=document.createElement('div');body.className='systemErrorBody';"
        "const actions=document.createElement('div');"
        "actions.className='systemErrorActions';"
        "const copyBtn=document.createElement('button');copyBtn.type='button';"
        "copyBtn.className='systemErrorCopy';copyBtn.textContent='Copy';"
        "copyBtn.setAttribute('aria-label','Copy error message');"
        "copyBtn.addEventListener('click',()=>"
        "copyToClipboard(el.querySelector('.systemErrorBody').textContent,copyBtn));"
        "actions.append(copyBtn);el.append(titleBar,body,actions);"
        "document.body.append(el);}"
        "el.querySelector('.systemErrorBody').textContent=message;"
        "el.hidden=false;el.classList.remove('systemErrorFading');"
        "if(el._fadeTimer)clearTimeout(el._fadeTimer);"
        "if(el._hideTimer)clearTimeout(el._hideTimer);"
        "el._fadeTimer=setTimeout(()=>el.classList.add('systemErrorFading'),9600);"
        "el._hideTimer=setTimeout(()=>hideSystemError(el),10000);}"
        "function hideSystemError(el){"
        "if(el._fadeTimer)clearTimeout(el._fadeTimer);"
        "if(el._hideTimer)clearTimeout(el._hideTimer);"
        "el.hidden=true;el.classList.remove('systemErrorFading');}"
        "async function login(e){e.preventDefault();try{const d=await api('/api/v1/auth/login','POST',"
        "{username:q('#username').value,password:q('#password').value});"
        "sessionStorage.setItem('csrf',d.csrfToken);location.href='/app';}"
        "catch(x){showSystemError('Login failed. Check your username and password.');}}"
        // Toggles the setup-vs-login sections on the initial page load by
        // asking the already-public /health/ready route whether the first
        // administrator still needs to be created.
        "async function initLogin(){try{const r=await fetch('/health/ready');"
        "const d=await r.json();if(d.setupRequired){q('#setupSection').hidden=false;"
        "q('#loginSection').hidden=true;}}catch(x){console.warn('initLogin: "
        "/health/ready check failed, defaulting to login form',x);}}"
        "async function setupLocalAdmin(e){e.preventDefault();const s=q('#status');"
        "try{await api('/api/v1/setup/local','POST',{setupToken:q('#setupToken').value,"
        "username:q('#setupUsername').value,password:q('#setupPassword').value,"
        "displayName:q('#setupDisplay').value});"
        "s.textContent='Administrator created. Sign in below.';"
        "q('#setupSection').hidden=true;q('#loginSection').hidden=false;}"
        "catch(x){showSystemError('Setup failed: '+x.message);}}"
        "async function load(){csrf=sessionStorage.getItem('csrf')||'';try{"
        "const [me,p,c,mem,m,b,d,u,ml,mlp,mlm,mld,mls,mllt,mlpj,mltj,mler,mlex,mlft,mlmb,mlie,mlsr,mlvs,mlrag,mlse,mlhs,mlmo,mlck,mldp,mlcmp,mlkd,mlend,mlnode,mlpipe,mlpolicy,mlcard,mlaudit,mlmon,cfg,report]="
        "await Promise.all([api('/api/v1/users/me'),"
        "api('/api/v1/projects').catch(()=>({projects:[]})),"
        "api('/api/v1/chats'),"
        "api('/api/v1/memories').catch(()=>({memories:[]})),"
        "api('/api/v1/models').catch(()=>({models:[]})),"
        "api('/api/v1/benchmarks').catch(()=>({benchmarks:[]})),"
        "api('/api/v1/model-downloads').catch(()=>({downloads:[]})),"
        "api('/api/v1/users').catch(()=>({users:[]})),"
        // Every ml.* fetch below is skipped (see fetchFor() above) unless
        // this page actually renders that resource's list -- 403s for
        // anyone who isn't an administrator (see role_allows'
        // ml.dashboard.view) still fall back to the same empty state on the
        // rare page that does render it.
        "fetchFor('#mlAck','/api/v1/ml/dashboard',"
        "{enabled:false,phase:'',activeProjects:0,modelsTraining:0,"
        "modelsAwaitingEvaluation:0,modelsAwaitingApproval:0,"
        "deployedModels:0,failedTrainingJobs:0,interfaces:[]}),"
        "fetchFor('#mlProjectsList,#mlTrainingJobProjectId,#mlExperimentProjectId,#mlFineTuningJobProjectId,#mlModelBuilderConfigProjectId',"
        "'/api/v1/ml/projects',{projects:[]}),"
        "fetchFor('#mlModelsList,#mlPredictModelId,#mlTrainingJobModelId,#mlEvaluationRunModelId,#mlExperimentModelId,#mlFineTuningJobModelId,#mlModelBuilderConfigBaseModelId,#mlModelOptimizationModelId,#mlDeploymentModelId,#mlModelComparisonBaselineModelId,#mlModelComparisonCandidateModelId,#mlPipelineModelId',"
        "'/api/v1/ml/models',{models:[]}),"
        "fetchFor('#mlDatasetsList,#mlDatasetContentId,#mlLabelTaskDatasetId,#mlPrepJobDatasetId,#mlTrainingJobDatasetId,#mlEvaluationRunDatasetId,#mlExperimentDatasetId,#mlFineTuningJobDatasetId,#mlInstructionExampleDatasetId,#mlSyntheticRecordDatasetId,#mlModelComparisonDatasetId,#mlPipelineDatasetId',"
        "'/api/v1/ml/datasets',{datasets:[]}),"
        "fetchFor('#mlSubjectsList,#mlKnowledgeSubjectId,#mlSubjectExamSubjectId',"
        "'/api/v1/ml/subjects',{subjects:[]}),"
        "fetchFor('#mlLabelTasksList','/api/v1/ml/label-tasks',{labelTasks:[]}),"
        "fetchFor('#mlPrepJobsList','/api/v1/ml/prep-jobs',{prepJobs:[]}),"
        "fetchFor('#mlTrainingJobsList,#mlHyperparameterSearchTrainingJobId,#mlCheckpointTrainingJobId','/api/v1/ml/training-jobs',"
        "{trainingJobs:[]}),"
        "fetchFor('#mlEvaluationRunsList','/api/v1/ml/evaluation-runs',"
        "{evaluationRuns:[]}),"
        "fetchFor('#mlExperimentsList','/api/v1/ml/experiments',"
        "{experiments:[]}),"
        "fetchFor('#mlFineTuningJobsList','/api/v1/ml/fine-tuning-jobs',"
        "{fineTuningJobs:[]}),"
        "fetchFor('#mlModelBuilderConfigsList',"
        "'/api/v1/ml/model-builder-configs',{modelBuilderConfigs:[]}),"
        "fetchFor('#mlInstructionExamplesList',"
        "'/api/v1/ml/instruction-examples',{instructionExamples:[]}),"
        "fetchFor('#mlSyntheticRecordsList','/api/v1/ml/synthetic-records',"
        "{syntheticRecords:[]}),"
        "fetchFor('#mlVectorStoresList,#mlKnowledgeVectorStoreId,#mlRagConfigVectorStoreId',"
        "'/api/v1/ml/vector-stores',"
        "{vectorStores:[]}),"
        "fetchFor('#mlRagConfigsList','/api/v1/ml/rag-configs',"
        "{ragConfigs:[]}),"
        "fetchFor('#mlSubjectExamsList','/api/v1/ml/subject-exams',"
        "{subjectExams:[]}),"
        "fetchFor('#mlHyperparameterSearchesList',"
        "'/api/v1/ml/hyperparameter-searches',{hyperparameterSearches:[]}),"
        "fetchFor('#mlModelOptimizationsList',"
        "'/api/v1/ml/model-optimizations',{modelOptimizations:[]}),"
        "fetchFor('#mlCheckpointsList','/api/v1/ml/checkpoints',"
        "{checkpoints:[]}),"
        "fetchFor('#mlDeploymentsList','/api/v1/ml/deployments',"
        "{deployments:[]}),"
        "fetchFor('#mlModelComparisonsList','/api/v1/ml/model-comparisons',"
        "{modelComparisons:[]}),"
        "fetchFor('#mlKnowledgeDocumentsList','/api/v1/ml/knowledge-documents',"
        "{knowledgeDocuments:[]}),"
        "fetchFor('#mlInferenceEndpointsList,#mlEndpointModelId',"
        "'/api/v1/ml/inference-endpoints',{inferenceEndpoints:[]}),"
        "fetchFor('#mlComputeNodesList','/api/v1/ml/compute-nodes',"
        "{computeNodes:[]}),"
        "fetchFor('#mlAutomationPipelinesList,#mlPipelineProjectId',"
        "'/api/v1/ml/automation-pipelines',{automationPipelines:[]}),"
        "fetchFor('#mlSafetyPoliciesList','/api/v1/ml/safety-policies',"
        "{safetyPolicies:[]}),"
        "fetchFor('#mlModelCardsList,#mlModelCardModelId',"
        "'/api/v1/ml/model-cards',{modelCards:[]}),"
        "fetchFor('#mlAuditLogsList','/api/v1/ml/audit-logs',{auditLogs:[]}),"
        "fetchFor('#mlMonitoringPanel','/api/v1/ml/monitoring',"
        "{systemResources:null,trainingJobCounts:{},evaluationMetrics:[],"
        "inferenceBenchmarks:[]}),"
        // 403/503 for anyone who isn't an administrator, or when no
        // settings.json path is known to the running server -- both are
        // quiet, expected no-ops here exactly like the ml.* fetches above.
        "fetchFor('#systemConfigForm','/api/v1/admin/config',null),"
        // 403 for anyone who isn't an administrator -- quiet no-op like the
        // ml.*/admin.config fetches above.
        "fetchFor('#systemReport','/api/v1/system/report',null)]);"
        "q('#who').textContent=me.displayName+' ('+me.role+')';"
        // A viewer's sidebar renders none of the settings pages, so these
        // elements legitimately don't exist -- guard every write instead of
        // assuming every page is present.
        "renderProjects(p.projects);renderModels(m.models);"
        // Phase 61: vector stores can choose either the always-available
        // authored backend or a verified GGUF embedding model from the
        // ordinary model inventory. No opaque model id needs to be typed.
        "const embeddingSelect=q('#mlVectorStoreEmbeddingModel');"
        "if(embeddingSelect){embeddingSelect.innerHTML="
        "'<option value=\"authored_hashing_vectorizer_v1\">MasterAI authored ' +"
        "'hashing vectorizer v1 (128 dimensions)</option>'+"
        "m.models.filter(x=>x.category==='embeddings-code-search'&&x.state==='ready')"
        ".map(x=>'<option value=\"'+esc(x.id)+'\">'+esc(x.displayName)+"
        "' (learned via llama.cpp)</option>').join('');}"
        "renderBenchmarks(b.benchmarks);renderDownloads(d.downloads);"
        "renderUsers(u.users);renderMlDashboard(ml);renderMlProjects(mlp.projects);"
        "renderMlModels(mlm.models);renderMlDatasets(mld.datasets);"
        "renderMlSubjects(mls.subjects);"
        "renderMlLabelTasks(mllt.labelTasks);renderMlPrepJobs(mlpj.prepJobs);"
        "renderMlTrainingJobs(mltj.trainingJobs);"
        "renderMlEvaluationRuns(mler.evaluationRuns);"
        "renderMlExperiments(mlex.experiments);"
        "renderMlFineTuningJobs(mlft.fineTuningJobs);"
        "renderMlModelBuilderConfigs(mlmb.modelBuilderConfigs);"
        "renderMlInstructionExamples(mlie.instructionExamples);"
        "renderMlSyntheticRecords(mlsr.syntheticRecords);"
        "renderMlVectorStores(mlvs.vectorStores);"
        "renderMlRagConfigs(mlrag.ragConfigs);"
        "renderMlSubjectExams(mlse.subjectExams);"
        "renderMlHyperparameterSearches(mlhs.hyperparameterSearches);"
        "renderMlModelOptimizations(mlmo.modelOptimizations);"
        "renderMlCheckpoints(mlck.checkpoints);"
        "renderMlDeployments(mldp.deployments);"
        "renderMlModelComparisons(mlcmp.modelComparisons);"
        "renderMlKnowledgeDocuments(mlkd.knowledgeDocuments);"
        "renderMlInferenceEndpoints(mlend.inferenceEndpoints);"
        "renderMlComputeNodes(mlnode.computeNodes);"
        "renderMlAutomationPipelines(mlpipe.automationPipelines);"
        "renderMlSafetyPolicies(mlpolicy.safetyPolicies);"
        "renderMlModelCards(mlcard.modelCards);"
        "renderMlAuditLogs(mlaudit.auditLogs);"
        "renderMlMonitoring(mlmon);"
        "fillMlSelect('#mlKnowledgeSubjectId',mls.subjects,'Choose a subject',"
        "x=>x.name);fillMlSelect('#mlSubjectExamSubjectId',mls.subjects,"
        "'Choose a subject',x=>x.name);"
        "fillMlSelect('#mlKnowledgeVectorStoreId',mlvs.vectorStores,"
        "'Choose a vector store',x=>x.name+' ('+x.status+')');"
        "fillMlSelect('#mlRagConfigVectorStoreId',mlvs.vectorStores,"
        "'Choose a vector store',x=>x.name+' ('+x.status+')');"
        "for(const id of ['#mlTrainingJobProjectId','#mlExperimentProjectId',"
        "'#mlFineTuningJobProjectId','#mlModelBuilderConfigProjectId',"
        "'#mlPipelineProjectId'])"
        "fillMlSelect(id,mlp.projects,'None / choose a project',x=>x.name);"
        "for(const id of ['#mlPredictModelId','#mlTrainingJobModelId',"
        "'#mlEvaluationRunModelId','#mlExperimentModelId','#mlFineTuningJobModelId',"
        "'#mlModelBuilderConfigBaseModelId','#mlModelOptimizationModelId',"
        "'#mlDeploymentModelId','#mlModelComparisonBaselineModelId',"
        "'#mlModelComparisonCandidateModelId','#mlEndpointModelId',"
        "'#mlModelCardModelId','#mlPipelineModelId'])fillMlSelect(id,mlm.models,"
        "'None / choose a model',x=>x.displayName||x.name);"
        "for(const id of ['#mlDatasetContentId','#mlLabelTaskDatasetId',"
        "'#mlPrepJobDatasetId','#mlTrainingJobDatasetId','#mlEvaluationRunDatasetId',"
        "'#mlExperimentDatasetId','#mlFineTuningJobDatasetId',"
        "'#mlInstructionExampleDatasetId','#mlSyntheticRecordDatasetId',"
        "'#mlModelComparisonDatasetId','#mlPipelineDatasetId'])fillMlSelect(id,mld.datasets,"
        "'None / choose a dataset',x=>x.name);"
        "fillMlSelect('#mlHyperparameterSearchTrainingJobId',mltj.trainingJobs,"
        "'Choose a training job',x=>x.name+' ('+x.status+')');"
        "fillMlSelect('#mlCheckpointTrainingJobId',mltj.trainingJobs,"
        "'Choose a training job',x=>x.name+' ('+x.status+')');"
        "fillMlSelect('#mlLabelTaskAssigneeId',u.users,'Unassigned',"
        "x=>x.displayName+' ('+x.role+')');"
        "renderSystemConfig(cfg);"
        "renderSystemReport(report);"
        "fill('#chatProject',p.projects,x=>x.id,x=>x.displayName);"
        // Chat offers every downloaded, verified model whose backend/format
        // and required CPU/GPU features this machine actually has (state
        // 'ready' on the server -- see ModelRegistry::scan()) -- anything
        // else genuinely can't be loaded, so it would just be a broken
        // option here. A model that's merely short on RAM *right now* still
        // counts as ready: that estimate is against current available RAM,
        // which can change, so it's left selectable with its recommended RAM
        // shown against its name (and a warning icon when the server's
        // diagnostic flags it) so the user can judge it themselves.
        "MODEL_ARCHS=Object.fromEntries(m.models.map(x=>[x.id,x.architecture||'']));"
        "const ready=m.models.filter(x=>x.state==='ready');"
        "fill('#chatModel',ready,x=>x.id,"
        "x=>(x.diagnostic&&x.diagnostic.startsWith('Warning:')?'\\u26a0\\ufe0f ':'')+"
        "x.displayName+' ('+Math.round(x.recommendedRamMiB/1024)+'GB)');"
        "renderChatList(c.chats);renderMemories(mem.memories);"
        // Landing directly on a chat's own URL (/app/chat/<id>) preloads its
        // id into this hidden field server-side; load its history now that
        // the page's other data has arrived.
        "const initialChat=q('#messageChat');"
        "if(initialChat&&initialChat.value)await openChat(initialChat.value);"
        "else if(q('#chatEmpty')){q('#chatEmpty').hidden=!ready.length;"
        "if(!ready.length)q('#chatEmpty').textContent="
        "'No downloaded models are ready for this machine yet. Ask an "
        "administrator or developer to download one from Settings.';"
        // A brand-new chat's model picker otherwise just falls back to
        // whatever option the browser puts first. Prefer the model that's
        // already warm in the runner (reusing it avoids paying another
        // load/warm-up cycle) and only fall back to the most recent chat's
        // model -- the "last known model used" -- when nothing is warm right
        // now (e.g. right after server start). Both candidates are checked
        // against the ready list since a warm or previously-used model can
        // have since been removed or gone stale.
        "await selectDefaultChatModel(ready,c.chats);}}"
        "catch(x){location.href='/';}}"
        // Renders the sidebar's chat list as real links to each chat's own
        // URL -- clicking one is a normal page navigation, not a client-side
        // panel swap. Chats arrive newest-first from the server; only the
        // most recent 20 render directly under Chats, and anything older
        // moves into a collapsed History section so the sidebar stays a
        // fixed, scannable size regardless of how many chats exist.
        "const RECENT_CHAT_LIMIT=20;"
        // Each sidebar row is now the chat link plus a bin icon that deletes
        // it -- deletion is permanent (the server drops the chat and every
        // one of its messages), so it's confirmed before the request fires.
        // Deleting the chat currently open just navigates to a blank /app
        // rather than trying to patch the now-gone chat out of the visible
        // panel.
        "function chatLink(c){const row=document.createElement('div');"
        "row.className='chatListItem';"
        "const a=document.createElement('a');"
        "a.className='navButton';a.href='/app/chat/'+encodeURIComponent(c.id);"
        "a.textContent=c.title||c.id;a.title=c.title||c.id;"
        "const del=document.createElement('button');del.type='button';"
        "del.className='chatDeleteBtn';del.title='Delete chat';"
        // An emoji glyph's font metrics can render outside the button's own
        // box (Windows in particular draws color emoji with extra vertical
        // padding baked into the glyph), so the visible icon and the actual
        // clickable rectangle can end up misaligned enough that a click on
        // what looks like the icon lands outside it. A fixed-size inline SVG
        // guarantees the drawn icon and the hit target are the same box.
        "del.setAttribute('aria-label','Delete chat');"
        "del.innerHTML='<svg viewBox=\"0 0 24 24\" width=\"15\" height=\"15\" "
        "fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" "
        "stroke-linecap=\"round\" stroke-linejoin=\"round\">"
        "<path d=\"M4 7h16\"/><path d=\"M10 11v6\"/><path d=\"M14 11v6\"/>"
        "<path d=\"M6 7l1 13a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2l1-13\"/>"
        "<path d=\"M9 7V4a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1v3\"/></svg>';"
        "del.addEventListener('click',async e=>{e.preventDefault();e.stopPropagation();"
        // confirm() temporarily removed for diagnosis: Chrome silently
        // no-ops window.confirm() with zero console output if the user
        // previously ticked "Prevent this page from creating additional
        // dialogs" on an earlier alert/confirm -- that reads identically to
        // "the button does nothing," so ruling it out here.
        "try{await api('/api/v1/chats/'+encodeURIComponent(c.id)+'/delete','POST');"
        "if(q('#messageChat')&&q('#messageChat').value===c.id){location.href='/app';return;}"
        "await load();}"
        "catch(x){const s=q('#actionStatus');"
        "showSystemError('Delete failed: '+x.message);}});"
        "row.append(a,del);return row;}"
        "function renderChatList(chats){lastChats=chats;"
        "const list=q('#chatList');if(!list)return;"
        "list.replaceChildren();"
        "for(const c of chats.slice(0,RECENT_CHAT_LIMIT))list.append(chatLink(c));"
        "const older=chats.slice(RECENT_CHAT_LIMIT);"
        "const historySection=q('#chatHistorySection');"
        "const historyList=q('#chatHistoryList');"
        "if(historySection&&historyList){"
        "historySection.hidden=older.length===0;historyList.replaceChildren();"
        "for(const c of older)historyList.append(chatLink(c));}}"
        // Keeps remembered details inspectable and removable. The section is
        // collapsed by default in the sidebar, preserving the uncluttered
        // chat layout while making automatic captures transparent.
        "function renderMemories(memories){const list=q('#memoryList');if(!list)return;"
        "list.replaceChildren();if(!memories.length){"
        "const empty=document.createElement('div');empty.className='memoryEmpty';"
        "empty.textContent='No saved details.';list.append(empty);return;}"
        "for(const memory of memories){const row=document.createElement('div');"
        "row.className='memoryItem';const text=document.createElement('span');"
        "text.textContent=memory.content;text.title=memory.source==='auto'?"
        "'Remembered automatically':'Saved explicitly';row.append(text);"
        "if(q('#newMemory')){const del=document.createElement('button');"
        "del.type='button';del.className='memoryDeleteBtn';del.textContent='\u00d7';"
        "del.title='Forget this detail';del.setAttribute('aria-label','Forget detail');"
        "del.addEventListener('click',async()=>{try{await api('/api/v1/memories/'+"
        "encodeURIComponent(memory.id)+'/delete','POST');await refreshMemories();}"
        "catch(x){showSystemError('Could not forget detail: '+x.message);}});"
        "row.append(del);}list.append(row);}}"
        "async function refreshMemories(){const data=await api('/api/v1/memories');"
        "renderMemories(data.memories||[]);}"
        "async function addMemory(e){e.preventDefault();const input=q('#memoryContent');"
        "const content=input.value.trim();if(!content)return;try{"
        "await api('/api/v1/memories','POST',{content});input.value='';"
        "await refreshMemories();}catch(x){showSystemError('Could not save detail: '+x.message);}}"
        // Loads one chat's full message history (via the GET
        // /api/v1/chats/{id} route) into the chat panel.
        "async function openChat(id){const s=q('#actionStatus');"
        "try{const chat=await api('/api/v1/chats/'+encodeURIComponent(id));"
        "q('#messageChat').value=id;openedProjectId=chat.projectId;"
        "if(q('#chatEmpty'))q('#chatEmpty').hidden=true;"
        // The model can still be switched after the first message (see
        // changeChatModel()) -- only the project is fixed for the chat's
        // lifetime, since it's tied to the attachments/retrieval already
        // scoped against it.
        "const modelSelect=q('#chatModel');"
        // Setting .value to an id that isn't one of the picker's current
        // options (the model this chat was created with may not be in the
        // "ready" list right now -- unloaded, still downloading, etc.)
        // silently no-ops in every browser, leaving whatever option was
        // already first selected instead of the chat's actual model. Adding
        // a placeholder option for it first guarantees the assignment always
        // sticks, so restoring a chat always shows the model it really uses.
        "if(modelSelect){"
        "if(chat.modelId&&![...modelSelect.options].some(o=>o.value===chat.modelId)){"
        "const placeholder=document.createElement('option');"
        "placeholder.value=chat.modelId;"
        "placeholder.textContent=chat.modelId+' (not currently loaded)';"
        "modelSelect.append(placeholder);}"
        "modelSelect.value=chat.modelId;refreshModelSettingsPanel();}"
        "if(q('#chatProject'))q('#chatProject').disabled=true;"
        "const box=q('#chatMessages');box.replaceChildren();"
        "for(const message of chat.messages)"
        "appendMessage(box,message.role,message.content,message.tokenCount);"
        "box.scrollTop=box.scrollHeight;"
        // Warming used to be a side effect of the GET above (fired the
        // instant a chat was opened, mid-load -- before the browser had
        // actually painted this history or the models dropdown from the
        // request racing it). It's triggered explicitly now, and only
        // after two animation frames have actually elapsed, which
        // guarantees a real paint has happened -- so the full page
        // (sidebar, model list, this chat's history) is genuinely on
        // screen before a multi-gigabyte cold load starts competing with
        // it for CPU/disk. Fire-and-forget: a failure here just means the
        // pre-warm head start didn't happen, not a page error.
        "if(chat.modelId)requestAnimationFrame(()=>requestAnimationFrame(()=>"
        "api('/api/v1/runner/warm','POST',{modelId:chat.modelId}).catch(()=>{})));}"
        "catch(x){showSystemError('Failed to load chat: '+x.message);}}"
        // Bubble side and color already say who's speaking; only the system
        // role (rendered plain, centered) still needs a label. User/assistant
        // content renders through renderMarkdown() below (fenced code
        // blocks, bullet/numbered lists, inline code, bold) instead of raw
        // textContent, so multi-line replies read like a real chat client
        // rather than one run-on paragraph. The raw source is kept on the
        // element itself so streamed tokens can be re-rendered as they
        // arrive (see streamMessage()).
        // Writes the bubble's title row as 'Query (Tokens: N)' / 'Response
        // (Tokens: N)' -- the count is the runner's own figure for that
        // message (prompt tokens evaluated for a query, tokens generated
        // for a response). A count of 0 means "not recorded" (older
        // messages, or a turn that failed first) and shows no suffix.
        "function setMsgTokens(msgEl,role,tokens){"
        "const title=msgEl.querySelector('.chatMsg-responseTitle');"
        "if(!title)return;"
        "title.textContent=(role==='assistant'?'Response':'Query')+"
        "(tokens>0?' (Tokens: '+tokens+')':'');}"
        "function appendMessage(box,role,content,tokenCount){"
        "const p=document.createElement('div');"
        "p.className='chatMsg chatMsg-'+role;"
        "if(role==='system'){p.textContent='system: '+content;}"
        "else{p.dataset.raw=content;"
        // Assistant replies get a 'Response' title and user messages get a
        // 'Query' title, matching the error card's own title row -- the
        // copy button (added below) shares that row's top-right corner via
        // .msgCopyBtn's absolute positioning, same as it already does for
        // the error card and every other bubble.
        "if(role==='assistant'||role==='user'){const title=document.createElement('div');"
        "title.className='chatMsg-responseTitle';"
        "p.append(title);"
        // setMsgTokens() owns the title text so the '(Tokens: N)' suffix
        // renders identically here and when streamMessage() back-fills the
        // counts from the stream's 'complete' event.
        "setMsgTokens(p,role,tokenCount||0);}"
        // Rendered content lives in its own .msgBody child rather than
        // directly in p -- streamMessage() rewrites just that child's
        // innerHTML as tokens arrive, so the copy button appended below
        // survives every re-render instead of being wiped out along with
        // the old content each time.
        "const bodyEl=document.createElement('div');bodyEl.className='msgBody';"
        "bodyEl.innerHTML=renderMarkdown(content);p.append(bodyEl);"
        "addMessageCopyButton(p);addCodeCopyButtons(bodyEl);}"
        "box.append(p);return p;}"
        // Writes text to the clipboard and gives the clicked button
        // momentary 'Copied!' feedback so the click registers as having
        // worked, then restores its original label.
        "function copyToClipboard(text,btn){navigator.clipboard.writeText(text||'')"
        ".then(()=>{const old=btn.textContent;btn.textContent='Copied!';"
        "setTimeout(()=>{btn.textContent=old;},1500);});}"
        // One button per message bubble that copies the whole reply's raw
        // source (kept on p.dataset.raw, same field renderMarkdown()
        // re-renders from) -- not the HTML, so pasting elsewhere gets plain
        // text/Markdown rather than this page's markup.
        "function addMessageCopyButton(msgEl){const btn=document.createElement('button');"
        "btn.type='button';btn.className='msgCopyBtn';btn.textContent='Copy';"
        "btn.setAttribute('aria-label','Copy message');"
        "btn.addEventListener('click',()=>copyToClipboard(msgEl.dataset.raw,btn));"
        "msgEl.append(btn);}"
        // One button per fenced code block (renderMarkdown() emits
        // <pre><code>) so a snippet can be copied on its own without the
        // surrounding prose -- idempotent (skips a <pre> that already has
        // one) since streamed re-renders call this again on every token.
        "function addCodeCopyButtons(container){"
        "container.querySelectorAll('pre').forEach(pre=>{"
        "if(pre.querySelector('.codeCopyBtn'))return;"
        "const btn=document.createElement('button');btn.type='button';"
        "btn.className='codeCopyBtn';btn.textContent='Copy';"
        "btn.setAttribute('aria-label','Copy code block');"
        "btn.addEventListener('click',e=>{e.stopPropagation();"
        "const code=pre.querySelector('code');"
        "copyToClipboard(code?code.textContent:pre.textContent,btn);});"
        "pre.append(btn);});}"
        // Best-effort LaTeX-to-HTML for the common constructs model replies
        // actually use (fractions, super/subscripts, Greek letters, common
        // operators) -- not a real TeX engine (this project takes no
        // third-party dependencies), just enough substitution that
        // \\frac{a}{b}, x_i, x^2, and \\theta read as intended instead of
        // showing their raw backslash source. Operates on already
        // HTML-escaped text (see renderInline below), so every replacement
        // only ever introduces the handful of safe tags below, never raw
        // user/model text as markup.
        "function renderMathExpr(t){"
        // Structural LaTeX first, before any symbol substitution: row
        // separators (\\) become line breaks, alignment tabs (a bare &,
        // which is &amp; by the time this runs on escaped text) vanish,
        // \begin{...}/\end{...} environment wrappers vanish, \text{}-style
        // upright-text commands keep just their contents, and \left/\right
        // delimiter-sizing prefixes vanish. Without these, an align*
        // environment leaked as literal 'beginalign*' / 'textradius' text.
        "t=t.replace(/\\\\\\\\\\\\\\\\/g,'<br>');"
        "t=t.replace(/&amp;/g,'');"
        "t=t.replace(/\\\\(?:begin|end)\\{[a-zA-Z]+\\*?\\}/g,'');"
        "t=t.replace(/\\\\(?:text|textbf|textit|mathrm|mathbf|mathit|operatorname)"
        "\\{([^{}]*)\\}/g,(m,a)=>a);"
        "t=t.replace(/\\\\(?:left|right)(?=[^a-zA-Z]|$)/g,'');"
        "t=t.replace(/\\\\frac\\{([^{}]*)\\}\\{([^{}]*)\\}/g,"
        "(m,a,b)=>'<span class=\"frac\"><span class=\"fracNum\">'+a+"
        "'</span><span class=\"fracDen\">'+b+'</span></span>');"
        "t=t.replace(/\\\\sqrt\\{([^{}]*)\\}/g,(m,a)=>'\\u221a('+a+')');"
        "t=t.replace(/\\^\\{([^{}]*)\\}/g,(m,a)=>'<sup>'+a+'</sup>');"
        "t=t.replace(/\\^(\\w)/g,(m,a)=>'<sup>'+a+'</sup>');"
        "t=t.replace(/_\\{([^{}]*)\\}/g,(m,a)=>'<sub>'+a+'</sub>');"
        "t=t.replace(/_(\\w)/g,(m,a)=>'<sub>'+a+'</sub>');"
        "const symbols={alpha:'\\u03b1',beta:'\\u03b2',gamma:'\\u03b3',"
        "delta:'\\u03b4',epsilon:'\\u03b5',zeta:'\\u03b6',eta:'\\u03b7',"
        "theta:'\\u03b8',lambda:'\\u03bb',mu:'\\u03bc',nu:'\\u03bd',"
        "pi:'\\u03c0',rho:'\\u03c1',sigma:'\\u03c3',tau:'\\u03c4',"
        "phi:'\\u03c6',chi:'\\u03c7',psi:'\\u03c8',omega:'\\u03c9',"
        "Delta:'\\u0394',Theta:'\\u0398',Lambda:'\\u039b',Pi:'\\u03a0',"
        "Sigma:'\\u03a3',Phi:'\\u03a6',Psi:'\\u03a8',Omega:'\\u03a9',"
        "cdot:'\\u00b7',times:'\\u00d7',pm:'\\u00b1',mp:'\\u2213',"
        "leq:'\\u2264',geq:'\\u2265',neq:'\\u2260',approx:'\\u2248',"
        "infty:'\\u221e',to:'\\u2192',rightarrow:'\\u2192',"
        "partial:'\\u2202',nabla:'\\u2207',in:'\\u2208',"
        "sum:'\\u2211',int:'\\u222b',prod:'\\u220f'};"
        "t=t.replace(/\\\\([a-zA-Z]+)/g,(m,name)=>"
        "symbols[name]!==undefined?symbols[name]:name);"
        "return t.replace(/[{}]/g,'');}"
        // Turns Markdown-style [label](url) links and bare http(s):// URLs
        // into real anchors. Runs on already-HTML-escaped text (same
        // convention as the rest of renderInline), so a URL/label can never
        // inject markup of its own -- the only tags this ever introduces are
        // the <a> wrapper below. A markdown link's own url is matched first
        // so the bare-URL pass afterward (which requires a preceding
        // whitespace/paren/line-start boundary) never re-wraps the url this
        // already placed inside an href=\"...\" attribute. Trailing
        // punctuation (a period ending the sentence, a closing bracket from
        // surrounding prose) is peeled off the bare-URL match so it reads
        // outside the link instead of becoming part of the href.
        "function linkify(t){"
        "t=t.replace(/\\[([^\\]<>]+)\\]\\((https?:\\/\\/[^\\s()<>]+)\\)/g,"
        "(m,label,url)=>'<a href=\"'+url+'\" target=\"_blank\" "
        "rel=\"noopener noreferrer\">'+label+'</a>');"
        "t=t.replace(/(^|[\\s(])(https?:\\/\\/[^\\s<>()]+)/g,(m,pre,url)=>{"
        "let trail='';while(url&&/[.,;:!?]$/.test(url)){"
        "trail=url.slice(-1)+trail;url=url.slice(0,-1);}"
        "if(!url)return m;"
        "return pre+'<a href=\"'+url+'\" target=\"_blank\" "
        "rel=\"noopener noreferrer\">'+url+'</a>'+trail;});"
        "return t;}"
        // Inline span-level formatting within a single line: linkify (see
        // above), backtick code spans, **bold**, and \\(...\\) inline math.
        // Operates on already-HTML-escaped text so the replacement groups
        // never need escaping themselves.
        "function renderInline(text){let t=esc(text);t=linkify(t);"
        "t=t.replace(/`([^`]+)`/g,(m,c)=>'<code>'+c+'</code>');"
        "t=t.replace(/\\*\\*([^*]+)\\*\\*/g,(m,b)=>'<strong>'+b+'</strong>');"
        "t=t.replace(/\\\\\\((.+?)\\\\\\)/g,"
        "(m,e)=>'<span class=\"math\">'+renderMathExpr(e)+'</span>');"
        "return t;}"
        // Small block-level Markdown renderer covering what model replies
        // actually use: fenced code blocks, bullet and numbered lists, and
        // paragraphs with soft line breaks. Deliberately not a full Markdown
        // parser -- just enough structure to read code and lists correctly
        // instead of everything collapsing into a single run-on line.
        "function renderMarkdown(raw){const lines=String(raw==null?'':raw)"
        ".replace(/\\r\\n/g,'\\n').split('\\n');let html='',i=0;"
        "while(i<lines.length){const line=lines[i];"
        "const fence=/^```(\\w*)\\s*$/.exec(line);"
        "if(fence){const lang=fence[1];i++;const code=[];"
        "while(i<lines.length&&!/^```\\s*$/.test(lines[i])){code.push(lines[i]);i++;}"
        "i++;const cls=lang?' class=\"language-'+esc(lang)+'\"':'';"
        "html+='<pre><code'+cls+'>'+esc(code.join('\\n'))+'</code></pre>';continue;}"
        // Block math: \\[ ... \\], possibly spanning multiple lines (each
        // line of a wrapped equation is common in model output). Collected
        // as raw (unescaped) source and rendered the same way inline math
        // is, then dropped into its own centered block.
        "if(/^\\s*\\\\\\[/.test(line)){const mathLines=[line];i++;"
        "while(i<lines.length&&!/\\\\\\]\\s*$/.test(mathLines[mathLines.length-1])){"
        "mathLines.push(lines[i]);i++;}"
        "const joined=mathLines.join(' ');"
        "const inner=joined.replace(/^\\s*\\\\\\[/,'').replace(/\\\\\\]\\s*$/,'');"
        "html+='<div class=\"mathBlock\">'+renderMathExpr(esc(inner))+'</div>';continue;}"
        // Bare \\begin{align*}...\\end{align*}-style environments with no
        // \\[ \\] wrapper (how QWEN typically emits display math) get the
        // same centered math-block treatment -- renderMathExpr() itself
        // strips the begin/end wrappers, alignment tabs, and \\text{}.
        "if(/^\\s*\\\\begin\\{/.test(line)){const mathLines=[line];i++;"
        "while(i<lines.length&&!/\\\\end\\{[^}]*\\}\\s*$/"
        ".test(mathLines[mathLines.length-1])){mathLines.push(lines[i]);i++;}"
        "html+='<div class=\"mathBlock\">'+renderMathExpr(esc(mathLines.join(' ')))+"
        "'</div>';continue;}"
        // $$ ... $$ display math (the other delimiter models commonly use),
        // either on one line or spanning several until the closing $$.
        "if(/^\\s*\\$\\$/.test(line)){"
        "const mathLines=[line.replace(/^\\s*\\$\\$/,'')];i++;"
        "while(i<lines.length&&!/\\$\\$\\s*$/"
        ".test(mathLines[mathLines.length-1])){mathLines.push(lines[i]);i++;}"
        "const dollarInner=mathLines.join(' ').replace(/\\$\\$\\s*$/,'');"
        "html+='<div class=\"mathBlock\">'+renderMathExpr(esc(dollarInner))+"
        "'</div>';continue;}"
        // A single blank line between two list items (common in model
        // output that puts a blank line after every bullet for readability)
        // must not end the list -- otherwise each item becomes its own
        // one-item <ol>/<ul>, which is what made every ordered item render
        // as \"1.\" instead of counting up. Only a non-list line (or two
        // blank lines in a row) actually ends the list.
        "if(/^\\s*[-*]\\s+/.test(line)){const items=[];"
        "while(i<lines.length){"
        "if(/^\\s*[-*]\\s+/.test(lines[i])){"
        "items.push(renderInline(lines[i].replace(/^\\s*[-*]\\s+/,'')));i++;}"
        "else if(lines[i].trim()===''&&i+1<lines.length&&"
        "/^\\s*[-*]\\s+/.test(lines[i+1])){i++;}"
        "else break;}"
        "html+='<ul>'+items.map(x=>'<li>'+x+'</li>').join('')+'</ul>';continue;}"
        "if(/^\\s*\\d+[.)]\\s+/.test(line)){const items=[];"
        "while(i<lines.length){"
        "const m=/^\\s*(\\d+)[.)]\\s+/.exec(lines[i]);"
        "if(m){items.push({n:parseInt(m[1],10),"
        "text:renderInline(lines[i].replace(/^\\s*\\d+[.)]\\s+/,''))});i++;}"
        "else if(lines[i].trim()===''&&i+1<lines.length&&"
        "/^\\s*\\d+[.)]\\s+/.test(lines[i+1])){i++;}"
        "else break;}"
        // The model's own leading numbers are preserved instead of letting
        // the browser silently renumber every list from 1 -- start=
        // anchors the first item and a per-item value= carries the rest, so
        // a list that begins mid-sequence (e.g. continuing after a code
        // block) or skips a number still renders exactly as written.
        "const start=items.length?items[0].n:1;"
        "html+='<ol start=\"'+start+'\">'+items.map(x=>"
        "'<li value=\"'+x.n+'\">'+x.text+'</li>').join('')+'</ol>';continue;}"
        "if(line.trim()===''){i++;continue;}"
        "const para=[];"
        // A math block starting mid-paragraph (\\[, \\begin{...}, or $$ on
        // its own line right after prose) must end the paragraph, or the
        // paragraph loop swallows it and it never reaches the block
        // handlers above.
        "while(i<lines.length&&lines[i].trim()!==''&&!/^```/.test(lines[i])&&"
        "!/^\\s*[-*]\\s+/.test(lines[i])&&!/^\\s*\\d+[.)]\\s+/.test(lines[i])&&"
        "!/^\\s*\\\\\\[/.test(lines[i])&&!/^\\s*\\\\begin\\{/.test(lines[i])&&"
        "!/^\\s*\\$\\$/.test(lines[i])){"
        "para.push(renderInline(lines[i]));i++;}"
        "html+='<p>'+para.join('<br>')+'</p>';}"
        "return html;}"
        // Streaming re-render helper: instead of wholesale innerHTML
        // replacement on every token (which destroyed and recreated every
        // element -- resetting each code block's scroll position to the
        // top and making its scrollbar flicker), diff the freshly rendered
        // blocks against the live DOM and touch only what changed. An
        // unchanged block keeps its exact DOM node (scroll state, copy
        // button and all); the one block still growing has its <code> text
        // swapped in place, keeps its horizontal scroll, and follows its
        // own bottom while the user hasn't scrolled up inside it.
        "function patchMsgBody(el,html){"
        "const tpl=document.createElement('template');tpl.innerHTML=html;"
        "const next=Array.from(tpl.content.children);"
        "for(let i=0;i<next.length;i++){"
        "const a=el.children[i],b=next[i];"
        "if(!a){el.append(b);continue;}"
        "if(a.tagName==='PRE'&&b.tagName==='PRE'){"
        "const ac=a.querySelector('code'),bc=b.querySelector('code');"
        "if(ac&&bc){"
        "if(ac.textContent!==bc.textContent||ac.className!==bc.className){"
        // 'At the bottom' is measured before the update so a user who
        // scrolled up to read stays put; anyone at (or within a few px of)
        // the bottom keeps following the incoming code.
        "const follow=a.scrollTop+a.clientHeight>=a.scrollHeight-8;"
        "const left=a.scrollLeft;"
        "ac.className=bc.className;ac.textContent=bc.textContent;"
        "a.scrollLeft=left;"
        "if(follow)a.scrollTop=a.scrollHeight;}"
        "continue;}}"
        "if(a.outerHTML!==b.outerHTML)a.replaceWith(b);}"
        "while(el.children.length>next.length)el.lastElementChild.remove();}"
        // Escapes text for safe insertion as HTML content elsewhere in this
        // file (table cells built from trusted-looking but user-supplied
        // strings such as display names and diagnostics).
        "function esc(s){const d=document.createElement('div');"
        "d.textContent=s==null?'':String(s);return d.innerHTML;}"
        // Shortens long id/hash/name values (dataset ids, SHA digests, etc.)
        // so a table column never forces the page wider than the window --
        // the full value is still available on hover via the title
        // attribute. Used in place of esc() for any field whose value is
        // not a short human-authored label.
        "function escTrim(s,n){const full=s==null?'':String(s);"
        "if(full.length<=n)return esc(full);"
        "return '<span title=\"'+esc(full)+'\">'+esc(full.slice(0,n-1))+"
        "'\\u2026</span>';}"
        // Small inline-SVG icon set (Bootstrap-Icons-style outline glyphs)
        // used by the row-action toolbar buttons below instead of shipping
        // an icon font -- this app has no CDN/network dependency and none
        // of these screens warrant one just for icons.
        "const ICONS={"
        "check:'<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" "
        "fill=\"currentColor\"><path d=\"M13.7 3.3a1 1 0 0 1 0 1.4l-7 7a1 "
        "1 0 0 1-1.4 0l-3.5-3.5a1 1 0 1 1 1.4-1.4L6 9.6l6.3-6.3a1 1 0 0 1 "
        "1.4 0z\"/></svg>',"
        "trash:'<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" "
        "fill=\"currentColor\"><path d=\"M5.5 1a.5.5 0 0 0-.5.5V2H2.5a.5.5 "
        "0 0 0 0 1H3v10.5A1.5 1.5 0 0 0 4.5 15h7a1.5 1.5 0 0 0 "
        "1.5-1.5V3h.5a.5.5 0 0 0 0-1H11v-.5a.5.5 0 0 0-.5-.5h-5zM4 3h8v10.5a"
        ".5.5 0 0 1-.5.5h-7a.5.5 0 0 1-.5-.5V3zm2 2a.5.5 0 0 1 .5.5v6a.5.5 "
        "0 0 1-1 0v-6A.5.5 0 0 1 6 5zm4 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 "
        "1-1 0v-6a.5.5 0 0 1 .5-.5z\"/></svg>',"
        "x:'<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" "
        "fill=\"currentColor\"><path d=\"M4.3 4.3a1 1 0 0 1 1.4 0L8 6.6l"
        "2.3-2.3a1 1 0 1 1 1.4 1.4L9.4 8l2.3 2.3a1 1 0 0 1-1.4 1.4L8 9.4l"
        "-2.3 2.3a1 1 0 0 1-1.4-1.4L6.6 8 4.3 5.7a1 1 0 0 1 0-1.4z\"/>"
        "</svg>',"
        "play:'<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" "
        "fill=\"currentColor\"><path d=\"M4 2.5a.5.5 0 0 1 .77-.42l9 5.5a."
        "5.5 0 0 1 0 .84l-9 5.5A.5.5 0 0 1 4 13.5v-11z\"/></svg>',"
        "eye:'<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" "
        "fill=\"currentColor\"><path d=\"M16 8s-3-5.5-8-5.5S0 8 0 8s3 5.5 "
        "8 5.5S16 8 16 8zM1.173 8a13.13 13.13 0 0 1 1.66-2.043C4.12 4.668 "
        "5.88 3.5 8 3.5c2.12 0 3.879 1.168 5.168 2.457A13.13 13.13 0 0 1 "
        "14.828 8c-.058.087-.122.183-.195.288-.335.48-.83 1.12-1.465 "
        "1.755C11.879 11.332 10.119 12.5 8 12.5c-2.12 0-3.879-1.168-"
        "5.168-2.457A13.13 13.13 0 0 1 1.172 8z\"/><path d=\"M8 5.5a2.5 "
        "2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5zM4.5 8a3.5 3.5 0 1 1 7 0 3.5 3.5 "
        "0 0 1-7 0z\"/></svg>',"
        "gear:'<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" "
        "fill=\"currentColor\"><path d=\"M9.405 1.05c-.413-1.4-2.397-1.4-"
        "2.81 0l-.1.34a1.464 1.464 0 0 1-2.105.872l-.31-.17c-1.283-.698-"
        "2.686.705-1.987 1.987l.169.311c.446.82.023 1.841-.872 2.105l-.34."
        "1c-1.4.413-1.4 2.397 0 2.81l.34.1a1.464 1.464 0 0 1 .872 2.105l-"
        ".17.31c-.698 1.283.705 2.686 1.987 1.987l.311-.169a1.464 1.464 0 "
        "0 1 2.105.872l.1.34c.413 1.4 2.397 1.4 2.81 0l.1-.34a1.464 1.464 "
        "0 0 1 2.105-.872l.31.17c1.283.698 2.686-.705 1.987-1.987l-.169-"
        ".311a1.464 1.464 0 0 1 .872-2.105l.34-.1c1.4-.413 1.4-2.397 0-"
        "2.81l-.34-.1a1.464 1.464 0 0 1-.872-2.105l.17-.31c.698-1.283-"
        ".705-2.686-1.987-1.987l-.311.169a1.464 1.464 0 0 1-2.105-.872l-"
        ".1-.34zM8 10.93a2.929 2.929 0 1 1 0-5.858 2.929 2.929 0 0 1 0 "
        "5.858z\"/></svg>',"
        "pause:'<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" "
        "fill=\"currentColor\"><path d=\"M5 3.5A1.5 1.5 0 0 1 6.5 5v6a1.5 "
        "1.5 0 0 1-3 0V5A1.5 1.5 0 0 1 5 3.5zm6 0A1.5 1.5 0 0 1 12.5 5v6a"
        "1.5 1.5 0 0 1-3 0V5A1.5 1.5 0 0 1 11 3.5z\"/></svg>',"
        "stop:'<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" "
        "fill=\"currentColor\"><path d=\"M3.5 3.5A1 1 0 0 1 4.5 2.5h7a1 1 "
        "0 0 1 1 1v7a1 1 0 0 1-1 1h-7a1 1 0 0 1-1-1v-7z\"/></svg>'};"
        // Renders a single icon-only action button. `label` is both the
        // tooltip and the accessible name since the button carries no
        // visible text -- callers group several of these in a
        // '.rowToolbar' div that sits above the row's descriptive text
        // instead of stretching the row with full-width text buttons.
        // `extraAttr` is an optional raw HTML attribute (e.g. 'disabled').
        "function iconBtn(icon,label,attr,id,extraClass,extraAttr){"
        "return '<button type=\"button\" class=\"iconBtn'+"
        "(extraClass?' '+extraClass:'')+'\" data-'+attr+'=\"'+id+"
        "'\" title=\"'+esc(label)+'\" aria-label=\"'+esc(label)+'\"'+"
        "(extraAttr?' '+extraAttr:'')+'>'+"
        "ICONS[icon]+'</button>';}"
        "function applyBtn(attr,id){return iconBtn('check','Apply',attr,id,"
        "'iconBtn-apply');}"
        "function deleteBtn(attr,id){return iconBtn('trash','Delete',attr,"
        "id,'iconBtn-delete');}"
        "function approveBtn(attr,id){return iconBtn('check','Approve',"
        "attr,id,'iconBtn-apply');}"
        "function rejectBtn(attr,id){return iconBtn('x','Reject',attr,id,"
        "'iconBtn-delete');}"
        "function runBtn(attr,id,label){return iconBtn('play',"
        "label||'Run',attr,id,'');}"
        "function viewBtn(attr,id,label){return iconBtn('eye',"
        "label||'View',attr,id,'');}"
        "function configureBtn(attr,id){return iconBtn('gear','Configure',"
        "attr,id,'');}"
        "function toolbar(){return '<div class=\"rowToolbar\">'+"
        "Array.prototype.slice.call(arguments).join('')+'</div>';}"
        // Builds a simple two-column-plus-actions HTML table from rows,
        // replacing the raw JSON dumps every list used to show verbatim --
        // this is a user-facing screen, not a debugging console.
        // Wrapped in a scrollable div so a wide table (many ML tables run
        // to 6-7 columns of status pickers and action buttons) scrolls
        // inside its own box instead of forcing the whole page wider.
        "function table(headers,rows){if(!rows.length)return'<p>Nothing here yet.</p>';"
        "let h='<div style=\"overflow-x:auto\"><table><thead><tr>';"
        "for(const header of headers)h+='<th>'+esc(header)+'</th>';"
        "h+='</tr></thead><tbody>';"
        "for(const row of rows){h+='<tr>';for(const cell of row)h+='<td>'+cell+'</td>';h+='</tr>';}"
        "return h+'</tbody></table></div>';}"
        "function renderProjects(projects){const el=q('#projectsList');if(!el)return;"
        "el.innerHTML=table(['Project','ID'],projects.map(x=>"
        "[esc(x.displayName),esc(x.id)]));}"
        "function renderModels(models){const el=q('#modelsList');if(!el)return;"
        "el.innerHTML=table(['Model','Category','Status','Recommended RAM','Notes'],"
        "models.map(x=>[esc(x.displayName),esc(x.category),"
        "'<span class=\"stateTag stateTag-'+esc(x.state)+'\">'+esc(x.state)+'</span>',"
        "x.recommendedRamMiB?Math.round(x.recommendedRamMiB/1024)+' GB':'-',"
        "esc(x.diagnostic)]));}"
        "function renderBenchmarks(benchmarks){const el=q('#benchmarksList');if(!el)return;"
        "el.innerHTML=table(['Model','Profile','Passed','Tokens/sec'],"
        "benchmarks.map(x=>[esc(x.modelId),esc(x.profile),"
        "x.passedCases+' / '+x.totalCases,Number(x.tokensPerSecond).toFixed(1)]));}"
        "function renderUsers(users){const el=q('#usersList');if(!el)return;"
        "el.innerHTML=users.length?table(['User','Display name','Role'],"
        "users.map(x=>[esc(x.username),esc(x.displayName),esc(x.role)])):"
        "'<p>No users visible, or administrator access is required.</p>';}"
        // Phase 30A system-configuration editor: reads/writes settings.json
        // fields by dotted path (matching each input's data-path attribute)
        // instead of one hand-written getter/setter pair per field --
        // cfgGet/cfgSet below are the only place that indirection lives.
        "function cfgGet(obj,path){return path.split('.').reduce("
        "(v,k)=>(v==null?v:v[k]),obj);}"
        "function cfgSet(obj,path,value){const keys=path.split('.');"
        "let node=obj;for(let i=0;i<keys.length-1;i++)node=node[keys[i]];"
        "node[keys[keys.length-1]]=value;}"
        // Keeps the full document last loaded from the server so submit can
        // overlay only the fields this form actually exposes back onto it --
        // fields the form never shows (host/port, TLS material, storage
        // roots, allow-lists, ...) are round-tripped unchanged instead of
        // being reconstructed (and possibly gotten wrong) client-side.
        "let systemConfigDocument=null;"
        "function renderSystemConfig(cfg){const form=q('#systemConfigForm');"
        "if(!form)return;if(!cfg){"
        "q('#systemConfigStatus').textContent="
        "'System configuration is unavailable (administrator access, or a "
        "running settings.json path, is required).';return;}"
        "systemConfigDocument=cfg;"
        "form.querySelectorAll('[data-path]').forEach(input=>{"
        "const value=cfgGet(cfg,input.dataset.path);if(value===undefined)return;"
        "if(input.dataset.type==='bool')input.checked=!!value;"
        "else input.value=value;});}"
        "async function submitSystemConfig(e){e.preventDefault();"
        "const status=q('#systemConfigStatus');status.textContent='Saving...';"
        "try{if(!systemConfigDocument)"
        "throw new Error('configuration was not loaded');"
        "const updated=JSON.parse(JSON.stringify(systemConfigDocument));"
        "q('#systemConfigForm').querySelectorAll('[data-path]').forEach("
        "input=>{const value=input.dataset.type==='bool'?input.checked:"
        "(input.type==='number'?Number(input.value):input.value);"
        "cfgSet(updated,input.dataset.path,value);});"
        "const result=await api('/api/v1/admin/config','POST',updated);"
        "systemConfigDocument=result.configuration;"
        "status.textContent=result.restartRequired?"
        "'Saved. Some changed fields only take effect after MasterAI is "
        "restarted.':'Saved and applied.';}"
        "catch(x){showSystemError('Could not save configuration: '+x.message);"
        "status.textContent='';}}"
        // System Report (Report sidebar): renders the single consolidated
        // read from GET /api/v1/system/report. Early-returns when the
        // target element is absent (any non-administrator page load, and
        // any page other than the report itself) rather than assuming this
        // section is always present -- same convention as every other
        // render*() helper here.
        "function mib(x){return Number(x).toLocaleString()+' MiB';}"
        "function gib(mibValue){return (Number(mibValue)/1024).toFixed(1)+"
        "' GiB';}"
        "function bytesGib(x){return (Number(x)/1073741824).toFixed(2)+"
        "' GiB';}"
        "function renderSystemReport(report){const el=q('#systemReport');"
        "if(!el)return;if(!report){el.textContent="
        "'System Report is unavailable (administrator access is required).';"
        "return;}"
        "const h=report.hardware,p=report.process,mem=report.memory,"
        "spf=report.systemPageFile,pf=report.pageFile,f=report.features;"
        "const row=(label,value)=>'<tr><td class=\"reportLabel\">'+esc(label)+"
        "'</td><td class=\"reportValue\">'+value+'</td></tr>';"
        "const section=(title,rows)=>'<div class=\"reportSection\">"
        "<h3>'+esc(title)+'</h3><table><tbody>'+rows.join('')+"
        "'</tbody></table></div>';"
        "el.innerHTML="
        "section('Hardware',["
        "row('Platform / architecture',esc(h.platform)+' / '+esc(h.architecture)),"
        "row('CPUs',h.physicalCpus+' physical / '+h.logicalCpus+' logical, '+"
        "h.numaNodes+' NUMA node(s)'),"
        "row('System RAM',gib(h.totalRamMiB)+' total, '+gib(h.availableRamMiB)+"
        "' available'),"
        "row('GPU memory',h.gpuMemoryMiB?gib(h.gpuMemoryMiB):'none detected'),"
        "row('GPU backends',h.gpuBackends.length?h.gpuBackends.map(esc)."
        "join(', '):'none')"
        "])+"
        "section('System pagefile',["
        "row('Commit limit (RAM + system pagefile)',gib(spf.totalVirtualMemoryMiB)),"
        "row('Available',gib(spf.availableVirtualMemoryMiB))"
        "])+"
        "section('MasterAI PageFile (virtual pagefile substitute)',["
        "row('Location',esc(pf.location)+(pf.usingDefault?"
        "' (default -- set Storage &gt; PageFile location in System "
        "configuration to move it)':' (custom location)')),"
        // Spelled out as total/used/free rather than "capacity / free" --
        // that paired phrasing read as "used / free" at a glance, making an
        // almost-empty drive (e.g. 40 GiB total, 39.9 GiB free) look like it
        // had consumed nearly all of its own capacity.
        "row('Drive (total / used / free)',gib(pf.driveCapacityMiB)+' / '+"
        "gib(pf.driveCapacityMiB-pf.driveFreeMiB)+' / '+gib(pf.driveFreeMiB)),"
        "row('MasterAI usage (used / capacity)',bytesGib(pf.usedBytes)+' / '+"
        "bytesGib(pf.capacityBytes))"
        "])+"
        "section('Memory pressure',["
        "row('Pressure level',esc(mem.pressure||'')),"
        "row('Reserved',bytesGib(mem.reservedBytes||0))"
        "])+"
        "section('Process',["
        "row('Resident / private memory',bytesGib(p.residentMemoryBytes)+"
        "' / '+bytesGib(p.privateMemoryBytes)),"
        "row('Commit',bytesGib(p.commitBytes)),"
        "row('Page faults',Number(p.pageFaults).toLocaleString())"
        "])+"
        "section('Enabled features',["
        "row('Accelerator policy',esc(f.acceleratorPolicy)),"
        "row('Retrieval',f.retrievalEnabled?'enabled':'disabled'),"
        "row('Cache',f.cacheEnabled?'enabled':'disabled'),"
        "row('Session reuse',f.sessionReuseEnabled?'enabled':'disabled'),"
        "row('Automatic calibration',f.performanceAutoTune?'enabled':"
        "'disabled'),"
        "row('Project file watching',f.watchProjectFiles?'enabled':"
        "'disabled'),"
        "row('Local password accounts',f.allowLocalPasswordAccounts?"
        "'allowed':'not allowed'),"
        "row('OS-integrated sign-in',f.allowOsIdentityAccounts?'allowed':"
        "'not allowed'),"
        "row('TLS mode',esc(f.tlsMode))"
        "]);}"
        // Renders the Machine Learning foundation page: an acknowledgement
        // line, the (currently always zero) dashboard counts, and the full
        // interface roadmap with each entry tagged available/planned -- see
        // MachineLearningRegistry's class comment in masterai.hpp.
        "function renderMlDashboard(ml){const ack=q('#mlAck');if(!ack)return;"
        "ack.textContent=ml.enabled?"
        "'Machine Learning module is enabled ('+ml.phase+' phase).':"
        "'Machine Learning module is unavailable.';"
        "q('#mlStats').innerHTML=table(['Metric','Count'],["
        "['Active projects','activeProjects'],"
        "['Models training','modelsTraining'],"
        "['Models awaiting evaluation','modelsAwaitingEvaluation'],"
        "['Models awaiting approval','modelsAwaitingApproval'],"
        "['Deployed models','deployedModels'],"
        "['Failed training jobs','failedTrainingJobs']]"
        ".map(([label,key])=>[label,String(ml[key])]));"
        "q('#mlInterfaces').innerHTML=table(['Interface','Status'],"
        "ml.interfaces.map(x=>[esc(x.label),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+esc(x.status)+"
        "'</span>']));}"
        // Renders the ML Projects list with a Delete button per row -- the
        // only mutation this foundation phase supports beyond create, since
        // status transitions belong to the training/evaluation/deployment
        // phases that don't exist yet (see MLProjectStore's comment).
        "function renderMlProjects(projects){const el=q('#mlProjectsList');"
        "if(!el)return;"
        "if(!projects.length){el.innerHTML='<p>No Machine Learning projects "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Subject domain','Model task','Status',''],"
        "projects.map(x=>[esc(x.name),esc(x.subjectDomain),esc(x.modelTask),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+esc(x.status)+"
        "'</span>',"
        "deleteBtn('delete-ml-project',x.id)]));"
        "for(const btn of el.querySelectorAll('[data-delete-ml-project]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/projects/'+"
        "encodeURIComponent(btn.dataset.deleteMlProject)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete Machine Learning project failed: '+"
        "x.message);}});}}"
        // Model Registry (docs/PLAN.md "Machine Learning Abilities" section
        // 7): each row carries its own lifecycle-state dropdown so an
        // administrator can move a model along its states one deliberate
        // step at a time, plus a Delete button. The dropdown always lists
        // every known state -- the server enforces the one hard rule
        // (production requires approved/staging first, see
        // ModelRegistryStore::set_state) and reports the reason back if it
        // rejects the change.
        "const MODEL_STATES=['imported','unverified','verified','training',"
        "'evaluation','rejected','approved','staging','production',"
        "'deprecated','archived','quarantined'];"
        "function renderMlModels(models){const el=q('#mlModelsList');"
        "if(!el)return;"
        "if(!models.length){el.innerHTML='<p>No models registered yet.</p>';"
        "return;}"
        "el.innerHTML=table(['Name','Version','Family','Task','State','Set state'],"
        "models.map(x=>[esc(x.displayName||x.name),esc(x.version),"
        "esc(x.family),esc(x.task),"
        "'<span class=\"stateTag stateTag-'+esc(x.state)+'\">'+esc(x.state)+"
        "'</span>',"
        "toolbar('<select data-state-for=\"'+x.id+'\">'+MODEL_STATES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.state?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',applyBtn('apply-state',x.id),"
        "deleteBtn('delete-ml-model',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-apply-state]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyState;"
        "const state=el.querySelector('[data-state-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/models/'+encodeURIComponent(id)+'/state',"
        "'POST',{state});await load();}"
        "catch(x){showSystemError('Update model state failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-model]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/models/'+"
        "encodeURIComponent(btn.dataset.deleteMlModel)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete model failed: '+x.message);}});}}"
        // Dataset Manager (docs/PLAN.md "Machine Learning Abilities"
        // section 10): each row shows its approval status plus Approve,
        // Reject, and Delete actions.
        "function renderMlDatasets(datasets){const el=q('#mlDatasetsList');"
        "if(!el)return;"
        "if(!datasets.length){el.innerHTML='<p>No datasets registered yet.</p>';"
        "return;}"
        "el.innerHTML=table(['Name','Subject area','Format','Approval',''],"
        "datasets.map(x=>[esc(x.name),esc(x.subjectArea),esc(x.dataFormat),"
        "'<span class=\"stateTag stateTag-'+esc(x.approvalStatus)+'\">'+"
        "esc(x.approvalStatus)+'</span>',"
        "toolbar(approveBtn('approve-ml-dataset',x.id),"
        "rejectBtn('reject-ml-dataset',x.id),"
        "deleteBtn('delete-ml-dataset',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-approve-ml-dataset]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/datasets/'+"
        "encodeURIComponent(btn.dataset.approveMlDataset)+'/approve','POST',"
        "{status:'approved'});await load();}"
        "catch(x){showSystemError('Approve dataset failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-reject-ml-dataset]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/datasets/'+"
        "encodeURIComponent(btn.dataset.rejectMlDataset)+'/approve','POST',"
        "{status:'rejected'});await load();}"
        "catch(x){showSystemError('Reject dataset failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-dataset]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/datasets/'+"
        "encodeURIComponent(btn.dataset.deleteMlDataset)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete dataset failed: '+x.message);}});}}"
        // Subject Knowledge Manager (docs/PLAN.md "Machine Learning
        // Abilities" section 12): each row carries its own review-status
        // dropdown, mirroring the Model Registry's set-state pattern above,
        // plus a Delete button.
        "const SUBJECT_REVIEW_STATUSES=['draft','in_review','approved',"
        "'needs_revision','retired'];"
        "function renderMlSubjects(subjects){const el=q('#mlSubjectsList');"
        "if(!el)return;"
        "if(!subjects.length){el.innerHTML='<p>No subject packages "
        "registered yet.</p>';return;}"
        "el.innerHTML=table(['Name','Scope','Target audience','Review "
        "status','Set status'],"
        "subjects.map(x=>[esc(x.name),esc(x.scope),esc(x.targetAudience),"
        "'<span class=\"stateTag stateTag-'+esc(x.reviewStatus)+'\">'+"
        "esc(x.reviewStatus)+'</span>',"
        "toolbar('<select data-review-status-for=\"'+x.id+'\">'+"
        "SUBJECT_REVIEW_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.reviewStatus?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-review-status',x.id),"
        "deleteBtn('delete-ml-subject',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-apply-review-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyReviewStatus;"
        "const status=el.querySelector("
        "'[data-review-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/subjects/'+encodeURIComponent(id)+"
        "'/review-status','POST',{status});await load();}"
        "catch(x){showSystemError('Update subject review status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-subject]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/subjects/'+"
        "encodeURIComponent(btn.dataset.deleteMlSubject)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete subject failed: '+x.message);}});}}"
        // Phases 58-59: ingested source files and their measured index
        // footprint. SHA-256 is shortened only for display; the API retains
        // the complete digest.
        "function renderMlKnowledgeDocuments(documents){"
        "const el=q('#mlKnowledgeDocumentsList');if(!el)return;"
        "if(!documents.length){el.innerHTML='<p>No knowledge files ingested yet.</p>';"
        "return;}el.innerHTML=table(['File','Subject','Vector store','Bytes',"
        "'Chunks','SHA-256',''],documents.map(x=>[escTrim(x.fileName,28),"
        "escTrim(x.subjectId,16),escTrim(x.vectorStoreId,16),"
        "String(x.byteCount),String(x.chunkCount),"
        "'<code title=\"'+esc(x.sha256)+'\">'+esc(x.sha256.slice(0,12))+"
        "'...</code>',deleteBtn('delete-ml-knowledge',x.id)]));"
        "for(const btn of el.querySelectorAll('[data-delete-ml-knowledge]')){"
        "btn.addEventListener('click',async()=>{try{await api("
        "'/api/v1/ml/knowledge-documents/'+encodeURIComponent("
        "btn.dataset.deleteMlKnowledge)+'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete knowledge file failed: '+x.message);}});}}"
        // Data Labeling (docs/PLAN.md "Machine Learning Abilities" section
        // 14): each row carries its own status dropdown, mirroring the
        // Subject Knowledge Manager's review-status pattern above, plus a
        // Delete button.
        "const LABEL_TASK_STATUSES=['queued','in_progress','in_review',"
        "'completed'];"
        "function renderMlLabelTasks(tasks){const el=q('#mlLabelTasksList');"
        "if(!el)return;"
        "if(!tasks.length){el.innerHTML='<p>No labeling tasks created "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Dataset','Label mode','Assignee',"
        "'Status','Set status'],"
        "tasks.map(x=>[esc(x.name),escTrim(x.datasetId,16),esc(x.labelMode),"
        "escTrim(x.assigneeId,16),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-label-task-status-for=\"'+x.id+'\">'+"
        "LABEL_TASK_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-label-task-status',x.id),"
        "deleteBtn('delete-ml-label-task',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-label-task-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyLabelTaskStatus;"
        "const status=el.querySelector("
        "'[data-label-task-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/label-tasks/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update labeling task status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-label-task]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/label-tasks/'+"
        "encodeURIComponent(btn.dataset.deleteMlLabelTask)+'/delete',"
        "'POST');await load();}"
        "catch(x){showSystemError('Delete labeling task failed: '+"
        "x.message);}});}}"
        // Data Preparation (docs/PLAN.md "Machine Learning Abilities"
        // section 15): same status-dropdown-plus-Delete pattern as Data
        // Labeling above.
        "const PREP_JOB_STATUSES=['pending','running','completed','failed'];"
        "function renderMlPrepJobs(jobs){const el=q('#mlPrepJobsList');"
        "if(!el)return;"
        "if(!jobs.length){el.innerHTML='<p>No preparation jobs created "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Dataset','Operation','Status',"
        "'Set status'],"
        "jobs.map(x=>[esc(x.name),escTrim(x.datasetId,16),esc(x.operation),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-prep-job-status-for=\"'+x.id+'\">'+"
        "PREP_JOB_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-prep-job-status',x.id),"
        "deleteBtn('delete-ml-prep-job',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-prep-job-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyPrepJobStatus;"
        "const status=el.querySelector("
        "'[data-prep-job-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/prep-jobs/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update preparation job status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-prep-job]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/prep-jobs/'+"
        "encodeURIComponent(btn.dataset.deleteMlPrepJob)+'/delete',"
        "'POST');await load();}"
        "catch(x){showSystemError('Delete preparation job failed: '+"
        "x.message);}});}}"
        // Training Jobs (docs/PLAN.md "Machine Learning Abilities" section
        // 16): same status-dropdown-plus-Delete pattern as Data Labeling/
        // Data Preparation above, with the status list matching section
        // 16's eleven lifecycle states.
        "const TRAINING_JOB_STATUSES=['draft','queued','preparing','running',"
        "'paused','canceling','canceled','failed','completed',"
        "'awaiting_evaluation','archived'];"
        // Phase 56: one shared formatter for real evaluation metrics so
        // Training Jobs, Evaluation Lab, and prediction results all report
        // numbers the same way.
        "function fmtMlMetrics(m){if(!m)return'';"
        "if(m.task==='classification'){return'accuracy '+"
        "(100*m.accuracy).toFixed(1)+'%, macro precision '+"
        "Number(m.macroPrecision).toFixed(3)+', macro recall '+"
        "Number(m.macroRecall).toFixed(3)+', macro F1 '+"
        "Number(m.macroF1).toFixed(3)+' over '+m.evaluatedRows+' rows.';}"
        "return'MSE '+Number(m.mse).toPrecision(4)+', MAE '+"
        "Number(m.mae).toPrecision(4)+', R\\u00b2 '+"
        "Number(m.rSquared).toFixed(3)+' over '+m.evaluatedRows+' rows.';}"
        "function renderMlTrainingJobs(jobs){const el=q('#mlTrainingJobsList');"
        "if(!el)return;"
        "if(!jobs.length){el.innerHTML='<p>No training jobs created "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Project','Model','Dataset','Training "
        "type','Status','Set status'],"
        "jobs.map(x=>[esc(x.name),escTrim(x.projectId,16),"
        "escTrim(x.modelId,16),escTrim(x.datasetId,16),esc(x.trainingType),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-training-job-status-for=\"'+x.id+'\">'+"
        "TRAINING_JOB_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-training-job-status',x.id),"
        "runBtn('run-ml-training-job',x.id,'Train now'),"
        "deleteBtn('delete-ml-training-job',x.id))]));"
        // Phase 56: "Train now" invokes the real training executor and
        // shows the genuine result (method, loss, held-out metrics) in the
        // panel below the table.
        "for(const btn of el.querySelectorAll('[data-run-ml-training-job]')){"
        "btn.addEventListener('click',async()=>{"
        "const out=q('#mlTrainingRunResult');"
        "if(out)out.textContent='Training...';btn.disabled=true;"
        "try{const r=await api('/api/v1/ml/training-jobs/'+"
        "encodeURIComponent(btn.dataset.runMlTrainingJob)+'/run','POST',{});"
        "if(out)out.textContent='Trained '+r.method+' over '+r.epochs+"
        "' epochs on '+r.trainRows+' rows (final loss '+"
        "Number(r.finalLoss).toPrecision(4)+'). '+"
        "(r.evaluatedOnTest?'Held-out ('+r.testRows+' rows): '"
        ":'No held-out rows; metrics use training data: ')+"
        "fmtMlMetrics(r.metrics)+' Model ID: '+r.modelId;"
        "await load();}"
        "catch(x){if(out)out.textContent='';"
        "showSystemError('Training failed: '+x.message);}"
        "finally{btn.disabled=false;}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-training-job-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyTrainingJobStatus;"
        "const status=el.querySelector("
        "'[data-training-job-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/training-jobs/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update training job status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-training-job]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/training-jobs/'+"
        "encodeURIComponent(btn.dataset.deleteMlTrainingJob)+'/delete',"
        "'POST');await load();}"
        "catch(x){showSystemError('Delete training job failed: '+"
        "x.message);}});}}"
        // Evaluation Lab (docs/PLAN.md "Machine Learning Abilities" section
        // 23): same status-dropdown-plus-Delete pattern as Training Jobs
        // above.
        "const EVALUATION_RUN_STATUSES=['queued','running','completed',"
        "'failed','canceled'];"
        "function renderMlEvaluationRuns(runs){const el=q('#mlEvaluationRunsList');"
        "if(!el)return;"
        "if(!runs.length){el.innerHTML='<p>No evaluation runs created "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Model','Dataset','Category','Status',"
        "'Set status'],"
        "runs.map(x=>[esc(x.name),escTrim(x.modelId,16),"
        "escTrim(x.datasetId,16),esc(x.category),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-evaluation-run-status-for=\"'+x.id+'\">'+"
        "EVALUATION_RUN_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-evaluation-run-status',x.id),"
        "runBtn('run-ml-evaluation',x.id,'Evaluate now'),"
        "viewBtn('view-ml-evaluation',x.id,'View result'),"
        "deleteBtn('delete-ml-evaluation-run',x.id))]));"
        // Phase 56: "Evaluate now" scores the run's trained model against
        // its dataset for real; "View result" recalls the stored metrics.
        "for(const btn of el.querySelectorAll('[data-run-ml-evaluation]')){"
        "btn.addEventListener('click',async()=>{"
        "const out=q('#mlEvaluationRunResult');"
        "if(out)out.textContent='Evaluating...';btn.disabled=true;"
        "try{const r=await api('/api/v1/ml/evaluation-runs/'+"
        "encodeURIComponent(btn.dataset.runMlEvaluation)+'/run','POST',{});"
        "if(out)out.textContent='Evaluation complete: '+fmtMlMetrics(r.metrics);"
        "await load();}"
        "catch(x){if(out)out.textContent='';"
        "showSystemError('Evaluation failed: '+x.message);}"
        "finally{btn.disabled=false;}});}"
        "for(const btn of el.querySelectorAll('[data-view-ml-evaluation]')){"
        "btn.addEventListener('click',async()=>{"
        "const out=q('#mlEvaluationRunResult');"
        "try{const r=await api('/api/v1/ml/evaluation-runs/'+"
        "encodeURIComponent(btn.dataset.viewMlEvaluation)+'/result');"
        "if(out)out.textContent='Stored result: '+fmtMlMetrics(r.metrics);}"
        "catch(x){showSystemError('No stored result for this run yet: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-evaluation-run-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyEvaluationRunStatus;"
        "const status=el.querySelector("
        "'[data-evaluation-run-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/evaluation-runs/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update evaluation run status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-evaluation-run]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/evaluation-runs/'+"
        "encodeURIComponent(btn.dataset.deleteMlEvaluationRun)+'/delete',"
        "'POST');await load();}"
        "catch(x){showSystemError('Delete evaluation run failed: '+"
        "x.message);}});}}"
        // Experiment Tracking (docs/PLAN.md "Machine Learning Abilities"
        // section 25): same status-dropdown-plus-Delete pattern as
        // Evaluation Lab above.
        "const EXPERIMENT_STATUSES=['queued','running','completed',"
        "'failed','canceled'];"
        "function renderMlExperiments(experiments){const el=q('#mlExperimentsList');"
        "if(!el)return;"
        "if(!experiments.length){el.innerHTML='<p>No experiments created "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Project','Model','Dataset','Status',"
        "'Set status'],"
        "experiments.map(x=>[esc(x.name),escTrim(x.projectId,16),"
        "escTrim(x.modelId,16),escTrim(x.datasetId,16),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-experiment-status-for=\"'+x.id+'\">'+"
        "EXPERIMENT_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-experiment-status',x.id),"
        "deleteBtn('delete-ml-experiment',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-experiment-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyExperimentStatus;"
        "const status=el.querySelector("
        "'[data-experiment-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/experiments/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update experiment status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-experiment]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/experiments/'+"
        "encodeURIComponent(btn.dataset.deleteMlExperiment)+'/delete',"
        "'POST');await load();}"
        "catch(x){showSystemError('Delete experiment failed: '+"
        "x.message);}});}}"
        // Fine-Tuning Interface (docs/PLAN.md "Machine Learning Abilities"
        // section 18): same status-dropdown-plus-Delete pattern as Training
        // Jobs above, reusing that section's eleven-state lifecycle since
        // fine-tuning is a training-job variant. Phase 70 adds "Fine-tune
        // now", the real executor: it warm-starts gradient descent from the
        // base model's already-learned weights (see execute_fine_tuning_job
        // in server.cpp) rather than training a fresh model from scratch.
        "const FINE_TUNING_JOB_STATUSES=['draft','queued','preparing',"
        "'running','paused','canceling','canceled','failed','completed',"
        "'awaiting_evaluation','archived'];"
        "function renderMlFineTuningJobs(jobs){"
        "const el=q('#mlFineTuningJobsList');if(!el)return;"
        "if(!jobs.length){el.innerHTML='<p>No fine-tuning jobs created "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Project','Base model','Dataset',"
        "'Method','Status','Set status'],"
        "jobs.map(x=>[esc(x.name),escTrim(x.projectId,16),"
        "escTrim(x.modelId,16),escTrim(x.datasetId,16),esc(x.method),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-fine-tuning-job-status-for=\"'+x.id+'\">'+"
        "FINE_TUNING_JOB_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-fine-tuning-job-status',x.id),"
        "runBtn('run-ml-fine-tuning-job',x.id,'Fine-tune now'),"
        "deleteBtn('delete-ml-fine-tuning-job',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-run-ml-fine-tuning-job]')){"
        "btn.addEventListener('click',async()=>{"
        "const out=q('#mlFineTuningRunResult');"
        "if(out)out.textContent='Fine-tuning...';btn.disabled=true;"
        "try{const r=await api('/api/v1/ml/fine-tuning-jobs/'+"
        "encodeURIComponent(btn.dataset.runMlFineTuningJob)+'/run','POST',{});"
        "if(out)out.textContent='Fine-tuned '+r.method+' over '+r.epochs+"
        "' epochs on '+r.trainRows+' rows (final loss '+"
        "Number(r.finalLoss).toPrecision(4)+'). '+"
        "(r.evaluatedOnTest?'Held-out ('+r.testRows+' rows): '"
        ":'No held-out rows; metrics use training data: ')+"
        "fmtMlMetrics(r.metrics)+' Adapted model ID: '+r.modelId;"
        "await load();}"
        "catch(x){if(out)out.textContent='';"
        "showSystemError('Fine-tuning failed: '+x.message);}"
        "finally{btn.disabled=false;}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-fine-tuning-job-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyFineTuningJobStatus;"
        "const status=el.querySelector("
        "'[data-fine-tuning-job-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/fine-tuning-jobs/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update fine-tuning job status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-fine-tuning-job]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/fine-tuning-jobs/'+"
        "encodeURIComponent(btn.dataset.deleteMlFineTuningJob)+'/delete',"
        "'POST');await load();}"
        "catch(x){showSystemError('Delete fine-tuning job failed: '+"
        "x.message);}});}}"
        // Model Builder Interface (docs/PLAN.md "Machine Learning
        // Abilities" section 9): same status-dropdown-plus-Delete pattern
        // as Fine-Tuning above, with its own five-state design-time
        // lifecycle (draft/configuring/ready/submitted/archived) instead of
        // the eleven-state job lifecycle, since a builder configuration
        // never runs. Each row's Configure button loads that
        // configuration's full section 9 build settings into the
        // basic/advanced configure form (fillMlModelBuilderConfigureForm),
        // which saves through POST .../{id}/configure.
        "const MODEL_BUILDER_CONFIG_STATUSES=['draft','configuring','ready',"
        "'submitted','archived'];"
        "let mlModelBuilderConfigureTarget=null;"
        "function fillMlModelBuilderConfigureForm(x){"
        "mlModelBuilderConfigureTarget=x.id;"
        "q('#mlMbcTarget').textContent='Editing build settings for: '+x.name;"
        "const s=x.settings||{};"
        "q('#mlMbcMode').value=s.configurationMode||'basic';"
        "q('#mlMbcArchitecture').value=s.architecture||'';"
        "q('#mlMbcLossFunction').value=s.lossFunction||'';"
        "q('#mlMbcOptimiser').value=s.optimiser||'';"
        "q('#mlMbcBatchSize').value=s.batchSize||'';"
        "q('#mlMbcEpochCount').value=s.epochCount||'';"
        "q('#mlMbcSequenceLength').value=s.sequenceLength||'';"
        "q('#mlMbcLayerConfiguration').value=s.layerConfiguration||'';"
        "q('#mlMbcHiddenDimensions').value=s.hiddenDimensions||'';"
        "q('#mlMbcAttentionConfiguration').value="
        "s.attentionConfiguration||'';"
        "q('#mlMbcVocabularyTokenizer').value=s.vocabularyTokenizer||'';"
        "q('#mlMbcActivationFunctions').value=s.activationFunctions||'';"
        "q('#mlMbcDropout').value=s.dropout||'';"
        "q('#mlMbcInitialisationStrategy').value="
        "s.initialisationStrategy||'';"
        "q('#mlMbcLearningRateScheduler').value="
        "s.learningRateScheduler||'';"
        "q('#mlMbcGradientAccumulation').value=s.gradientAccumulation||'';"
        "q('#mlMbcGradientClipping').value=s.gradientClipping||'';"
        "q('#mlMbcMixedPrecision').checked=!!s.mixedPrecision;"
        "q('#mlMbcCheckpointFrequency').value=s.checkpointFrequency||'';"
        "q('#mlMbcValidationFrequency').value=s.validationFrequency||'';"
        "q('#mlMbcEarlyStopping').checked=!!s.earlyStopping;"
        "q('#mlMbcRandomSeed').value=s.randomSeed||'';"
        "q('#mlMbcReproducibilitySettings').value="
        "s.reproducibilitySettings||'';"
        "q('#mlMbcDistributedTrainingSettings').value="
        "s.distributedTrainingSettings||'';"
        "q('#mlMbcAdvanced').style.display="
        "q('#mlMbcMode').value==='advanced'?'':'none';}"
        "function renderMlModelBuilderConfigs(configs){"
        "const el=q('#mlModelBuilderConfigsList');if(!el)return;"
        "if(!configs.length){el.innerHTML='<p>No model builder "
        "configurations created yet.</p>';return;}"
        "el.innerHTML=table(['Name','Project','Base model','Source type',"
        "'Architecture','Mode','Status','Set status'],"
        "configs.map(x=>[esc(x.name),escTrim(x.projectId,16),"
        "escTrim(x.baseModelId,16),esc(x.sourceType),"
        "esc((x.settings&&x.settings.architecture)||''),"
        "esc((x.settings&&x.settings.configurationMode)||'basic'),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-model-builder-config-status-for=\"'+x.id+"
        "'\">'+MODEL_BUILDER_CONFIG_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-model-builder-config-status',x.id),"
        "configureBtn('configure-ml-model-builder-config',x.id),"
        "deleteBtn('delete-ml-model-builder-config',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-configure-ml-model-builder-config]')){"
        "btn.addEventListener('click',()=>{const found=configs.find(c=>"
        "c.id===btn.dataset.configureMlModelBuilderConfig);"
        "if(found)fillMlModelBuilderConfigureForm(found);});}"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-model-builder-config-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyModelBuilderConfigStatus;"
        "const status=el.querySelector("
        "'[data-model-builder-config-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/model-builder-configs/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update model builder configuration "
        "status failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-model-builder-config]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/model-builder-configs/'+"
        "encodeURIComponent(btn.dataset.deleteMlModelBuilderConfig)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete model builder configuration "
        "failed: '+x.message);}});}}"
        // Prompt and Instruction Training (docs/PLAN.md "Machine Learning
        // Abilities" section 19): same status-dropdown-plus-Delete pattern
        // as Model Builder above, with its own five-state reviewer workflow
        // (draft/in_review/approved/rejected/archived) matching section 19's
        // "generated training examples must require approval before
        // entering an approved dataset."
        "const INSTRUCTION_EXAMPLE_STATUSES=['draft','in_review','approved',"
        "'rejected','archived'];"
        "function renderMlInstructionExamples(examples){"
        "const el=q('#mlInstructionExamplesList');if(!el)return;"
        "if(!examples.length){el.innerHTML='<p>No instruction examples "
        "created yet.</p>';return;}"
        "el.innerHTML=table(['Name','Dataset','Subject classification',"
        "'Status','Set status'],"
        "examples.map(x=>[esc(x.name),escTrim(x.datasetId,16),"
        "esc(x.subjectClassification),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-instruction-example-status-for=\"'+x.id+"
        "'\">'+INSTRUCTION_EXAMPLE_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-instruction-example-status',x.id),"
        "deleteBtn('delete-ml-instruction-example',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-instruction-example-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyInstructionExampleStatus;"
        "const status=el.querySelector("
        "'[data-instruction-example-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/instruction-examples/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update instruction example status "
        "failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-instruction-example]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/instruction-examples/'+"
        "encodeURIComponent(btn.dataset.deleteMlInstructionExample)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete instruction example failed: '+"
        "x.message);}});}}"
        // Synthetic Data Generation (docs/PLAN.md "Machine Learning
        // Abilities" section 20): same status-dropdown-plus-Delete pattern
        // as Prompt and Instruction Training above, with its own five-state
        // reviewer workflow (draft/in_review/approved/rejected/archived)
        // matching section 20's requirement that generated records "remain
        // distinguishable from human-created and real-world data" until
        // reviewed.
        "const SYNTHETIC_RECORD_STATUSES=['draft','in_review','approved',"
        "'rejected','archived'];"
        "function renderMlSyntheticRecords(records){"
        "const el=q('#mlSyntheticRecordsList');if(!el)return;"
        "if(!records.length){el.innerHTML='<p>No synthetic records "
        "created yet.</p>';return;}"
        "el.innerHTML=table(['Name','Dataset','Generation technique',"
        "'Status','Set status'],"
        "records.map(x=>[esc(x.name),escTrim(x.datasetId,16),"
        "esc(x.generationTechnique),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-synthetic-record-status-for=\"'+x.id+"
        "'\">'+SYNTHETIC_RECORD_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-synthetic-record-status',x.id),"
        "deleteBtn('delete-ml-synthetic-record',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-synthetic-record-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applySyntheticRecordStatus;"
        "const status=el.querySelector("
        "'[data-synthetic-record-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/synthetic-records/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update synthetic record status "
        "failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-synthetic-record]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/synthetic-records/'+"
        "encodeURIComponent(btn.dataset.deleteMlSyntheticRecord)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete synthetic record failed: '+"
        "x.message);}});}}"
        // Embeddings and Vector Stores (docs/PLAN.md "Machine Learning
        // Abilities" section 21): a vector store is a standalone registered
        // resource, not a target-scoped content record, so it reuses
        // Dataset's three-state pending/approved/rejected approval workflow
        // instead of the five-state reviewer workflow content records use.
        "const VECTOR_STORE_STATUSES=['pending','approved','rejected'];"
        "function renderMlVectorStores(stores){"
        "const el=q('#mlVectorStoresList');if(!el)return;"
        "if(!stores.length){el.innerHTML='<p>No vector stores registered "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Embedding model','Distance metric',"
        "'Status','Set status'],"
        "stores.map(x=>[esc(x.name),esc(x.embeddingModel),"
        "esc(x.distanceMetric),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-vector-store-status-for=\"'+x.id+'\">'+"
        "VECTOR_STORE_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-vector-store-status',x.id),"
        "viewBtn('view-ml-vector-index',x.id,'View index'),"
        "deleteBtn('delete-ml-vector-store',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-vector-store-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyVectorStoreStatus;"
        "const status=el.querySelector("
        "'[data-vector-store-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/vector-stores/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update vector store status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-view-ml-vector-index]')){"
        "btn.addEventListener('click',async()=>{try{const r=await api("
        "'/api/v1/ml/vector-stores/'+encodeURIComponent("
        "btn.dataset.viewMlVectorIndex)+'/index');const out=q('#mlVectorIndexResult');"
        "if(out)out.textContent=r.documentCount+' document(s), '+r.chunkCount+"
        "' chunk(s), '+r.dimensions+' dimensions, '+r.indexedTextBytes+"
        "' indexed text bytes ('+r.embeddingMethod+').';}catch(x){"
        "showSystemError('Read vector index failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-vector-store]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/vector-stores/'+"
        "encodeURIComponent(btn.dataset.deleteMlVectorStore)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete vector store failed: '+"
        "x.message);}});}}"
        // Retrieval-Augmented Generation (docs/PLAN.md "Machine Learning
        // Abilities" section 22): a RAG configuration is a standalone
        // registered resource, not a target-scoped content record, so it
        // reuses the same three-state pending/approved/rejected approval
        // workflow as Vector Stores above.
        "const RAG_CONFIG_STATUSES=['pending','approved','rejected'];"
        "function renderMlRagConfigs(configs){"
        "const el=q('#mlRagConfigsList');if(!el)return;"
        "fillMlSelect('#mlRagQueryConfigId',configs,'Choose an approved RAG configuration',"
        "x=>x.name+' ('+x.status+')');"
        "if(!configs.length){el.innerHTML='<p>No RAG configurations "
        "registered yet.</p>';return;}"
        "el.innerHTML=table(['Name','Search strategy','Vector store',"
        "'Status','Set status'],"
        "configs.map(x=>[esc(x.name),esc(x.searchStrategy),"
        "escTrim(x.vectorStoreId,16),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-rag-config-status-for=\"'+x.id+'\">'+"
        "RAG_CONFIG_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-rag-config-status',x.id),"
        "deleteBtn('delete-ml-rag-config',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-rag-config-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyRagConfigStatus;"
        "const status=el.querySelector("
        "'[data-rag-config-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/rag-configs/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update RAG config status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-rag-config]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/rag-configs/'+"
        "encodeURIComponent(btn.dataset.deleteMlRagConfig)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete RAG config failed: '+"
        "x.message);}});}}"
        // Subject Examination System (docs/PLAN.md "Machine Learning
        // Abilities" section 24): an exam is authored content that a
        // reviewer approves before it may examine anything, so it uses the
        // same five-state reviewer workflow as Instruction Examples.
        "const SUBJECT_EXAM_STATUSES=['draft','in_review','approved',"
        "'rejected','archived'];"
        "function renderMlSubjectExams(exams){"
        "const el=q('#mlSubjectExamsList');if(!el)return;"
        "if(!exams.length){el.innerHTML='<p>No subject exams created "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Subject ID','Question format',"
        "'Status','Set status'],"
        "exams.map(x=>[esc(x.name),escTrim(x.subjectId,16),"
        "esc(x.questionFormat),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-subject-exam-status-for=\"'+x.id+'\">'+"
        "SUBJECT_EXAM_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-subject-exam-status',x.id),"
        "deleteBtn('delete-ml-subject-exam',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-subject-exam-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applySubjectExamStatus;"
        "const status=el.querySelector("
        "'[data-subject-exam-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/subject-exams/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update subject exam status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-subject-exam]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/subject-exams/'+"
        "encodeURIComponent(btn.dataset.deleteMlSubjectExam)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete subject exam failed: '+"
        "x.message);}});}}"
        // Hyperparameter Optimization (docs/PLAN.md "Machine Learning
        // Abilities" section 26): a search queues, runs, and pauses like a
        // training job, so it uses the same eleven-state job lifecycle.
        "const HYPERPARAMETER_SEARCH_STATUSES=['draft','queued','preparing',"
        "'running','paused','canceling','canceled','failed','completed',"
        "'awaiting_evaluation','archived'];"
        "function renderMlHyperparameterSearches(searches){"
        "const el=q('#mlHyperparameterSearchesList');if(!el)return;"
        "if(!searches.length){el.innerHTML='<p>No hyperparameter searches "
        "created yet.</p>';return;}"
        "el.innerHTML=table(['Name','Training job ID','Strategy',"
        "'Status','Set status'],"
        "searches.map(x=>[esc(x.name),escTrim(x.trainingJobId,16),"
        "esc(x.strategy),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-hyperparameter-search-status-for=\"'+x.id+"
        "'\">'+HYPERPARAMETER_SEARCH_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-hyperparameter-search-status',x.id),"
        "deleteBtn('delete-ml-hyperparameter-search',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-hyperparameter-search-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyHyperparameterSearchStatus;"
        "const status=el.querySelector("
        "'[data-hyperparameter-search-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/hyperparameter-searches/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError("
        "'Update hyperparameter search status failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-hyperparameter-search]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/hyperparameter-searches/'+"
        "encodeURIComponent(btn.dataset.deleteMlHyperparameterSearch)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete hyperparameter search failed: '+"
        "x.message);}});}}"
        // Model Optimization (docs/PLAN.md "Machine Learning Abilities"
        // section 28): an optimization run executes like a training job, so
        // it uses the same eleven-state job lifecycle.
        "const MODEL_OPTIMIZATION_STATUSES=['draft','queued','preparing',"
        "'running','paused','canceling','canceled','failed','completed',"
        "'awaiting_evaluation','archived'];"
        "function renderMlModelOptimizations(runs){"
        "const el=q('#mlModelOptimizationsList');if(!el)return;"
        "if(!runs.length){el.innerHTML='<p>No model optimization runs "
        "created yet.</p>';return;}"
        "el.innerHTML=table(['Name','Model ID','Operation',"
        "'Status','Set status'],"
        "runs.map(x=>[esc(x.name),escTrim(x.modelId,16),esc(x.operation),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-model-optimization-status-for=\"'+x.id+"
        "'\">'+MODEL_OPTIMIZATION_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-model-optimization-status',x.id),"
        "deleteBtn('delete-ml-model-optimization',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-model-optimization-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyModelOptimizationStatus;"
        "const status=el.querySelector("
        "'[data-model-optimization-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/model-optimizations/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError("
        "'Update model optimization status failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-model-optimization]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/model-optimizations/'+"
        "encodeURIComponent(btn.dataset.deleteMlModelOptimization)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete model optimization failed: '+"
        "x.message);}});}}"
        // Checkpoint Management (docs/PLAN.md "Machine Learning Abilities"
        // section 33): a checkpoint carries a retention lifecycle (active/
        // pinned/archived), not an approval workflow -- pinned is section
        // 33's own "protect" operation.
        "const CHECKPOINT_STATUSES=['active','pinned','archived'];"
        "function renderMlCheckpoints(checkpoints){"
        "const el=q('#mlCheckpointsList');if(!el)return;"
        "if(!checkpoints.length){el.innerHTML='<p>No checkpoints recorded "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Training job ID','Capture reason',"
        "'Status','Set status'],"
        "checkpoints.map(x=>[esc(x.name),escTrim(x.trainingJobId,16),"
        "esc(x.captureReason),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-checkpoint-status-for=\"'+x.id+'\">'+"
        "CHECKPOINT_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-checkpoint-status',x.id),"
        "deleteBtn('delete-ml-checkpoint',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-checkpoint-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyCheckpointStatus;"
        "const status=el.querySelector("
        "'[data-checkpoint-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/checkpoints/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update checkpoint status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-checkpoint]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/checkpoints/'+"
        "encodeURIComponent(btn.dataset.deleteMlCheckpoint)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete checkpoint failed: '+"
        "x.message);}});}}"
        // Deployment Manager (docs/PLAN.md "Machine Learning Abilities"
        // section 34): a deployment is a standalone registered resource
        // awaiting authorization, so it reuses the same three-state
        // pending/approved/rejected approval workflow as Vector Stores.
        "const DEPLOYMENT_STATUSES=['pending','approved','rejected'];"
        "function renderMlDeployments(deployments){"
        "const el=q('#mlDeploymentsList');if(!el)return;"
        "if(!deployments.length){el.innerHTML='<p>No deployments recorded "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Model ID','Environment','Strategy',"
        "'Status','Set status'],"
        "deployments.map(x=>[esc(x.name),escTrim(x.modelId,16),"
        "esc(x.environment),esc(x.strategy),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-deployment-status-for=\"'+x.id+'\">'+"
        "DEPLOYMENT_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-deployment-status',x.id),"
        "deleteBtn('delete-ml-deployment',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-deployment-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyDeploymentStatus;"
        "const status=el.querySelector("
        "'[data-deployment-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/deployments/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update deployment status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-deployment]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/deployments/'+"
        "encodeURIComponent(btn.dataset.deleteMlDeployment)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete deployment failed: '+"
        "x.message);}});}}"
        // Model Comparison (docs/PLAN.md "Machine Learning Abilities"
        // section 27): the same status-dropdown pattern as Evaluation Lab,
        // plus Phase 57's real "Compare now"/"View result" actions.
        "const MODEL_COMPARISON_STATUSES=['queued','running','completed',"
        "'failed','canceled'];"
        // One shared formatter for a stored comparison result so the run
        // and view actions describe the winner identically.
        "function fmtMlComparison(r){if(!r)return'';"
        "const verdict=r.winner==='tie'?'Tie on '+r.primaryMetric:"
        "(r.winner==='candidate'?'Candidate':'Baseline')+' wins on '+"
        "r.primaryMetric;"
        "return verdict+' (baseline '+Number(r.baselineValue).toPrecision(4)+"
        "', candidate '+Number(r.candidateValue).toPrecision(4)+"
        "', delta '+Number(r.delta).toPrecision(4)+'). '+"
        "'Baseline '+r.baseline.modelId+': '+fmtMlMetrics(r.baseline.metrics)+"
        "' Candidate '+r.candidate.modelId+': '+"
        "fmtMlMetrics(r.candidate.metrics);}"
        "function renderMlModelComparisons(comparisons){"
        "const el=q('#mlModelComparisonsList');if(!el)return;"
        "if(!comparisons.length){el.innerHTML='<p>No model comparisons "
        "created yet.</p>';return;}"
        "el.innerHTML=table(['Name','Baseline model','Candidate model',"
        "'Benchmark dataset','Status','Set status'],"
        "comparisons.map(x=>[esc(x.name),escTrim(x.baselineModelId,16),"
        "escTrim(x.candidateModelId,16),escTrim(x.datasetId,16),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-comparison-status-for=\"'+x.id+'\">'+"
        "MODEL_COMPARISON_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-comparison-status',x.id),"
        "runBtn('run-ml-comparison',x.id,'Compare now'),"
        "viewBtn('view-ml-comparison',x.id,'View result'),"
        "deleteBtn('delete-ml-comparison',x.id))]));"
        // Phase 57: "Compare now" scores both trained artifacts against the
        // shared benchmark for real; "View result" recalls the stored
        // verdict.
        "for(const btn of el.querySelectorAll('[data-run-ml-comparison]')){"
        "btn.addEventListener('click',async()=>{"
        "const out=q('#mlModelComparisonResult');"
        "if(out)out.textContent='Comparing...';btn.disabled=true;"
        "try{const r=await api('/api/v1/ml/model-comparisons/'+"
        "encodeURIComponent(btn.dataset.runMlComparison)+'/run','POST',{});"
        "if(out)out.textContent='Comparison complete: '+"
        "fmtMlComparison(r.result);await load();}"
        "catch(x){if(out)out.textContent='';"
        "showSystemError('Comparison failed: '+x.message);}"
        "finally{btn.disabled=false;}});}"
        "for(const btn of el.querySelectorAll('[data-view-ml-comparison]')){"
        "btn.addEventListener('click',async()=>{"
        "const out=q('#mlModelComparisonResult');"
        "try{const r=await api('/api/v1/ml/model-comparisons/'+"
        "encodeURIComponent(btn.dataset.viewMlComparison)+'/result');"
        "if(out)out.textContent='Stored result: '+fmtMlComparison(r.result);}"
        "catch(x){showSystemError('No stored result for this comparison "
        "yet: '+x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-comparison-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyComparisonStatus;"
        "const status=el.querySelector("
        "'[data-comparison-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/model-comparisons/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update comparison status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-comparison]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/model-comparisons/'+"
        "encodeURIComponent(btn.dataset.deleteMlComparison)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete comparison failed: '+"
        "x.message);}});}}"
        // Phase 62 (docs/PLAN.md "Machine Learning Abilities" section 35):
        // same status-dropdown-plus-Delete pattern as every registry above.
        "const INFERENCE_ENDPOINT_STATUSES=['draft','active','disabled'];"
        "function renderMlInferenceEndpoints(endpoints){"
        "const el=q('#mlInferenceEndpointsList');if(!el)return;"
        "if(!endpoints.length){el.innerHTML='<p>No inference endpoints "
        "created yet.</p>';return;}"
        "el.innerHTML=table(['Name','Model','Host','Port','Protocol',"
        "'Status','Set status'],"
        "endpoints.map(x=>[esc(x.name),escTrim(x.modelId,16),esc(x.host),"
        "esc(x.port),esc(x.protocol),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-endpoint-status-for=\"'+x.id+'\">'+"
        "INFERENCE_ENDPOINT_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-endpoint-status',x.id),"
        "deleteBtn('delete-ml-endpoint',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-apply-endpoint-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyEndpointStatus;"
        "const status=el.querySelector("
        "'[data-endpoint-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/inference-endpoints/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update endpoint status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-endpoint]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/inference-endpoints/'+"
        "encodeURIComponent(btn.dataset.deleteMlEndpoint)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete endpoint failed: '+"
        "x.message);}});}}"
        // Phase 63 (docs/PLAN.md "Machine Learning Abilities" section 30):
        // same list/status/delete pattern as the other registries. Phase 67
        // adds a real "View live telemetry" action for whichever node is
        // flagged local -- see ComputeNode's class comment in masterai.hpp.
        "const COMPUTE_NODE_STATUSES=['available','reserved','draining',"
        "'disabled'];"
        "function fmtMlHardware(h){if(!h)return'';"
        "return h.physicalCpus+' physical / '+h.logicalCpus+' logical CPUs, '+"
        "h.availableRamMiB+' / '+h.totalRamMiB+' MiB RAM available, '+"
        "(h.gpuBackends.length?h.gpuBackends.join('/')+' GPU backend, '+"
        "h.gpuMemoryMiB+' MiB VRAM':'no GPU backend detected')+', '+"
        "h.storageFreeMiB+' MiB free disk ('+h.storageClass+').';}"
        "function renderMlComputeNodes(nodes){"
        "const el=q('#mlComputeNodesList');if(!el)return;"
        "if(!nodes.length){el.innerHTML='<p>No compute nodes registered "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Address','OS','CPU','GPU','Local',"
        "'Status','Set status'],"
        "nodes.map(x=>[esc(x.name),esc(x.address),esc(x.operatingSystem),"
        "escTrim(x.cpuDescription,20),escTrim(x.gpuDescription,20),"
        "x.isLocal?'Yes':'No',"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-node-status-for=\"'+x.id+'\">'+"
        "COMPUTE_NODE_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-node-status',x.id),"
        "x.isLocal?viewBtn('view-ml-node-telemetry',x.id,"
        "'View live telemetry'):'',"
        "deleteBtn('delete-ml-node',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-apply-node-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyNodeStatus;"
        "const status=el.querySelector("
        "'[data-node-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/compute-nodes/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update compute node status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-view-ml-node-telemetry]')"
        "){btn.addEventListener('click',async()=>{"
        "const out=q('#mlComputeNodeTelemetry');"
        "if(out)out.textContent='Probing...';"
        "try{const r=await api('/api/v1/ml/compute-nodes/'+"
        "encodeURIComponent(btn.dataset.viewMlNodeTelemetry)+'/telemetry');"
        "if(out)out.textContent='Live telemetry: '+"
        "fmtMlHardware(r.telemetry);}"
        "catch(x){if(out)out.textContent='';"
        "showSystemError('Fetch telemetry failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-node]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/compute-nodes/'+"
        "encodeURIComponent(btn.dataset.deleteMlNode)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete compute node failed: '+"
        "x.message);}});}}"
        // Phase 64/69/71 (docs/PLAN.md "Machine Learning Abilities" section
        // 37): 'Run' genuinely executes Train model/Evaluate model/Validate
        // data/Validate model/Safety tests/Request approval/Deploy staging/
        // Deploy production/Rollback/Monitor stages and honestly records
        // every other named stage as skipped -- see AutomationPipeline's
        // class comment in masterai.hpp. The run's per-stage outcome is
        // rendered into #mlPipelineRunDetail below. Phase 71: a run now
        // executes on a background thread and POST .../run returns
        // immediately in the `running` state, so renderMlPipelineRun()
        // shows a live <progress> bar (completedStageCount/totalStageCount)
        // and the currently-running stage's name while pollMlPipelineRun()
        // re-fetches GET .../runs once a second until the run reaches a
        // terminal status.
        "const AUTOMATION_PIPELINE_STATUSES=['draft','active','disabled'];"
        "function renderMlPipelineRun(run){"
        "const el=q('#mlPipelineRunDetail');if(!el)return;"
        "const stillRunning=run.status==='queued'||run.status==='running';"
        "const bar=stillRunning?'<progress value=\"'+run.completedStageCount+"
        "'\" max=\"'+(run.totalStageCount||1)+'\"></progress><p>'+"
        "run.completedStageCount+' / '+run.totalStageCount+' stage(s) complete'+"
        "(run.currentStage?' -- '+esc(run.currentStage):'')+'</p>':'';"
        "el.innerHTML='<h3>Run '+esc(run.id)+' -- '+"
        "'<span class=\"stateTag stateTag-'+esc(run.status)+'\">'+"
        "esc(run.status)+'</span></h3>'+bar+'<p>'+esc(run.outcomeNote)+'</p>'+"
        "table(['Stage','Status','Detail'],"
        "(run.stageResults||[]).map(s=>[esc(s.stage),"
        "'<span class=\"stateTag stateTag-'+esc(s.status)+'\">'+"
        "esc(s.status)+'</span>',esc(s.detail)]));}"
        // Re-fetches the run list (the run's own live progress fields, not
        // just its final result) once a second until the run this button
        // started reaches a terminal status, re-rendering the progress bar
        // and stage table on every tick so the operator can see how far
        // along a still-running pipeline is instead of staring at a
        // disabled button with no feedback.
        "const TERMINAL_PIPELINE_RUN_STATES=new Set("
        "['completed','failed','canceled']);"
        "async function pollMlPipelineRun(pipelineId,runId){"
        "for(;;){let run;"
        "try{const d=await api('/api/v1/ml/automation-pipelines/'+"
        "encodeURIComponent(pipelineId)+'/runs');"
        "run=(d.pipelineRuns||[]).find(r=>r.id===runId);}"
        "catch(x){console.warn('pipeline run poll tick failed, retrying "
        "next interval',x);}"
        "if(run)renderMlPipelineRun(run);"
        "if(run&&TERMINAL_PIPELINE_RUN_STATES.has(run.status))return run;"
        "await new Promise(r=>setTimeout(r,1000));}}"
        "function renderMlAutomationPipelines(pipelines){"
        "const el=q('#mlAutomationPipelinesList');if(!el)return;"
        "if(!pipelines.length){el.innerHTML='<p>No automation pipelines "
        "created yet.</p>';return;}"
        "el.innerHTML=table(['Name','Project','Stages','Dataset','Model',"
        "'Status','Set status'],"
        "pipelines.map(x=>[esc(x.name),escTrim(x.projectId,16),"
        "escTrim(x.stages,24),escTrim(x.datasetId,16),escTrim(x.modelId,16),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-pipeline-status-for=\"'+x.id+'\">'+"
        "AUTOMATION_PIPELINE_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-pipeline-status',x.id),"
        "runBtn('run-ml-pipeline',x.id,'Run pipeline'),"
        "deleteBtn('delete-ml-pipeline',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-run-ml-pipeline]')){"
        "btn.addEventListener('click',async()=>{btn.disabled=true;"
        "const pipelineId=btn.dataset.runMlPipeline;"
        "try{const run=await api('/api/v1/ml/automation-pipelines/'+"
        "encodeURIComponent(pipelineId)+'/run','POST',{});"
        "renderMlPipelineRun(run);"
        "await pollMlPipelineRun(pipelineId,run.id);await load();}"
        "catch(x){showSystemError('Run pipeline failed: '+x.message);}"
        "finally{btn.disabled=false;}});}"
        "for(const btn of el.querySelectorAll('[data-apply-pipeline-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyPipelineStatus;"
        "const status=el.querySelector("
        "'[data-pipeline-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/automation-pipelines/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update pipeline status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-pipeline]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/automation-pipelines/'+"
        "encodeURIComponent(btn.dataset.deleteMlPipeline)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete pipeline failed: '+"
        "x.message);}});}}"
        // Phase 65 (docs/PLAN.md "Machine Learning Abilities" section 40):
        // policies and model cards each get their own independent
        // pending/approved/rejected approval workflow.
        "const SAFETY_POLICY_STATUSES=['pending','approved','rejected'];"
        "function renderMlSafetyPolicies(policies){"
        "const el=q('#mlSafetyPoliciesList');if(!el)return;"
        "if(!policies.length){el.innerHTML='<p>No safety policies created "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Scope','Restricted categories',"
        "'Status','Set status'],"
        "policies.map(x=>[esc(x.name),esc(x.scope),"
        "escTrim(x.restrictedDataCategories,24),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-policy-status-for=\"'+x.id+'\">'+"
        "SAFETY_POLICY_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-policy-status',x.id),"
        "deleteBtn('delete-ml-policy',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-apply-policy-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyPolicyStatus;"
        "const status=el.querySelector("
        "'[data-policy-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/safety-policies/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update policy status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-policy]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/safety-policies/'+"
        "encodeURIComponent(btn.dataset.deleteMlPolicy)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete policy failed: '+"
        "x.message);}});}}"
        "function renderMlModelCards(cards){"
        "const el=q('#mlModelCardsList');if(!el)return;"
        "if(!cards.length){el.innerHTML='<p>No model cards created yet."
        "</p>';return;}"
        "el.innerHTML=table(['Model','Purpose','Status','Set status'],"
        "cards.map(x=>[escTrim(x.modelId,20),escTrim(x.purpose,28),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-card-status-for=\"'+x.id+'\">'+"
        "SAFETY_POLICY_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-card-status',x.id),"
        "deleteBtn('delete-ml-card',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-apply-card-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyCardStatus;"
        "const status=el.querySelector("
        "'[data-card-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/model-cards/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update model card status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-card]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/model-cards/'+"
        "encodeURIComponent(btn.dataset.deleteMlCard)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete model card failed: '+"
        "x.message);}});}}"
        // Phase 66 (docs/PLAN.md "Machine Learning Abilities" section 43):
        // read-only -- no row actions, no form.
        "function renderMlAuditLogs(entries){"
        "const el=q('#mlAuditLogsList');if(!el)return;"
        "if(!entries.length){el.innerHTML='<p>No Machine Learning "
        "administrator actions recorded yet.</p>';return;}"
        "el.innerHTML=table(['When','Event','Actor','Outcome','Detail'],"
        "entries.map(x=>[esc(new Date(x.timestampEpochSeconds*1000)"
        ".toLocaleString()),esc(x.event),esc(x.actor),esc(x.outcome),"
        "esc(x.detail)]));}"
        // Phase 68 (docs/PLAN.md "Machine Learning Abilities" section 44):
        // a read-only aggregation over data other real phases already
        // measured -- see the /api/v1/ml/monitoring handler's own comment
        // in server.cpp for exactly what is real here and what remains
        // planned (per-step training curves, live per-request telemetry,
        // temperature/network sensors).
        "function renderMlMonitoring(data){"
        "const panel=q('#mlMonitoringPanel');if(!panel)return;"
        "const sys=q('#mlMonitoringSystem');"
        "if(sys)sys.textContent=data.systemResources?"
        "fmtMlHardware(data.systemResources):"
        "'No system resource snapshot available.';"
        "const jobs=q('#mlMonitoringTrainingJobs');"
        "if(jobs){const counts=data.trainingJobCounts||{};"
        "const keys=Object.keys(counts);"
        "jobs.innerHTML=keys.length?table(['Training job status','Count'],"
        "keys.map(k=>[esc(k),String(counts[k])])):"
        "'<p>No training jobs recorded yet.</p>';}"
        "const evals=q('#mlMonitoringEvaluations');"
        "if(evals){const rows=data.evaluationMetrics||[];"
        "evals.innerHTML=rows.length?table("
        "['Evaluation run','Model','Dataset','Measured result'],"
        "rows.map(x=>[esc(x.name),escTrim(x.modelId,16),"
        "escTrim(x.datasetId,16),fmtMlMetrics(x.metrics)])):"
        "'<p>No completed evaluation runs with stored metrics yet.</p>';}"
        "const benches=q('#mlMonitoringBenchmarks');"
        "if(benches){const rows=data.inferenceBenchmarks||[];"
        "benches.innerHTML=rows.length?table("
        "['Model','Benchmark profile','Prompt tokens/sec',"
        "'Generation tokens/sec','Passed cases'],"
        "rows.map(x=>[escTrim(x.modelId,16),esc(x.profile),"
        "Number(x.promptTokensPerSecond).toFixed(1),"
        "Number(x.generationTokensPerSecond).toFixed(1),"
        "x.passedCases+' / '+x.totalCases])):"
        "'<p>No benchmark runs recorded yet -- run one from the "
        "Dashboard to populate real inference throughput evidence "
        "here.</p>';}}"
        // Each row gets its own Start/resume, Pause, Stop, and Remove buttons
        // wired directly to that job's id -- nothing to hand-type, unlike the
        // old single manual 'Download job ID' field this replaces. The State
        // cell carries its own id (state-<id>) so the progress poll below can
        // update it live instead of leaving it frozen at whatever it showed
        // when this row was last rendered.
        "function renderDownloads(downloads){const el=q('#downloadsList');if(!el)return;"
        "if(!downloads.length){el.innerHTML='<p>No downloads queued yet.</p>';return;}"
        "const ACTIVE=new Set(['queued','transferring','paused','verifying']);"
        "el.innerHTML=table(['Model','Filename','State','Progress',''],"
        "downloads.map(x=>[esc(x.modelId),esc(x.filename),"
        "'<span id=\"state-'+x.id+'\">'+esc(x.state)+'</span>',"
        "'<span id=\"progress-'+x.id+'\">'+"
        "(x.completedBytes/1048576).toFixed(1)+' MiB</span>',"
        "toolbar("
        "iconBtn(x.state==='complete'?'check':'play',"
        "x.state==='complete'?'Complete':'Start / resume','run-id',x.id,"
        "'',x.state==='complete'?'disabled':''),"
        "(x.state==='transferring'?"
        "iconBtn('pause','Pause','pause-id',x.id):''),"
        "(ACTIVE.has(x.state)?"
        "iconBtn('stop','Stop','stop-id',x.id,'iconBtn-delete'):''),"
        "iconBtn('trash','Remove','remove-id',x.id,'iconBtn-delete'))]));"
        "for(const btn of el.querySelectorAll('[data-run-id]')){"
        "btn.addEventListener('click',()=>runDownloadJob(btn.dataset.runId));}"
        "for(const btn of el.querySelectorAll('[data-pause-id]')){"
        "btn.addEventListener('click',()=>pauseDownload(btn.dataset.pauseId));}"
        "for(const btn of el.querySelectorAll('[data-stop-id]')){"
        "btn.addEventListener('click',()=>stopDownload(btn.dataset.stopId));}"
        "for(const btn of el.querySelectorAll('[data-remove-id]')){"
        "btn.addEventListener('click',()=>removeDownload(btn.dataset.removeId));}"
        // A job already 'transferring' (started earlier in this session or
        // by a page that's since been reloaded) has no JS-side poll loop of
        // its own yet -- without this, its progress and state cells only
        // ever show the single snapshot captured at this render and never
        // move again until something else happens to reload the page.
        // Resuming the poll here (without re-POSTing /run, which is already
        // in flight server-side) is what keeps it live.
        "for(const x of downloads){if(x.state==='transferring'&&!activePolls[x.id]){"
        "activePolls[x.id]=pollDownloadProgress(x.id);}}}"
        // Each section (chat, projects, models/*, admin/*) is its own
        // server-rendered page at its own URL now, so switching sections is
        // a normal browser navigation -- there is no client-side router.
        // Curated, size-tiered GGUF suggestions. Every commit hash and SHA-256
        // below was read directly from the Hugging Face API so the server's
        // immutable-revision and digest checks pass without hand-editing.
        "const PRESETS=["
        "{id:'tiny-test',tier:'test',label:'ggml-org/models tinyllamas stories260K (~1 MB)',"
        "category:'general-programming',modelId:'stories260k-test',filename:'stories260K.gguf',"
        "sourceUrl:'https://huggingface.co/ggml-org/models/resolve/499bc8821c6b12b4e53c5bffcb21ec206f212d81/tinyllamas/stories260K.gguf',"
        "revision:'499bc8821c6b12b4e53c5bffcb21ec206f212d81',"
        "sha256:'270cba1bd5109f42d03350f60406024560464db173c0e387d91f0426d3bd256d',"
        "minRam:64,recRam:128,sizeBytes:1185376,"
        "displayName:'TinyStories 260K (test fixture)',architecture:'llama',"
        "quantization:'F32',licenseSpdx:'MIT'},"
        "{id:'qwen25-1.5b-q4km',tier:'2',label:'Qwen2.5-Coder-1.5B-Instruct Q4_K_M (~1.1 GiB)',"
        "category:'general-programming',modelId:'qwen25-coder-1.5b-q4km',"
        "filename:'qwen2.5-coder-1.5b-instruct-q4_k_m.gguf',"
        "sourceUrl:'https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/f86cb2c1fa58255f8052cc32aeede1b7482d4361/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf',"
        "revision:'f86cb2c1fa58255f8052cc32aeede1b7482d4361',"
        "sha256:'cc324af070c2ecbfd324a30884d2f951a7ff756aba85cb811a6ec436933bb046',"
        "minRam:1536,recRam:2048,sizeBytes:1117320768,"
        "displayName:'Qwen2.5-Coder-1.5B-Instruct',architecture:'qwen2',"
        "quantization:'Q4_K_M',licenseSpdx:'Apache-2.0'},"
        "{id:'qwen25-3b-q4km',tier:'4',label:'Qwen2.5-Coder-3B-Instruct Q4_K_M (~2.0 GiB)',"
        "category:'general-programming',modelId:'qwen25-coder-3b-q4km',"
        "filename:'qwen2.5-coder-3b-instruct-q4_k_m.gguf',"
        "sourceUrl:'https://huggingface.co/Qwen/Qwen2.5-Coder-3B-Instruct-GGUF/resolve/f74adce6aa16316c625447af059dbebe4983757c/qwen2.5-coder-3b-instruct-q4_k_m.gguf',"
        "revision:'f74adce6aa16316c625447af059dbebe4983757c',"
        "sha256:'724fb256bec1ff062b2f65e4569e871ad2e95ab2a3989723d1769c54294730b7',"
        "minRam:3072,recRam:4096,sizeBytes:2104932800,"
        "displayName:'Qwen2.5-Coder-3B-Instruct',architecture:'qwen2',"
        "quantization:'Q4_K_M',licenseSpdx:'Apache-2.0'},"
        "{id:'qwen25-7b-q4km',tier:'8',label:'Qwen2.5-Coder-7B-Instruct Q4_K_M (~4.4 GiB)',"
        "category:'general-programming',modelId:'qwen25-coder-7b-q4km',"
        "filename:'qwen2.5-coder-7b-instruct-q4_k_m.gguf',"
        "sourceUrl:'https://huggingface.co/Qwen/Qwen2.5-Coder-7B-Instruct-GGUF/resolve/13fb94bfda8c8cf22497dc57b78f391a9acb426a/qwen2.5-coder-7b-instruct-q4_k_m.gguf',"
        "revision:'13fb94bfda8c8cf22497dc57b78f391a9acb426a',"
        "sha256:'509287f78cb4d4cf6b3843734733b914b2c158e43e22a7f4bf5e963800894d3c',"
        "minRam:6144,recRam:8192,sizeBytes:4683073536,"
        "displayName:'Qwen2.5-Coder-7B-Instruct',architecture:'qwen2',"
        "quantization:'Q4_K_M',licenseSpdx:'Apache-2.0'},"
        "{id:'qwen25-14b-q4km',tier:'16',label:'Qwen2.5-Coder-14B-Instruct Q4_K_M (~8.4 GiB)',"
        "category:'general-programming',modelId:'qwen25-coder-14b-q4km',"
        "filename:'qwen2.5-coder-14b-instruct-q4_k_m.gguf',"
        "sourceUrl:'https://huggingface.co/Qwen/Qwen2.5-Coder-14B-Instruct-GGUF/resolve/d0a692ef765eefbf2fabb130b3cb2e8917e3d225/qwen2.5-coder-14b-instruct-q4_k_m.gguf',"
        "revision:'d0a692ef765eefbf2fabb130b3cb2e8917e3d225',"
        "sha256:'c1e659736d89ac1065fb495330fb824d94001974a4bfa78e7270e43476a8d940',"
        "minRam:11264,recRam:16384,sizeBytes:8988110272,"
        "displayName:'Qwen2.5-Coder-14B-Instruct',architecture:'qwen2',"
        "quantization:'Q4_K_M',licenseSpdx:'Apache-2.0'},"
        "{id:'qwen25-32b-q4km',tier:'24',label:'Qwen2.5-Coder-32B-Instruct Q4_K_M (~18.5 GiB)',"
        "category:'general-programming',modelId:'qwen25-coder-32b-q4km',"
        "filename:'qwen2.5-coder-32b-instruct-q4_k_m.gguf',"
        "sourceUrl:'https://huggingface.co/Qwen/Qwen2.5-Coder-32B-Instruct-GGUF/resolve/9d3053fce650fe1cdbdb75998c2a87add9d178ef/qwen2.5-coder-32b-instruct-q4_k_m.gguf',"
        "revision:'9d3053fce650fe1cdbdb75998c2a87add9d178ef',"
        "sha256:'4d64b316b5e6319d9613e0d97935d9ebd631fc7e334da400d00085eca749d085',"
        "minRam:20480,recRam:24576,sizeBytes:19851335872,"
        "displayName:'Qwen2.5-Coder-32B-Instruct',architecture:'qwen2',"
        "quantization:'Q4_K_M',licenseSpdx:'Apache-2.0'},"
        "{id:'qwen25-32b-q6k',tier:'32',label:'Qwen2.5-Coder-32B-Instruct Q6_K (~25 GiB)',"
        "category:'general-programming',modelId:'qwen25-coder-32b-q6k',"
        "filename:'qwen2.5-coder-32b-instruct-q6_k.gguf',"
        "sourceUrl:'https://huggingface.co/Qwen/Qwen2.5-Coder-32B-Instruct-GGUF/resolve/9d3053fce650fe1cdbdb75998c2a87add9d178ef/qwen2.5-coder-32b-instruct-q6_k.gguf',"
        "revision:'9d3053fce650fe1cdbdb75998c2a87add9d178ef',"
        "sha256:'38c1555adabcc7e9dfdae217cfbfdea53c97996a1ca17bd00125cf32bbdc63c2',"
        "minRam:27648,recRam:32768,sizeBytes:26886154432,"
        "displayName:'Qwen2.5-Coder-32B-Instruct',architecture:'qwen2',"
        "quantization:'Q6_K',licenseSpdx:'Apache-2.0'},"
        "{id:'qwen25-32b-q8',tier:'64',label:'Qwen2.5-Coder-32B-Instruct Q8_0 (~32.4 GiB)',"
        "category:'general-programming',modelId:'qwen25-coder-32b-q8',"
        "filename:'qwen2.5-coder-32b-instruct-q8_0.gguf',"
        "sourceUrl:'https://huggingface.co/Qwen/Qwen2.5-Coder-32B-Instruct-GGUF/resolve/9d3053fce650fe1cdbdb75998c2a87add9d178ef/qwen2.5-coder-32b-instruct-q8_0.gguf',"
        "revision:'9d3053fce650fe1cdbdb75998c2a87add9d178ef',"
        "sha256:'ae6e5cee79233499b41502ea7270e665dc402b2c35c48d43ef2d5a0a10842725',"
        "minRam:36864,recRam:49152,sizeBytes:34820884672,"
        "displayName:'Qwen2.5-Coder-32B-Instruct',architecture:'qwen2',"
        "quantization:'Q8_0',licenseSpdx:'Apache-2.0'},"
        // Non-Qwen families, spanning the same tiers, so a given RAM budget
        // is never a one-model choice. Every commit hash, SHA-256, and size
        // below was likewise read directly from the Hugging Face API.
        "{id:'deepseek-coder-1.3b-q4km',tier:'1',"
        "label:'DeepSeek-Coder-1.3B-Instruct Q4_K_M (~0.8 GiB)',"
        "category:'general-programming',modelId:'deepseek-coder-1.3b-q4km',"
        "filename:'deepseek-coder-1.3b-instruct.Q4_K_M.gguf',"
        "sourceUrl:'https://huggingface.co/TheBloke/deepseek-coder-1.3b-instruct-GGUF/resolve/4595af8c3dff738094bd6c86054dfb5a90d5c41e/deepseek-coder-1.3b-instruct.Q4_K_M.gguf',"
        "revision:'4595af8c3dff738094bd6c86054dfb5a90d5c41e',"
        "sha256:'04cebb6fafa40ae628cf6bfeb76032ec792852f54020c559ad0a56b9f2839118',"
        "minRam:768,recRam:1024,sizeBytes:873582624},"
        "{id:'codegemma-2b-q4km',tier:'3',label:'CodeGemma-2B Q4_K_M (~1.5 GiB)',"
        "category:'general-programming',modelId:'codegemma-2b-q4km',"
        "filename:'codegemma-2b-Q4_K_M.gguf',"
        "sourceUrl:'https://huggingface.co/bartowski/codegemma-2b-GGUF/resolve/4c371c2e831eebf3de95e32c841c8b38d4d9502e/codegemma-2b-Q4_K_M.gguf',"
        "revision:'4c371c2e831eebf3de95e32c841c8b38d4d9502e',"
        "sha256:'4747caaf4dc51a6e7ad6df5fe26f4c485d847998bd58933eaf011f76e5273c14',"
        "minRam:1536,recRam:3072,sizeBytes:1630262400,"
        "displayName:'CodeGemma-2B',architecture:'gemma',"
        "quantization:'Q4_K_M',licenseSpdx:'Gemma'},"
        "{id:'starcoder2-3b-q4km',tier:'3',label:'StarCoder2-3B Q4_K_M (~1.7 GiB)',"
        "category:'general-programming',modelId:'starcoder2-3b-q4km',"
        "filename:'starcoder2-3b-Q4_K_M.gguf',"
        "sourceUrl:'https://huggingface.co/second-state/StarCoder2-3B-GGUF/resolve/7fca3e2da2ce31df411461e2cb9cae2d2b492f35/starcoder2-3b-Q4_K_M.gguf',"
        "revision:'7fca3e2da2ce31df411461e2cb9cae2d2b492f35',"
        "sha256:'d8fb39287a463549b80d97473b0a7595c3a5a6da3ae2604ca33906a1a43f7175',"
        "minRam:2048,recRam:3072,sizeBytes:1848976448},"
        "{id:'phi35-mini-q4km',tier:'3',label:'Phi-3.5-mini-Instruct Q4_K_M (~2.2 GiB)',"
        "category:'general-programming',modelId:'phi35-mini-q4km',"
        "filename:'Phi-3.5-mini-instruct-Q4_K_M.gguf',"
        "sourceUrl:'https://huggingface.co/bartowski/Phi-3.5-mini-instruct-GGUF/resolve/6d70da17e749a471ccb62ade694486011a75cda3/Phi-3.5-mini-instruct-Q4_K_M.gguf',"
        "revision:'6d70da17e749a471ccb62ade694486011a75cda3',"
        "sha256:'e4165e3a71af97f1b4820da61079826d8752a2088e313af0c7d346796c38eff5',"
        "minRam:2048,recRam:3072,sizeBytes:2393232672,"
        "displayName:'Phi-3.5-mini-Instruct',architecture:'phi3',"
        "quantization:'Q4_K_M',licenseSpdx:'MIT'},"
        "{id:'deepseek-coder-6.7b-q4km',tier:'5',"
        "label:'DeepSeek-Coder-6.7B-Instruct Q4_K_M (~3.8 GiB)',"
        "category:'general-programming',modelId:'deepseek-coder-6.7b-q4km',"
        "filename:'deepseek-coder-6.7b-instruct.Q4_K_M.gguf',"
        "sourceUrl:'https://huggingface.co/TheBloke/deepseek-coder-6.7B-instruct-GGUF/resolve/9e221e6b41cb1bf1c5d8f9718e81e3dc781f7557/deepseek-coder-6.7b-instruct.Q4_K_M.gguf',"
        "revision:'9e221e6b41cb1bf1c5d8f9718e81e3dc781f7557',"
        "sha256:'92da6238854f2fa902d8b2ad79d548536af1d3ab06821f323bd5bbcea2013276',"
        "minRam:3840,recRam:5120,sizeBytes:4083015904},"
        "{id:'codellama-7b-q4km',tier:'5',label:'CodeLlama-7B-Instruct Q4_K_M (~3.8 GiB)',"
        "category:'general-programming',modelId:'codellama-7b-q4km',"
        "filename:'codellama-7b-instruct.Q4_K_M.gguf',"
        "sourceUrl:'https://huggingface.co/TheBloke/CodeLlama-7B-Instruct-GGUF/resolve/2f064ee0c6ae3f025ec4e392c6ba5dd049c77969/codellama-7b-instruct.Q4_K_M.gguf',"
        "revision:'2f064ee0c6ae3f025ec4e392c6ba5dd049c77969',"
        "sha256:'0701500c591c2c1b910516658e58044cdfa07b2e8b5a2e3b6808d983441daf1a',"
        "minRam:3840,recRam:5120,sizeBytes:4081095360},"
        "{id:'starcoder2-7b-q4km',tier:'5',label:'StarCoder2-7B Q4_K_M (~4.2 GiB)',"
        "category:'general-programming',modelId:'starcoder2-7b-q4km',"
        "filename:'starcoder2-7b.Q4_K_M.gguf',"
        "sourceUrl:'https://huggingface.co/QuantFactory/starcoder2-7b-GGUF/resolve/673788033750e9a7a677072a5184e3c923c19355/starcoder2-7b.Q4_K_M.gguf',"
        "revision:'673788033750e9a7a677072a5184e3c923c19355',"
        "sha256:'c6f8a3f618bfc1c2cf2172a6d28d43d1e3c3b0489544fd13ef3a73939728d943',"
        "minRam:4096,recRam:5120,sizeBytes:4461280384},"
        "{id:'yi-coder-9b-q4km',tier:'6',"
        "label:'Yi-Coder-9B-Chat Q4_K_M (~5 GiB)',"
        "category:'general-programming',modelId:'yi-coder-9b-q4km',"
        "filename:'Yi-Coder-9B-Chat-Q4_K_M.gguf',"
        "sourceUrl:'https://huggingface.co/bartowski/Yi-Coder-9B-Chat-GGUF/resolve/2f28bfc370a5457310f3880202b2ed577e2bcbd8/Yi-Coder-9B-Chat-Q4_K_M.gguf',"
        "revision:'2f28bfc370a5457310f3880202b2ed577e2bcbd8',"
        "sha256:'251cc196e3813d149694f362bb0f8f154f3320abe44724eebe58c23dc54f201d',"
        "minRam:5120,recRam:6144,sizeBytes:5328958272,"
        "displayName:'Yi-Coder-9B-Chat',architecture:'yi',"
        "quantization:'Q4_K_M',licenseSpdx:'Apache-2.0'},"
        "{id:'codegemma-7b-q4km',tier:'7',label:'CodeGemma-7B Q4_K_M (~5 GiB)',"
        "category:'general-programming',modelId:'codegemma-7b-q4km',"
        "filename:'codegemma-7b-Q4_K_M.gguf',"
        "sourceUrl:'https://huggingface.co/bartowski/codegemma-7b-GGUF/resolve/7445262558e223ac999c8c34c6bcbb6e35821a47/codegemma-7b-Q4_K_M.gguf',"
        "revision:'7445262558e223ac999c8c34c6bcbb6e35821a47',"
        "sha256:'5d36c9391069f7c54339a71a52ed8c0bb36219cba621189fc5427d4cdc6c8e5a',"
        "minRam:5120,recRam:7168,sizeBytes:5329758592,"
        "displayName:'CodeGemma-7B',architecture:'gemma',"
        "quantization:'Q4_K_M',licenseSpdx:'Gemma'},"
        "{id:'codellama-13b-q3km',tier:'7',"
        "label:'CodeLlama-13B-Instruct Q3_K_M (~5.9 GiB)',"
        "category:'general-programming',modelId:'codellama-13b-q3km',"
        "filename:'codellama-13b-instruct.Q3_K_M.gguf',"
        "sourceUrl:'https://huggingface.co/TheBloke/CodeLlama-13B-Instruct-GGUF/resolve/82f1dd9567b9b20b7e8f8aa9ecf3d2f121e5d415/codellama-13b-instruct.Q3_K_M.gguf',"
        "revision:'82f1dd9567b9b20b7e8f8aa9ecf3d2f121e5d415',"
        "sha256:'68357eb6af266639528c632483d86db554b1f3346dc9d2afc67702a1623b1a99',"
        "minRam:5632,recRam:7168,sizeBytes:6337872256},"
        "{id:'codellama-13b-q4km',tier:'8',"
        "label:'CodeLlama-13B-Instruct Q4_K_M (~7.3 GiB)',"
        "category:'general-programming',modelId:'codellama-13b-q4km',"
        "filename:'codellama-13b-instruct.Q4_K_M.gguf',"
        "sourceUrl:'https://huggingface.co/TheBloke/CodeLlama-13B-Instruct-GGUF/resolve/82f1dd9567b9b20b7e8f8aa9ecf3d2f121e5d415/codellama-13b-instruct.Q4_K_M.gguf',"
        "revision:'82f1dd9567b9b20b7e8f8aa9ecf3d2f121e5d415',"
        "sha256:'48cc5600c5e35b1226208a53b1871f50efb15764232babaef23e2264c285d7d9',"
        "minRam:6144,recRam:8192,sizeBytes:7866070016},"
        "{id:'deepseek-v2-lite-q3ks',tier:'8',"
        "label:'DeepSeek-Coder-V2-Lite-Instruct Q3_K_S (~7 GiB)',"
        "category:'general-programming',modelId:'deepseek-v2-lite-q3ks',"
        "filename:'DeepSeek-Coder-V2-Lite-Instruct-Q3_K_S.gguf',"
        "sourceUrl:'https://huggingface.co/bartowski/DeepSeek-Coder-V2-Lite-Instruct-GGUF/resolve/8f248fa2072348f77a8bc37754e470de1f61866e/DeepSeek-Coder-V2-Lite-Instruct-Q3_K_S.gguf',"
        "revision:'8f248fa2072348f77a8bc37754e470de1f61866e',"
        "sha256:'508e1ead6515d50a68d3d0f1e0d7f0c29f3ca351404703266793af6708ea89f5',"
        "minRam:6144,recRam:8192,sizeBytes:7487663872},"
        "{id:'codellama-34b-q3ks',tier:'16',"
        "label:'CodeLlama-34B-Instruct Q3_K_S (~13.6 GiB)',"
        "category:'general-programming',modelId:'codellama-34b-q3ks',"
        "filename:'codellama-34b-instruct.Q3_K_S.gguf',"
        "sourceUrl:'https://huggingface.co/TheBloke/CodeLlama-34B-Instruct-GGUF/resolve/7b84402c234acb1c5be542b5ecfc820ea3b74422/codellama-34b-instruct.Q3_K_S.gguf',"
        "revision:'7b84402c234acb1c5be542b5ecfc820ea3b74422',"
        "sha256:'08b5aec470700ed1a703299d95dcf8ece296e99877ee99fbb040f09a79a2e4fa',"
        "minRam:12288,recRam:16384,sizeBytes:14605349024},"
        "{id:'deepseek-v2-lite-q6k',tier:'16',"
        "label:'DeepSeek-Coder-V2-Lite-Instruct Q6_K (~13.1 GiB)',"
        "category:'general-programming',modelId:'deepseek-v2-lite-q6k',"
        "filename:'DeepSeek-Coder-V2-Lite-Instruct-Q6_K.gguf',"
        "sourceUrl:'https://huggingface.co/bartowski/DeepSeek-Coder-V2-Lite-Instruct-GGUF/resolve/8f248fa2072348f77a8bc37754e470de1f61866e/DeepSeek-Coder-V2-Lite-Instruct-Q6_K.gguf',"
        "revision:'8f248fa2072348f77a8bc37754e470de1f61866e',"
        "sha256:'1ff79f43ad5728d3179bf8fa7ee2993652f4306d6aeca9c35055f4f5b7b864cd',"
        "minRam:12288,recRam:16384,sizeBytes:14066972416},"
        "{id:'codellama-34b-q4km',tier:'24',"
        "label:'CodeLlama-34B-Instruct Q4_K_M (~18.8 GiB)',"
        "category:'general-programming',modelId:'codellama-34b-q4km',"
        "filename:'codellama-34b-instruct.Q4_K_M.gguf',"
        "sourceUrl:'https://huggingface.co/TheBloke/CodeLlama-34B-Instruct-GGUF/resolve/7b84402c234acb1c5be542b5ecfc820ea3b74422/codellama-34b-instruct.Q4_K_M.gguf',"
        "revision:'7b84402c234acb1c5be542b5ecfc820ea3b74422',"
        "sha256:'57290fe55636910ab11b935dbe675d19781d06bd8020594d9135e06477e3c2bf',"
        "minRam:18432,recRam:24576,sizeBytes:20219900064},"
        // OpenAI's gpt-oss line is its only line of downloadable, openly
        // licensed weights (Apache-2.0) -- Claude and ChatGPT proper have no
        // downloadable weights anywhere, so they cannot appear here. Commit
        // hash and SHA-256 read directly from the Hugging Face API.
        "{id:'gpt-oss-20b-q4km',tier:'16',label:'gpt-oss-20b Q4_K_M (~10.8 GiB)',"
        "category:'general-programming',modelId:'gpt-oss-20b-q4km',"
        "filename:'gpt-oss-20b-Q4_K_M.gguf',"
        "sourceUrl:'https://huggingface.co/unsloth/gpt-oss-20b-GGUF/resolve/d449b42d93e1c2c7bda5312f5c25c8fb91dfa9b4/gpt-oss-20b-Q4_K_M.gguf',"
        "revision:'d449b42d93e1c2c7bda5312f5c25c8fb91dfa9b4',"
        "sha256:'c27536640e410032865dc68781d80a08b98f8db5e93575919af8ccc0568aeb4f',"
        "minRam:11264,recRam:14336,sizeBytes:11624759488,"
        "displayName:'gpt-oss-20b',architecture:'gpt-oss',"
        "quantization:'Q4_K_M',licenseSpdx:'Apache-2.0'},"
        "{id:'gpt-oss-20b-q8',tier:'16',label:'gpt-oss-20b Q8_0 (~11.3 GiB)',"
        "category:'general-programming',modelId:'gpt-oss-20b-q8',"
        "filename:'gpt-oss-20b-Q8_0.gguf',"
        "sourceUrl:'https://huggingface.co/unsloth/gpt-oss-20b-GGUF/resolve/d449b42d93e1c2c7bda5312f5c25c8fb91dfa9b4/gpt-oss-20b-Q8_0.gguf',"
        "revision:'d449b42d93e1c2c7bda5312f5c25c8fb91dfa9b4',"
        "sha256:'bcd455d4034ec02f71a875b46cb17df44a97911d7258291973be4d21f98329f3',"
        "minRam:12288,recRam:16384,sizeBytes:12109567168,"
        "displayName:'gpt-oss-20b',architecture:'gpt-oss',"
        "quantization:'Q8_0',licenseSpdx:'Apache-2.0'},"
        // ModelScope mirrors, so a given RAM budget is never a one-host
        // choice either. Every commit hash and SHA-256 below was read
        // directly from the ModelScope repo-files API.
        "{id:'starcoder2-3b-q4km-ms',tier:'3',"
        "label:'StarCoder2-3B Q4_K_M via ModelScope (~1.7 GiB)',"
        "category:'general-programming',modelId:'starcoder2-3b-q4km-ms',"
        "filename:'starcoder2-3b-Q4_K_M.gguf',"
        "sourceUrl:'https://modelscope.cn/models/second-state/StarCoder2-3B-GGUF/resolve/"
        "ad0cce4b6ae76131a5077a21ac72eee64bdb1a45/starcoder2-3b-Q4_K_M.gguf',"
        "revision:'ad0cce4b6ae76131a5077a21ac72eee64bdb1a45',"
        "sha256:'d8fb39287a463549b80d97473b0a7595c3a5a6da3ae2604ca33906a1a43f7175',"
        "minRam:2048,recRam:3072,sizeBytes:1848976448},"
        "{id:'phi35-mini-q4km-ms',tier:'3',"
        "label:'Phi-3.5-mini-Instruct Q4_K_M via ModelScope (~2.2 GiB)',"
        "category:'general-programming',modelId:'phi35-mini-q4km-ms',"
        "filename:'Phi-3.5-mini-instruct-Q4_K_M.gguf',"
        "sourceUrl:'https://modelscope.cn/models/second-state/Phi-3.5-mini-instruct-GGUF/resolve/"
        "6240e2431eb84b0b091b3e226b5c78ce2a1086bc/Phi-3.5-mini-instruct-Q4_K_M.gguf',"
        "revision:'6240e2431eb84b0b091b3e226b5c78ce2a1086bc',"
        "sha256:'c389ea28dc7f10dfbe30fc5e05452f832b1adf85989253ba590e3620b9584f06',"
        "minRam:2048,recRam:3072,sizeBytes:2393232384,"
        "displayName:'Phi-3.5-mini-Instruct',architecture:'phi3',"
        "quantization:'Q4_K_M',licenseSpdx:'MIT'},"
        "{id:'codegemma-7b-it-q4km-ms',tier:'7',"
        "label:'CodeGemma-7B-it Q4_K_M via ModelScope (~5 GiB)',"
        "category:'general-programming',modelId:'codegemma-7b-it-q4km-ms',"
        "filename:'codegemma-7b-it-Q4_K_M.gguf',"
        "sourceUrl:'https://modelscope.cn/models/second-state/CodeGemma-7b-it-GGUF/resolve/"
        "5c22ebd36d051418d121946acd183d8b8d530e34/codegemma-7b-it-Q4_K_M.gguf',"
        "revision:'5c22ebd36d051418d121946acd183d8b8d530e34',"
        "sha256:'7447e29e28f01ef593a4b0758cfb759a414ac32bbd65084509107e7091683fdc',"
        "minRam:5120,recRam:7168,sizeBytes:5329759232,"
        "displayName:'CodeGemma-7B-it',architecture:'gemma',"
        "quantization:'Q4_K_M',licenseSpdx:'Gemma'},"
        "{id:'deepseek-v2-lite-q4km-ms',tier:'16',"
        "label:'DeepSeek-Coder-V2-Lite-Instruct Q4_K_M via ModelScope (~9.7 GiB)',"
        "category:'general-programming',modelId:'deepseek-v2-lite-q4km-ms',"
        "filename:'DeepSeek-Coder-V2-Lite-Instruct-Q4_K_M.gguf',"
        "sourceUrl:'https://modelscope.cn/models/second-state/"
        "DeepSeek-Coder-V2-Lite-Instruct-GGUF/resolve/"
        "fea4380e9006b556991fd088706b6c7ea69977d1/"
        "DeepSeek-Coder-V2-Lite-Instruct-Q4_K_M.gguf',"
        "revision:'fea4380e9006b556991fd088706b6c7ea69977d1',"
        "sha256:'38bc76f3326b49b4d81d1027d092bf7ce5b4ed2de4136d1d2e7e6347c3ec8376',"
        "minRam:8192,recRam:10240,sizeBytes:10364416480},"
        "{id:'codellama-13b-q4km-ms',tier:'8',"
        "label:'CodeLlama-13B-Instruct Q4_K_M via ModelScope (~7.3 GiB)',"
        "category:'general-programming',modelId:'codellama-13b-q4km-ms',"
        "filename:'CodeLlama-13b-Instruct-hf-Q4_K_M.gguf',"
        "sourceUrl:'https://modelscope.cn/models/second-state/CodeLlama-13B-Instruct-GGUF/resolve/"
        "c1e2967a2531788fbbf5e6969ebaac55fec7fcae/CodeLlama-13b-Instruct-hf-Q4_K_M.gguf',"
        "revision:'c1e2967a2531788fbbf5e6969ebaac55fec7fcae',"
        "sha256:'e2ad727d4893bc44add809e992c8f584e4fb1e986163a5b7510e2cc1f34b3c55',"
        "minRam:6144,recRam:8192,sizeBytes:7866070080},"
        // General-purpose conversation/chat models, as distinct from the
        // coding-focused suggestions above -- same sourcing rule applies:
        // every commit hash and SHA-256 here was read directly from the
        // Hugging Face API, not computed locally.
        "{id:'qwen25-1.5b-instruct-q4km',tier:'2',"
        "label:'Qwen2.5-1.5B-Instruct Q4_K_M (~1.0 GiB)',"
        "category:'conversation',modelId:'qwen25-1.5b-instruct-q4km',"
        "filename:'qwen2.5-1.5b-instruct-q4_k_m.gguf',"
        "sourceUrl:'https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/"
        "91cad51170dc346986eccefdc2dd33a9da36ead9/qwen2.5-1.5b-instruct-q4_k_m.gguf',"
        "revision:'91cad51170dc346986eccefdc2dd33a9da36ead9',"
        "sha256:'6a1a2eb6d15622bf3c96857206351ba97e1af16c30d7a74ee38970e434e9407e',"
        "minRam:1536,recRam:2048,sizeBytes:1117320736,"
        "displayName:'Qwen2.5-1.5B-Instruct',architecture:'qwen2',"
        "quantization:'Q4_K_M',licenseSpdx:'Apache-2.0'},"
        "{id:'llama32-3b-instruct-q4km',tier:'4',"
        "label:'Llama-3.2-3B-Instruct Q4_K_M (~1.9 GiB)',"
        "category:'conversation',modelId:'llama32-3b-instruct-q4km',"
        "filename:'Llama-3.2-3B-Instruct-Q4_K_M.gguf',"
        "sourceUrl:'https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/"
        "5ab33fa94d1d04e903623ae72c95d1696f09f9e8/Llama-3.2-3B-Instruct-Q4_K_M.gguf',"
        "revision:'5ab33fa94d1d04e903623ae72c95d1696f09f9e8',"
        "sha256:'6c1a2b41161032677be168d354123594c0e6e67d2b9227c84f296ad037c728ff',"
        "minRam:3072,recRam:4096,sizeBytes:2019377696,"
        "displayName:'Llama-3.2-3B-Instruct',architecture:'llama',"
        "quantization:'Q4_K_M',licenseSpdx:'Llama-3.2'},"
        "{id:'llama31-8b-instruct-q4km',tier:'8',"
        "label:'Meta-Llama-3.1-8B-Instruct Q4_K_M (~4.6 GiB)',"
        "category:'conversation',modelId:'llama31-8b-instruct-q4km',"
        "filename:'Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf',"
        "sourceUrl:'https://huggingface.co/bartowski/Meta-Llama-3.1-8B-Instruct-GGUF/resolve/"
        "bf5b95e96dac0462e2a09145ec66cae9a3f12067/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf',"
        "revision:'bf5b95e96dac0462e2a09145ec66cae9a3f12067',"
        "sha256:'7b064f5842bf9532c91456deda288a1b672397a54fa729aa665952863033557c',"
        "minRam:6144,recRam:8192,sizeBytes:4920739232,"
        "displayName:'Meta-Llama-3.1-8B-Instruct',architecture:'llama',"
        "quantization:'Q4_K_M',licenseSpdx:'Llama-3.1'}];"
        // Rebuilds the suggestion dropdown for the selected RAM tier and
        // immediately applies the first match. Auto-applying here (rather
        // than requiring a separate button click) is what prevents the
        // stale-form trap: switching tiers can no longer leave the form
        // holding a previous tier's model while looking like it was updated.
        // Every preset's sourceUrl host says which of the Download source
        // options (huggingface/github/modelscope) it actually came from --
        // used below so the suggestion list only ever offers models that
        // match the source currently selected, instead of mixing in
        // suggestions whose URL wouldn't even match the visible source
        // fields. 'custom' matches nothing, since a hand-typed source has no
        // curated suggestions.
        "function presetSource(p){"
        "if(p.sourceUrl.includes('huggingface.co'))return 'huggingface';"
        "if(p.sourceUrl.includes('modelscope.cn'))return 'modelscope';"
        "if(p.sourceUrl.includes('github.com'))return 'github';"
        "return 'custom';}"
        "function refreshPresets(){const sel=q('#downloadPreset');if(!sel)return;"
        "sel.replaceChildren();const tier=q('#downloadTier').value;"
        "const type=q('#downloadSourceType').value;"
        "for(const p of PRESETS.filter(x=>x.tier===tier&&presetSource(x)===type)){"
        "const o=document.createElement('option');o.value=p.id;o.textContent=p.label;"
        "sel.append(o);}"
        "if(sel.options.length)applyPreset();}"
        // Copies the selected preset's verified fields into the raw form;
        // license acceptance is left to the operator, never auto-checked.
        // Not every preset carries manifest metadata (architecture,
        // quantization, an SPDX id this server's allow-list recognizes) --
        // those without it downloaded the file fine but can't reach Ready
        // without an administrator hand-authoring a compliant manifest, so
        // the status line says so instead of silently leaving blank fields.
        "function applyPreset(){const p=PRESETS.find(x=>x.id===q('#downloadPreset').value);"
        "if(!p)return;lastAppliedPreset=p;q('#downloadCategory').value=p.category;"
        "q('#downloadModelId').value=p.modelId;q('#downloadFilename').value=p.filename;"
        "q('#downloadSourceUrl').value=p.sourceUrl;q('#downloadRevision').value=p.revision;"
        "q('#downloadSha256').value=p.sha256;q('#downloadMinRam').value=p.minRam;"
        "q('#downloadRecRam').value=p.recRam;"
        "q('#downloadDisplayName').value=p.displayName||'';"
        "q('#downloadArchitecture').value=p.architecture||'';"
        "q('#downloadQuantization').value=p.quantization||'';"
        "q('#downloadLicenseSpdx').value=p.licenseSpdx||'';"
        "q('#downloadSizeBytes').value=p.sizeBytes;"
        "q('#actionStatus').textContent=p.licenseSpdx?"
        "'Loaded suggestion: '+p.label+"
        "'. Review its license on Hugging Face before accepting.':"
        "'Loaded suggestion: '+p.label+"
        "\". This model's license isn't in the recognized list -- fill in \"+"
        "'Display name/Architecture/Quantization/License yourself, using a '+"
        "'license this server accepts, before queueing.';}"
        // Queueing immediately starts the transfer too -- Active downloads
        // (now the bottom of this same page) is where progress and any
        // later resume happens, so there is nothing left to hand-type here.
        "async function queueDownload(e){e.preventDefault();const s=q('#actionStatus');"
        "try{const body={filename:q('#downloadFilename').value,"
        "category:q('#downloadCategory').value,modelId:q('#downloadModelId').value,"
        "sourceUrl:q('#downloadSourceUrl').value,immutableRevision:q('#downloadRevision').value,"
        "expectedSha256:q('#downloadSha256').value,licenseAccepted:q('#downloadLicense').checked,"
        "displayName:q('#downloadDisplayName').value,"
        "architecture:q('#downloadArchitecture').value,"
        "quantization:q('#downloadQuantization').value,"
        "licenseSpdx:q('#downloadLicenseSpdx').value,"
        "sizeBytes:Number(q('#downloadSizeBytes').value),"
        "minimumRamMiB:Number(q('#downloadMinRam').value),"
        "recommendedRamMiB:Number(q('#downloadRecRam').value)};"
        "const r=await api('/api/v1/model-downloads','POST',body);"
        // Only trust a preset's known total size for the progress bar when
        // the form still matches that preset (the operator may have edited
        // the source URL by hand after loading a suggestion).
        "downloadSizes[r.id]=lastAppliedPreset&&"
        "lastAppliedPreset.sourceUrl===body.sourceUrl?lastAppliedPreset.sizeBytes:null;"
        "s.textContent='Queued download '+r.id+' ('+r.hardwareRecommendation+"
        "'). Starting transfer...';await load();await runDownloadJob(r.id);}"
        "catch(x){showSystemError('Queue failed: '+x.message);}}"
        // Polls the job-list route (the /run response itself only arrives
        // once the whole transfer finishes) and updates that row's state and
        // progress cells from the polled job, using a known total size when
        // available or falling back to a plain byte counter. Both cells are
        // updated from the same poll so State never goes stale while
        // Progress keeps moving.
        "const TERMINAL_DOWNLOAD_STATES=new Set("
        "['complete','failed','quarantined','cancelled','paused']);"
        "async function pollDownloadProgress(id){const cell=q('#progress-'+id);"
        "const stateCell=q('#state-'+id);"
        "const total=downloadSizes[id];let handle;"
        "const tick=async()=>{try{const d=await api('/api/v1/model-downloads');"
        "const job=d.downloads.find(x=>x.id===id);if(!job)return;"
        "if(stateCell)stateCell.textContent=job.state;"
        "const mib=(job.completedBytes/1048576).toFixed(1);"
        "if(cell){if(total){cell.textContent=mib+' / '+(total/1048576).toFixed(1)+"
        "' MiB ('+Math.min(100,Math.round(job.completedBytes/total*100))+'%)';}"
        "else cell.textContent=mib+' MiB';}"
        // Stops itself once the transfer is no longer running -- this same
        // function is also how renderDownloads auto-resumes progress for a
        // job left 'transferring' across a page reload, so without this it
        // would keep polling a finished (or paused) job forever. A paused
        // job still needs a full re-render (via load()) to swap its row's
        // buttons back to Start/resume, so this only stops the polling loop,
        // not the eventual refresh.
        "if(TERMINAL_DOWNLOAD_STATES.has(job.state)){clearInterval(handle);"
        "delete activePolls[id];}"
        "}catch(x){console.warn('download poll tick failed, retrying next "
        "interval',x);}};"
        "await tick();handle=setInterval(tick,1000);return handle;}"
        // The server runs the transfer synchronously and only responds once
        // the job finishes, so this request can legitimately stay pending
        // for a long time on a large model; the progress cell above is what
        // fills that gap. Called both right after queueing and from a
        // row's own Start/resume button for a job that stalled or paused.
        "async function runDownloadJob(id){const s=q('#actionStatus');"
        "s.textContent='Downloading '+id+'... this can take a long time for large "
        "models; the page will keep working while it runs.';"
        "const timer=await pollDownloadProgress(id);"
        "try{const r=await api('/api/v1/model-downloads/'+encodeURIComponent(id)+'/run','POST');"
        "s.textContent='Download '+id+' finished with state: '+r.state;}"
        "catch(x){showSystemError('Download failed: '+x.message);}"
        "finally{if(timer)clearInterval(timer);await load();}}"
        // Pause/Stop signal the in-flight run() call (still pending on the
        // /run request started by runDownloadJob) rather than perform the
        // state change themselves -- that request's own 'finally' above is
        // what refreshes this row once the transfer actually lands in its
        // new state, so both handlers here just poll once immediately after
        // to reflect the signal being accepted.
        "async function pauseDownload(id){const s=q('#actionStatus');"
        "try{await api('/api/v1/model-downloads/'+encodeURIComponent(id)+'/pause','POST');"
        "s.textContent='Pausing download '+id+'...';await load();}"
        "catch(x){showSystemError('Pause failed: '+x.message);}}"
        "async function stopDownload(id){const s=q('#actionStatus');"
        "try{await api('/api/v1/model-downloads/'+encodeURIComponent(id)+'/cancel','POST');"
        "s.textContent='Stopping download '+id+'...';await load();}"
        "catch(x){showSystemError('Stop failed: '+x.message);}}"
        // Removing an active transfer stops it first (mirroring Stop) and
        // waits for its own run() call to actually unwind -- the manager
        // rejects remove() while a job is still in flight -- before deleting
        // its record, then refreshes the list so the row disappears.
        "async function removeDownload(id){const s=q('#actionStatus');"
        "try{const before=(await api('/api/v1/model-downloads'))"
        ".downloads.find(x=>x.id===id);"
        "if(before&&before.state==='transferring'){"
        "await api('/api/v1/model-downloads/'+encodeURIComponent(id)+'/cancel','POST')"
        ".catch(()=>{});"
        "for(let attempt=0;attempt<20;attempt++){"
        "await new Promise(resolve=>setTimeout(resolve,150));"
        "const current=(await api('/api/v1/model-downloads'))"
        ".downloads.find(x=>x.id===id);"
        "if(!current||current.state!=='transferring')break;}}"
        "await api('/api/v1/model-downloads/'+encodeURIComponent(id)+'/remove','POST');"
        "if(activePolls[id]){clearInterval(activePolls[id]);delete activePolls[id];}"
        "s.textContent='Download '+id+' removed.';await load();}"
        "catch(x){showSystemError('Remove failed: '+x.message);}}"
        "async function createUser(e){e.preventDefault();const s=q('#actionStatus');"
        "try{await api('/api/v1/users','POST',{username:q('#newUserName').value,"
        "displayName:q('#newUserDisplay').value,role:q('#newUserRole').value,"
        "password:q('#newUserPassword').value});"
        "s.textContent='Local account created.';q('#newUser').reset();await load();}"
        "catch(x){showSystemError('Create account failed: '+x.message);}}"
        "function fill(sel,items,key,label){const e=q(sel);if(!e)return;e.replaceChildren();"
        "for(const x of items){const o=document.createElement('option');o.value=key(x);"
        "o.textContent=label(x);e.append(o);}}"
        "async function submit(e,path,body){e.preventDefault();const s=q('#actionStatus');"
        "try{const r=await api(path,'POST',body());s.textContent='Completed: '+JSON.stringify(r);"
        "await load();}catch(x){showSystemError('Action failed: '+x.message);}}"
        // Uploads one picked file as a project attachment (text only, per
        // AttachmentStore::add_text) and adds it to the pending list for the
        // message currently being composed; attachmentIds are read off that
        // list at send time in streamMessage() rather than a hidden field.
        "async function attachFile(file){const s=q('#actionStatus');"
        "try{const content=await file.text();"
        "const record=await api('/api/v1/attachments','POST',"
        "{projectId:currentProjectId(),filename:file.name,content});"
        "attachedFiles.push({id:record.id,filename:file.name});renderAttachChips();}"
        "catch(x){showSystemError('Attach \\''+file.name+'\\' failed: '+x.message);}}"
        "function renderAttachChips(){const box=q('#attachChips');if(!box)return;"
        "box.replaceChildren();"
        "attachedFiles.forEach((f,i)=>{const chip=document.createElement('span');"
        "chip.className='attachChip';"
        "const label=document.createElement('span');label.textContent=f.filename;"
        "const remove=document.createElement('button');remove.type='button';"
        "remove.textContent='\\u00d7';remove.title='Remove attachment';"
        "remove.addEventListener('click',()=>{attachedFiles.splice(i,1);"
        "renderAttachChips();});"
        "chip.append(label,remove);box.append(chip);});}"
        // Composer support commands: typed as the whole message (leading/
        // trailing whitespace ignored, case-insensitive) instead of being
        // sent to the model. /app is a full navigation -- the same URL the
        // "+ New chat" sidebar link already uses -- so it resets every piece
        // of client state (messages, attachments, pickers) the same way a
        // fresh page load does, rather than this list having to duplicate
        // that reset by hand.
        "const SLASH_COMMANDS={'/clear':()=>location.href='/app',"
        "'/new':()=>location.href='/app',"
        "'/help':()=>{q('#actionStatus').textContent="
        "'Commands: /clear or /new starts a fresh chat. /help shows this message.';}};"
        // A brand-new chat (no project/model form of its own any more) is
        // created lazily on the first message: the composer's own project
        // and model pickers supply what /api/v1/chats needs, and the URL
        // updates to that chat's own address without a full reload. Tokens
        // stream directly into a new assistant bubble instead of the status
        // line, and the user's own message is appended immediately so the
        // conversation reads like a real chat, not a debug log.
        "async function streamMessage(e){e.preventDefault();const s=q('#actionStatus');"
        "const box=q('#chatMessages');const content=q('#messageContent').value;"
        "if(!content.trim())return;"
        "const command=SLASH_COMMANDS[content.trim().toLowerCase()];"
        "if(command){q('#messageContent').value='';command();return;}"
        "let assistantEl=null;"
        "try{let chatId=q('#messageChat').value;"
        "if(!chatId){const projectId=q('#chatProject').value,modelId=q('#chatModel').value;"
        "if(!projectId||!modelId){"
        "s.textContent='Choose a project and a downloaded model first.';return;}"
        "const created=await api('/api/v1/chats','POST',{projectId,modelId});"
        "chatId=created.id;openedProjectId=projectId;"
        // Render the new chat into the sidebar immediately from the
        // create response itself, instead of waiting on load()'s full
        // GET /api/v1/chats round trip below to be the only thing that
        // ever shows it -- a caller that navigates away, or where that
        // refetch is ever skipped, still leaves the new chat visible.
        "renderChatList([{id:chatId,title:'New chat'},...lastChats]);"
        // Sync the hidden chat-id field before reloading so load() takes the
        // openChat() branch (hides the empty-state greeting, clears/repopulates
        // #chatMessages, disables the now-fixed project/model pickers) instead
        // of re-running the brand-new-chat branch, which would leave the
        // greeting visible at the same time as the message about to be
        // appended below.
        "q('#messageChat').value=chatId;"
        "history.pushState(null,'','/app/chat/'+encodeURIComponent(chatId));"
        "await load();"
        // The dropdown itself doesn't fire 'change' for the model a new
        // chat was created with (only for a later switch, via
        // changeChatModel() above), so nothing would otherwise say which
        // model this chat is actually using -- confirm it here instead,
        // reading the label back off the now-reloaded #chatModel picker.
        "const chosen=q('#chatModel');"
        "const chosenOpt=chosen?chosen.options[chosen.selectedIndex]:null;"
        "appendModelCard(q('#chatMessages'),"
        "'Using model '+(chosenOpt?chosenOpt.textContent:modelId));}"
        "q('#messageContent').value='';"
        "const attachmentIds=attachedFiles.map(x=>x.id);"
        "attachedFiles=[];renderAttachChips();"
        // Kept so the stream's 'complete' event below can back-fill this
        // query bubble's title with the prompt-token count.
        "const userEl=appendMessage(box,'user',content);"
        "box.scrollTop=box.scrollHeight;"
        "generation=new AbortController();"
        "assistantEl=appendMessage(box,'assistant','');"
        // Shown until the first token actually streams back -- reasoning
        // models in particular can take a real amount of time to produce
        // anything, and an empty bubble with no feedback reads as a hang.
        "assistantEl.querySelector('.msgBody').innerHTML='<span class=\"chatThinking\">"
        "<span class=\"chatThinkingSpinner\"></span>Thinking</span>';"
        "box.scrollTop=box.scrollHeight;"
        "const r=await fetch('/api/v1/chats/'+encodeURIComponent(chatId)+'/messages',"
        "{method:'POST',headers:{'Content-Type':'application/json','X-CSRF-Token':csrf},"
        "body:JSON.stringify({content,attachmentIds,"
        "effort:q('#modelEffort')?q('#modelEffort').value:'medium',"
        "thinking:q('#modelThinking')?q('#modelThinking').value:'off'}),"
        "signal:generation.signal});"
        "if(!r.ok)throw new Error(await r.text());"
        "const reader=r.body.getReader(),decoder=new TextDecoder();let pending='';"
        "for(;;){const x=await reader.read();if(x.done)break;"
        "pending+=decoder.decode(x.value,{stream:true});let n;"
        "while((n=pending.indexOf('\\n'))>=0){const line=pending.slice(0,n);"
        "pending=pending.slice(n+1);"
        "if(line){const event=JSON.parse(line);"
        "if(event.type==='token'){assistantEl.dataset.raw="
        "(assistantEl.dataset.raw||'')+event.content;"
        "const bodyEl=assistantEl.querySelector('.msgBody');"
        // patchMsgBody() diffs against the live DOM instead of rebuilding
        // it, so code blocks keep their scroll position (and their
        // scrollbars stop flickering) while tokens stream in.
        "patchMsgBody(bodyEl,renderMarkdown(assistantEl.dataset.raw));"
        "addCodeCopyButtons(bodyEl);"
        "box.scrollTop=box.scrollHeight;}"
        // The final stream event carries the runner's real token figures --
        // promptTokens (whole evaluated prompt, i.e. what this query cost)
        // and generatedTokens (the reply) -- shown in each bubble's title.
        "if(event.type==='complete'){"
        "setMsgTokens(userEl,'user',event.promptTokens||0);"
        "if(assistantEl)setMsgTokens(assistantEl,'assistant',"
        "event.generatedTokens||0);"
        "if(event.memorySaved)refreshMemories().catch(()=>{});}"
        "if(event.type==='error')throw new Error(event.error,"
        "{cause:event.detail});}}}}"
        "catch(x){const cancelled=x.name==='AbortError';"
        // The server used to fold every generation-time exception (model not
        // loaded, runner crash, degraded model producing an empty reply,
        // etc.) into the single opaque 'generation_failed' code with no way
        // to tell them apart. It now also sends the real exception text as
        // `detail` (carried here via Error's `cause`) -- show that when
        // present instead of guessing, and fall back to the old generic
        // wording only if an older server/response omitted it.
        "if(x.message==='generation_failed')x.message=x.cause||"
        "'The model failed to generate a reply. It may still be loading, "
        "downloading, or unable to run on this machine -- check Model "
        "inventory and try again.';"
        "if(cancelled)s.textContent='Cancelled.';"
        // A cancelled turn already left whatever partial reply the user saw
        // in assistantEl (the server persists it too, see streamed_text in
        // send_chat_message) -- overwriting it here would erase text the
        // user already read. A real failure, though, previously left an
        // empty bubble with the only explanation in the easy-to-miss status
        // line above the composer; showing it inside the reply's own bubble
        // (or as a new one, if the failure happened before any bubble
        // existed -- e.g. no project/model chosen yet) puts it where the
        // reply itself would have appeared.
        "if(!cancelled){const el=assistantEl||appendMessage(box,'assistant','');"
        "el.className='chatMsg chatMsg-assistant chatMsg-error';el.replaceChildren();"
        "const title=document.createElement('div');"
        "title.className='chatMsg-errorTitle';title.textContent='SYSTEM ERROR!';"
        "const body=document.createElement('div');body.textContent=x.message;"
        // Same reusable clipboard helper the reply/code copy buttons use --
        // dataset.raw is what addMessageCopyButton() would normally read,
        // set here since this card replaces the bubble's usual rendered
        // content instead of going through appendMessage()'s own path.
        "el.dataset.raw=x.message;"
        "el.append(title,body);addMessageCopyButton(el);"
        "box.scrollTop=box.scrollHeight;}}"
        "finally{generation=null;}}"
        // Fills the raw source fields from the Hugging Face helper inputs;
        // the operator still supplies model ID, RAM figures, SHA-256, and
        // license acceptance separately, so nothing about validation changes.
        "function applyHfSource(){const repo=q('#hfRepo').value.trim(),"
        "revision=q('#hfRevision').value.trim(),filename=q('#hfFilename').value.trim();"
        "if(!repo||!revision||!filename)return;"
        "q('#downloadSourceUrl').value='https://huggingface.co/'+repo+'/resolve/'+"
        "revision+'/'+filename;"
        "q('#downloadRevision').value=revision;q('#downloadFilename').value=filename;"
        "q('#actionStatus').textContent='Filled the source URL from Hugging Face fields. "
        "Still set model ID, RAM, SHA-256, and review the license before queueing.';}"
        "function applyGithubSource(){const repo=q('#ghRepo').value.trim(),"
        "tag=q('#ghTag').value.trim(),asset=q('#ghAsset').value.trim();"
        "if(!repo||!tag||!asset)return;"
        "q('#downloadSourceUrl').value='https://github.com/'+repo+'/releases/download/'+"
        "tag+'/'+asset;"
        "q('#downloadRevision').value=tag;q('#downloadFilename').value=asset;"
        "q('#actionStatus').textContent='Filled the source URL from GitHub release fields. "
        "Still set model ID, RAM, SHA-256, and review the license before queueing.';}"
        // ModelScope serves immutable-revision files at the same
        // /resolve/<revision>/<file> path shape as Hugging Face, so this
        // mirrors applyHfSource() rather than introducing a different shape.
        "function applyMsSource(){const repo=q('#msRepo').value.trim(),"
        "revision=q('#msRevision').value.trim(),filename=q('#msFilename').value.trim();"
        "if(!repo||!revision||!filename)return;"
        "q('#downloadSourceUrl').value='https://modelscope.cn/models/'+repo+'/resolve/'+"
        "revision+'/'+filename;"
        "q('#downloadRevision').value=revision;q('#downloadFilename').value=filename;"
        "q('#actionStatus').textContent='Filled the source URL from ModelScope fields. "
        "Still set model ID, RAM, SHA-256, and review the license before queueing.';}"
        // Confirms which model a chat is using inline, in the transcript
        // itself, rather than only in the easy-to-miss status line -- so
        // it's obvious which replies came from which model. Shared by the
        // very first model choice on a brand-new chat and by later
        // switches (changeChatModel() below) so the confirmation always
        // appears, not just on a subsequent change.
        "function appendModelCard(box,text){if(!box)return;"
        "const card=document.createElement('div');"
        "card.className='chatMsg chatMsg-modelChange';card.textContent=text;"
        "box.append(card);box.scrollTop=box.scrollHeight;}"
        // Picks the model a brand-new chat's picker starts on: whatever the
        // runner already has warm (Ready state) takes priority, since
        // reusing it avoids a fresh load/warm-up cycle; otherwise falls back
        // to the model the most recent chat used. Leaves the picker's
        // browser-default selection alone if neither candidate is in the
        // ready list (e.g. a fresh install with nothing warm and no chat
        // history yet).
        "async function selectDefaultChatModel(ready,chats){"
        "const picker=q('#chatModel');if(!picker||!ready.length)return;"
        "let defaultId=null;"
        "try{const st=await api('/api/v1/runner/status');"
        "if(st.warmState==='Ready'&&st.modelId&&"
        "ready.some(x=>x.id===st.modelId))defaultId=st.modelId;}catch(x){"
        "console.warn('selectDefaultChatModel: runner status check failed, "
        "falling back to last chat model',x);}"
        "if(!defaultId){"
        "const lastChat=chats.find(x=>x.modelId&&ready.some(r=>r.id===x.modelId));"
        "if(lastChat)defaultId=lastChat.modelId;}"
        "if(defaultId){picker.value=defaultId;refreshModelSettingsPanel();}}"
        // Polls GET /api/v1/runner/status (see server.cpp) so the chat page
        // can announce once, inline, the moment the backend actually
        // finishes loading -- not just the initial 'Using model X' choice.
        // warmPollBaselined swallows whatever warmState the very first poll
        // finds (a model that was already warm from an earlier chat isn't a
        // transition worth announcing); after that, lastAnnouncedWarmModelId
        // is compared against the polled modelId, so the card fires exactly
        // once per model becoming Ready and stays silent on every later poll
        // for that same model -- it only fires again once the id actually
        // changes (the model was switched away from and back, or a
        // different chat's model finished loading), matching "warmed" being
        // per-model state, not a one-time-ever flag.
        "let warmPollBaselined=false,lastAnnouncedWarmModelId=null;"
        "async function pollRunnerStatus(){try{const st=await api('/api/v1/runner/status');"
        "if(!warmPollBaselined){warmPollBaselined=true;"
        "if(st.warmState==='Ready')lastAnnouncedWarmModelId=st.modelId;return;}"
        "if(st.warmState==='Ready'&&st.modelId&&"
        "lastAnnouncedWarmModelId!==st.modelId){"
        "lastAnnouncedWarmModelId=st.modelId;"
        "const box=q('#chatMessages');if(!box)return;"
        "const select=q('#chatModel');"
        "const opt=select?[...select.options].find(o=>o.value===st.modelId):null;"
        "appendModelCard(box,'Model warmed: '+(opt?opt.textContent:st.modelId)+"
        "' is ready.');}}catch(x){console.warn('pollRunnerStatus: status "
        "poll failed, retrying next interval',x);}}"
        // Fires when the composer's model picker changes while a chat is
        // already open -- persists the new model against the chat so it's
        // still selected (and used) on the next message and after a reload.
        // A brand-new chat (no id yet) has nothing to persist against, so
        // it's left alone; the chosen model just gets used (and confirmed
        // via appendModelCard) when the chat is created on first send, see
        // streamMessage().
        "async function changeChatModel(){const chatId=q('#messageChat').value;"
        "if(!chatId)return;const s=q('#actionStatus');const select=q('#chatModel');"
        "const modelId=select.value;"
        "try{await api('/api/v1/chats/'+encodeURIComponent(chatId)+'/model','POST',"
        "{modelId});"
        "const opt=select.options[select.selectedIndex];"
        "appendModelCard(q('#chatMessages'),"
        "'Model switched to '+(opt?opt.textContent:modelId));}"
        "catch(x){showSystemError('Failed to switch model: '+x.message);}}"
        "function toggleSourceFields(){const type=q('#downloadSourceType').value;"
        "q('#hfFields').hidden=type!=='huggingface';"
        "q('#githubFields').hidden=type!=='github';"
        "q('#msFields').hidden=type!=='modelscope';}"
        "addEventListener('DOMContentLoaded',()=>{const f=q('#login');"
        "if(f){f.addEventListener('submit',login);initLogin();"
        "q('#setupForm').addEventListener('submit',setupLocalAdmin);}"
        // Every section below is its own page, so only one of these blocks'
        // elements exists on any given load -- each is independently guarded.
        "if(q('#sidebar')){load();"
        "setInterval(pollRunnerStatus,4000);"
        // Sidebar section collapse state persists across the full-page
        // navigations every sidebar click already does (see
        // sidebar_section() in web_ui.cpp): each <details> only overrides
        // its default-open state when localStorage actually has a saved
        // value, and a 'toggle' listener keeps that value current.
        "document.querySelectorAll('.sidebarSection[data-key]').forEach(d=>{"
        "const k='sidebarSection:'+d.dataset.key;const saved=localStorage.getItem(k);"
        "if(saved!==null)d.open=saved==='1';"
        "d.addEventListener('toggle',()=>localStorage.setItem(k,d.open?'1':'0'));});"
        // The sidebar's width and scroll position persist the same way the
        // section collapse state above does: every sidebar click is a
        // full-page navigation, so both are restored from localStorage on
        // each load. The width restore must run before the scroll restore
        // -- reflowing the sidebar after setting scrollTop would shift it.
        "const sb=q('#sidebar');"
        "const savedWidth=parseInt(localStorage.getItem('sidebarWidth'));"
        "if(savedWidth)sb.style.width=savedWidth+'px';"
        "const savedScroll=parseInt(localStorage.getItem('sidebarScroll'));"
        "if(savedScroll)sb.scrollTop=savedScroll;"
        "sb.addEventListener('scroll',()=>"
        "localStorage.setItem('sidebarScroll',Math.round(sb.scrollTop)));"
        // Pointer-capture drag on the gutter between sidebar and content:
        // width is clamped to keep both panes usable, and the final width
        // is saved once on release rather than on every move event.
        "const rz=q('#sidebarResizer');"
        "if(rz)rz.addEventListener('pointerdown',e=>{e.preventDefault();"
        "rz.setPointerCapture(e.pointerId);rz.classList.add('dragging');"
        "const move=ev=>{const w=Math.min(Math.max(ev.clientX,170),600);"
        "sb.style.width=w+'px';};"
        "const up=()=>{rz.classList.remove('dragging');"
        "rz.removeEventListener('pointermove',move);"
        "rz.removeEventListener('pointerup',up);"
        "localStorage.setItem('sidebarWidth',parseInt(sb.style.width));};"
        "rz.addEventListener('pointermove',move);"
        "rz.addEventListener('pointerup',up);});"
        "if(q('#newProject'))q('#newProject').addEventListener('submit',e=>submit(e,'/api/v1/projects',"
        "()=>({id:q('#projectId').value,displayName:q('#projectName').value})));"
        "if(q('#attachToggle'))q('#attachToggle').addEventListener('click',()=>{"
        "const s=q('#actionStatus');"
        "if(!currentProjectId()){s.textContent="
        "'Choose a project first -- attachments are stored against it.';return;}"
        "q('#attachFileInput').click();});"
        "if(q('#attachFileInput'))q('#attachFileInput').addEventListener('change',"
        "async e=>{const files=[...e.target.files];e.target.value='';"
        "for(const file of files)await attachFile(file);});"
        "if(q('#newMessage'))q('#newMessage').addEventListener('submit',streamMessage);"
        "if(q('#chatModel'))q('#chatModel').addEventListener('change',"
        "()=>{changeChatModel();openModelSettingsPanel();});"
        "if(q('#modelSettingsToggle'))q('#modelSettingsToggle')"
        ".addEventListener('click',openModelSettingsPanel);"
        "if(q('#modelSettingsClose'))q('#modelSettingsClose')"
        ".addEventListener('click',()=>{q('#modelSettingsPanel').hidden=true;});"
        "if(q('#modelEffort'))q('#modelEffort')"
        ".addEventListener('change',saveCurrentModelSettings);"
        "if(q('#modelThinking'))q('#modelThinking')"
        ".addEventListener('change',saveCurrentModelSettings);"
        "if(q('#newMemory'))q('#newMemory').addEventListener('submit',addMemory);"
        // Enter sends the message, mirroring every mainstream chat client;
        // Shift+Enter still inserts a newline (the textarea's own default),
        // so multi-line prompts remain possible.
        "if(q('#messageContent'))q('#messageContent').addEventListener('keydown',"
        "e=>{if(e.key==='Enter'&&!e.shiftKey){e.preventDefault();"
        "q('#newMessage').requestSubmit();}});"
        "if(q('#cancelMessage'))q('#cancelMessage').addEventListener('click',"
        "()=>{if(generation)generation.abort();});"
        // Escape stops an in-flight reply immediately, mirroring the cancel
        // button -- listens on the document (not just the textarea) so it
        // works even while focus is elsewhere on the chat page, but only
        // while a generation is actually running so it doesn't swallow
        // Escape for anything else (closing a picker, blurring a field).
        "if(q('#newMessage'))document.addEventListener('keydown',"
        "e=>{if(e.key==='Escape'&&generation){generation.abort();}});"
        "if(q('#downloadTier')){q('#downloadTier').addEventListener('change',refreshPresets);"
        "q('#downloadPreset').addEventListener('change',applyPreset);"
        "refreshPresets();q('#applyPreset').addEventListener('click',applyPreset);"
        "q('#newDownload').addEventListener('submit',queueDownload);"
        "q('#downloadSourceType').addEventListener('change',"
        "()=>{toggleSourceFields();refreshPresets();});"
        "toggleSourceFields();"
        "q('#applyHfSource').addEventListener('click',applyHfSource);"
        "q('#applyGithubSource').addEventListener('click',applyGithubSource);"
        "q('#applyMsSource').addEventListener('click',applyMsSource);}"
        "if(q('#newUser'))q('#newUser').addEventListener('submit',createUser);"
        "if(q('#systemConfigForm'))q('#systemConfigForm').addEventListener("
        "'submit',submitSystemConfig);"
        "if(q('#newMlProject'))q('#newMlProject').addEventListener('submit',"
        "e=>submit(e,'/api/v1/ml/projects',()=>({name:q('#mlProjectName').value,"
        "description:q('#mlProjectDescription').value,"
        "objective:q('#mlProjectObjective').value,"
        "subjectDomain:q('#mlProjectSubjectDomain').value,"
        "modelTask:q('#mlProjectModelTask').value})));"
        "if(q('#newMlModel'))q('#newMlModel').addEventListener('submit',"
        "e=>submit(e,'/api/v1/ml/models',()=>({name:q('#mlModelName').value,"
        "displayName:q('#mlModelDisplayName').value,"
        "version:q('#mlModelVersion').value,"
        "family:q('#mlModelFamily').value,"
        "task:q('#mlModelTask').value,"
        "format:q('#mlModelFormat').value,"
        "source:q('#mlModelSource').value,"
        "license:q('#mlModelLicense').value})));"
        "if(q('#newMlDataset'))q('#newMlDataset').addEventListener('submit',"
        "e=>submit(e,'/api/v1/ml/datasets',()=>({name:q('#mlDatasetName').value,"
        "description:q('#mlDatasetDescription').value,"
        "subjectArea:q('#mlDatasetSubjectArea').value,"
        "source:q('#mlDatasetSource').value,"
        "license:q('#mlDatasetLicense').value,"
        "dataFormat:q('#mlDatasetFormat').value})));"
        // Phase 56: dataset content upload -- posts the CSV to the real
        // ingestion endpoint and shows the parsed profile it returns.
        "if(q('#newMlDatasetContent'))q('#newMlDatasetContent')"
        ".addEventListener('submit',async e=>{e.preventDefault();"
        "const out=q('#mlDatasetContentResult');"
        "const file=q('#mlDatasetContentFile').files[0];"
        "if(!file){showSystemError('Choose a CSV file first.');return;}"
        "if(file.size>8*1024*1024){showSystemError('Dataset file exceeds the 8 MiB limit.');return;}"
        "out.textContent='Reading, uploading, and validating '+file.name+'...';"
        "try{const csv=new TextDecoder('utf-8',{fatal:true}).decode("
        "await file.arrayBuffer());const r=await api('/api/v1/ml/datasets/'+"
        "encodeURIComponent(q('#mlDatasetContentId').value.trim())+'/content',"
        "'POST',{csv,"
        "targetColumn:q('#mlDatasetContentTarget').value.trim()});"
        "out.textContent='Stored '+r.rows+' rows: '+r.featureColumns.length+"
        "' feature column(s) ['+r.featureColumns.join(', ')+'], target \\''+"
        "r.targetColumn+'\\' ('+r.task+"
        "(r.task==='classification'?', classes: '+r.classes.join(', '):'')+"
        "').';await load();}"
        "catch(x){out.textContent='';"
        "showSystemError('Upload dataset content failed: '+x.message);}});"
        // Phase 56: live prediction form -- parses the feature JSON locally
        // for a clear error, then calls the real prediction endpoint.
        "if(q('#mlPredictForm'))q('#mlPredictForm')"
        ".addEventListener('submit',async e=>{e.preventDefault();"
        "const out=q('#mlPredictResult');"
        "let features;"
        "try{features=JSON.parse(q('#mlPredictFeatures').value);}"
        "catch(x){showSystemError('Feature values must be a valid JSON "
        "object.');return;}"
        "out.textContent='Predicting...';"
        "try{const r=await api('/api/v1/ml/models/'+"
        "encodeURIComponent(q('#mlPredictModelId').value.trim())+'/predict',"
        "'POST',{features});"
        "if(r.task==='classification'){"
        "out.textContent='Predicted class: '+r.label+' ('+r.classes.map("
        "(c,i)=>c+' '+(100*r.probabilities[i]).toFixed(1)+'%').join(', ')+')';}"
        "else{out.textContent='Predicted value: '+"
        "Number(r.value).toPrecision(6);}}"
        "catch(x){out.textContent='';"
        "showSystemError('Prediction failed: '+x.message);}});"
        "if(q('#newMlSubject'))q('#newMlSubject').addEventListener('submit',"
        "e=>submit(e,'/api/v1/ml/subjects',()=>({name:q('#mlSubjectName').value,"
        "description:q('#mlSubjectDescription').value,"
        "scope:q('#mlSubjectScope').value,"
        "targetAudience:q('#mlSubjectTargetAudience').value})));"
        "if(q('#newMlKnowledgeDocument'))q('#newMlKnowledgeDocument')"
        ".addEventListener('submit',async e=>{e.preventDefault();"
        "const file=q('#mlKnowledgeFile').files[0];"
        "if(!file){showSystemError('Choose a knowledge file first.');return;}"
        "const out=q('#mlKnowledgeUploadResult');"
        "out.textContent='Reading and indexing '+file.name+'...';"
        "try{const lower=file.name.toLowerCase();"
        "const isParquet=lower.endsWith('.parquet');"
        "let mediaType=file.type;let content;"
        "if(isParquet){mediaType='application/vnd.apache.parquet';"
        "const bytes=new Uint8Array(await file.arrayBuffer());"
        "let binary='';const chunkSize=0x8000;"
        "for(let i=0;i<bytes.length;i+=chunkSize){"
        "binary+=String.fromCharCode.apply(null,bytes.subarray(i,i+chunkSize));}"
        "content=btoa(binary);}"
        "else{content=new TextDecoder('utf-8',{fatal:true}).decode("
        "await file.arrayBuffer());"
        "if(!mediaType){mediaType=lower.endsWith('.md')?"
        "'text/markdown':lower.endsWith('.csv')?'text/csv':"
        "lower.endsWith('.json')?'application/json':"
        "lower.endsWith('.jsonl')?'application/x-ndjson':'text/plain';}}"
        "const r=await api('/api/v1/ml/knowledge-documents','POST',{"
        "subjectId:q('#mlKnowledgeSubjectId').value,"
        "vectorStoreId:q('#mlKnowledgeVectorStoreId').value,"
        "fileName:file.name,mediaType,content});"
        "out.textContent='Indexed '+r.fileName+': '+r.chunkCount+' chunk(s), SHA-256 '+r.sha256+'.';"
        "await load();}catch(x){out.textContent='';"
        "showSystemError('Knowledge ingestion failed: '+x.message);}});"
        "if(q('#newMlLabelTask'))q('#newMlLabelTask').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/label-tasks',"
        "()=>({datasetId:q('#mlLabelTaskDatasetId').value,"
        "name:q('#mlLabelTaskName').value,"
        "description:q('#mlLabelTaskDescription').value,"
        "labelMode:q('#mlLabelTaskLabelMode').value,"
        "assigneeId:q('#mlLabelTaskAssigneeId').value})));"
        "if(q('#newMlPrepJob'))q('#newMlPrepJob').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/prep-jobs',"
        "()=>({datasetId:q('#mlPrepJobDatasetId').value,"
        "name:q('#mlPrepJobName').value,"
        "description:q('#mlPrepJobDescription').value,"
        "operation:q('#mlPrepJobOperation').value})));"
        "if(q('#newMlTrainingJob'))q('#newMlTrainingJob').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/training-jobs',"
        "()=>({projectId:q('#mlTrainingJobProjectId').value,"
        "modelId:q('#mlTrainingJobModelId').value,"
        "datasetId:q('#mlTrainingJobDatasetId').value,"
        "name:q('#mlTrainingJobName').value,"
        "description:q('#mlTrainingJobDescription').value,"
        "trainingType:q('#mlTrainingJobTrainingType').value})));"
        "if(q('#newMlEvaluationRun'))q('#newMlEvaluationRun').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/evaluation-runs',"
        "()=>({modelId:q('#mlEvaluationRunModelId').value,"
        "datasetId:q('#mlEvaluationRunDatasetId').value,"
        "name:q('#mlEvaluationRunName').value,"
        "description:q('#mlEvaluationRunDescription').value,"
        "category:q('#mlEvaluationRunCategory').value})));"
        "if(q('#newMlExperiment'))q('#newMlExperiment').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/experiments',"
        "()=>({projectId:q('#mlExperimentProjectId').value,"
        "modelId:q('#mlExperimentModelId').value,"
        "datasetId:q('#mlExperimentDatasetId').value,"
        "name:q('#mlExperimentName').value,"
        "description:q('#mlExperimentDescription').value})));"
        "if(q('#newMlFineTuningJob'))q('#newMlFineTuningJob').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/fine-tuning-jobs',"
        "()=>({modelId:q('#mlFineTuningJobModelId').value,"
        "datasetId:q('#mlFineTuningJobDatasetId').value,"
        "projectId:q('#mlFineTuningJobProjectId').value,"
        "name:q('#mlFineTuningJobName').value,"
        "description:q('#mlFineTuningJobDescription').value,"
        "method:q('#mlFineTuningJobMethod').value})));"
        "if(q('#newMlModelBuilderConfig'))"
        "q('#newMlModelBuilderConfig').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/model-builder-configs',"
        "()=>({sourceType:q('#mlModelBuilderConfigSourceType').value,"
        "baseModelId:q('#mlModelBuilderConfigBaseModelId').value,"
        "projectId:q('#mlModelBuilderConfigProjectId').value,"
        "name:q('#mlModelBuilderConfigName').value,"
        "description:q('#mlModelBuilderConfigDescription').value})));"
        // Section 9's basic/advanced configuration modes: the mode select
        // toggles the advanced field group's visibility, and Save always
        // posts every field (basic mode keeps the advanced values the
        // Configure button loaded, so hiding them loses nothing).
        "if(q('#configureMlModelBuilderConfig')){"
        "q('#mlMbcMode').addEventListener('change',()=>{"
        "q('#mlMbcAdvanced').style.display="
        "q('#mlMbcMode').value==='advanced'?'':'none';});"
        "q('#configureMlModelBuilderConfig').addEventListener('submit',e=>{"
        "if(!mlModelBuilderConfigureTarget){e.preventDefault();"
        "showSystemError('Press Configure on a configuration in the list "
        "first to choose which one to edit.');return;}"
        "submit(e,'/api/v1/ml/model-builder-configs/'+"
        "encodeURIComponent(mlModelBuilderConfigureTarget)+'/configure',"
        "()=>({configurationMode:q('#mlMbcMode').value,"
        "architecture:q('#mlMbcArchitecture').value,"
        "layerConfiguration:q('#mlMbcLayerConfiguration').value,"
        "hiddenDimensions:Number(q('#mlMbcHiddenDimensions').value)||0,"
        "attentionConfiguration:q('#mlMbcAttentionConfiguration').value,"
        "vocabularyTokenizer:q('#mlMbcVocabularyTokenizer').value,"
        "sequenceLength:Number(q('#mlMbcSequenceLength').value)||0,"
        "activationFunctions:q('#mlMbcActivationFunctions').value,"
        "dropout:Number(q('#mlMbcDropout').value)||0,"
        "initialisationStrategy:q('#mlMbcInitialisationStrategy').value,"
        "lossFunction:q('#mlMbcLossFunction').value,"
        "optimiser:q('#mlMbcOptimiser').value,"
        "learningRateScheduler:q('#mlMbcLearningRateScheduler').value,"
        "batchSize:Number(q('#mlMbcBatchSize').value)||0,"
        "epochCount:Number(q('#mlMbcEpochCount').value)||0,"
        "gradientAccumulation:"
        "Number(q('#mlMbcGradientAccumulation').value)||0,"
        "gradientClipping:Number(q('#mlMbcGradientClipping').value)||0,"
        "mixedPrecision:q('#mlMbcMixedPrecision').checked,"
        "checkpointFrequency:"
        "Number(q('#mlMbcCheckpointFrequency').value)||0,"
        "validationFrequency:"
        "Number(q('#mlMbcValidationFrequency').value)||0,"
        "earlyStopping:q('#mlMbcEarlyStopping').checked,"
        "randomSeed:Number(q('#mlMbcRandomSeed').value)||0,"
        "reproducibilitySettings:"
        "q('#mlMbcReproducibilitySettings').value,"
        "distributedTrainingSettings:"
        "q('#mlMbcDistributedTrainingSettings').value}));});}"
        "if(q('#newMlInstructionExample'))"
        "q('#newMlInstructionExample').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/instruction-examples',"
        "()=>({datasetId:q('#mlInstructionExampleDatasetId').value,"
        "name:q('#mlInstructionExampleName').value,"
        "description:q('#mlInstructionExampleDescription').value,"
        "subjectClassification:"
        "q('#mlInstructionExampleSubjectClassification').value})));"
        "if(q('#newMlSyntheticRecord'))"
        "q('#newMlSyntheticRecord').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/synthetic-records',"
        "()=>({datasetId:q('#mlSyntheticRecordDatasetId').value,"
        "name:q('#mlSyntheticRecordName').value,"
        "description:q('#mlSyntheticRecordDescription').value,"
        "generationTechnique:"
        "q('#mlSyntheticRecordGenerationTechnique').value})));"
        "if(q('#newMlVectorStore'))"
        "q('#newMlVectorStore').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/vector-stores',"
        "()=>({name:q('#mlVectorStoreName').value,"
        "description:q('#mlVectorStoreDescription').value,"
        "embeddingModel:q('#mlVectorStoreEmbeddingModel').value,"
        "distanceMetric:q('#mlVectorStoreDistanceMetric').value})));"
        "if(q('#newMlRagConfig'))"
        "q('#newMlRagConfig').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/rag-configs',"
        "()=>({name:q('#mlRagConfigName').value,"
        "description:q('#mlRagConfigDescription').value,"
        "searchStrategy:q('#mlRagConfigSearchStrategy').value,"
        "vectorStoreId:q('#mlRagConfigVectorStoreId').value})));"
        "if(q('#mlRagQueryForm'))q('#mlRagQueryForm').addEventListener("
        "'submit',async e=>{e.preventDefault();const out=q('#mlRagQueryResult');"
        "out.textContent='Retrieving indexed evidence...';try{const r=await api("
        "'/api/v1/ml/rag-configs/'+encodeURIComponent("
        "q('#mlRagQueryConfigId').value)+'/query','POST',{"
        "query:q('#mlRagQueryText').value,topK:Number(q('#mlRagQueryTopK').value)});"
        "const result=r.result;out.textContent='Retrieved '+result.retrievedCount+"
        "' chunk(s) with '+result.searchStrategy+' search.\\n\\n'+"
        "result.chunks.map((x,i)=>(i+1)+'. '+x.citation+' score '+"
        "Number(x.score).toFixed(4)+'\\n'+x.text).join('\\n\\n')+"
        "'\\n\\nContext package:\\n'+result.context;}catch(x){out.textContent='';"
        "showSystemError('RAG retrieval failed: '+x.message);}});"
        "if(q('#newMlSubjectExam'))"
        "q('#newMlSubjectExam').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/subject-exams',"
        "()=>({subjectId:q('#mlSubjectExamSubjectId').value,"
        "name:q('#mlSubjectExamName').value,"
        "description:q('#mlSubjectExamDescription').value,"
        "questionFormat:q('#mlSubjectExamQuestionFormat').value})));"
        "if(q('#newMlHyperparameterSearch'))"
        "q('#newMlHyperparameterSearch').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/hyperparameter-searches',"
        "()=>({trainingJobId:q('#mlHyperparameterSearchTrainingJobId').value,"
        "name:q('#mlHyperparameterSearchName').value,"
        "description:q('#mlHyperparameterSearchDescription').value,"
        "strategy:q('#mlHyperparameterSearchStrategy').value})));"
        "if(q('#newMlModelOptimization'))"
        "q('#newMlModelOptimization').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/model-optimizations',"
        "()=>({modelId:q('#mlModelOptimizationModelId').value,"
        "name:q('#mlModelOptimizationName').value,"
        "description:q('#mlModelOptimizationDescription').value,"
        "operation:q('#mlModelOptimizationOperation').value})));"
        "if(q('#newMlCheckpoint'))"
        "q('#newMlCheckpoint').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/checkpoints',"
        "()=>({trainingJobId:q('#mlCheckpointTrainingJobId').value,"
        "name:q('#mlCheckpointName').value,"
        "description:q('#mlCheckpointDescription').value,"
        "captureReason:q('#mlCheckpointCaptureReason').value})));"
        "if(q('#newMlDeployment'))"
        "q('#newMlDeployment').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/deployments',"
        "()=>({modelId:q('#mlDeploymentModelId').value,"
        "name:q('#mlDeploymentName').value,"
        "description:q('#mlDeploymentDescription').value,"
        "environment:q('#mlDeploymentEnvironment').value,"
        "strategy:q('#mlDeploymentStrategy').value})));"
        "if(q('#newMlModelComparison'))"
        "q('#newMlModelComparison').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/model-comparisons',"
        "()=>({baselineModelId:q('#mlModelComparisonBaselineModelId').value,"
        "candidateModelId:q('#mlModelComparisonCandidateModelId').value,"
        "datasetId:q('#mlModelComparisonDatasetId').value,"
        "name:q('#mlModelComparisonName').value,"
        "description:q('#mlModelComparisonDescription').value})));"
        "if(q('#newMlInferenceEndpoint'))"
        "q('#newMlInferenceEndpoint').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/inference-endpoints',"
        "()=>({modelId:q('#mlEndpointModelId').value,"
        "name:q('#mlEndpointName').value,"
        "runtime:q('#mlEndpointRuntime').value,"
        "host:q('#mlEndpointHost').value,"
        "port:Number(q('#mlEndpointPort').value)||0,"
        "protocol:q('#mlEndpointProtocol').value,"
        "authenticationMethod:q('#mlEndpointAuthenticationMethod').value,"
        "rateLimitPerMinute:Number(q('#mlEndpointRateLimit').value)||0})));"
        "if(q('#newMlComputeNode'))"
        "q('#newMlComputeNode').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/compute-nodes',"
        "()=>({name:q('#mlComputeNodeName').value,"
        "address:q('#mlComputeNodeAddress').value,"
        "operatingSystem:q('#mlComputeNodeOperatingSystem').value,"
        "cpuDescription:q('#mlComputeNodeCpuDescription').value,"
        "gpuDescription:q('#mlComputeNodeGpuDescription').value,"
        "memoryMib:Number(q('#mlComputeNodeMemoryMib').value)||0,"
        "isLocal:q('#mlComputeNodeIsLocal').checked})));"
        "if(q('#newMlAutomationPipeline'))"
        "q('#newMlAutomationPipeline').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/automation-pipelines',"
        "()=>({name:q('#mlPipelineName').value,"
        "projectId:q('#mlPipelineProjectId').value,"
        "description:q('#mlPipelineDescription').value,"
        "stages:q('#mlPipelineStages').value,"
        "datasetId:q('#mlPipelineDatasetId').value,"
        "modelId:q('#mlPipelineModelId').value})));"
        "if(q('#newMlSafetyPolicy'))"
        "q('#newMlSafetyPolicy').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/safety-policies',"
        "()=>({name:q('#mlSafetyPolicyName').value,"
        "scope:q('#mlSafetyPolicyScope').value,"
        "restrictedDataCategories:"
        "q('#mlSafetyPolicyRestrictedDataCategories').value})));"
        "if(q('#newMlModelCard'))"
        "q('#newMlModelCard').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/model-cards',"
        "()=>({modelId:q('#mlModelCardModelId').value,"
        "purpose:q('#mlModelCardPurpose').value,"
        "intendedUse:q('#mlModelCardIntendedUse').value,"
        "prohibitedUse:q('#mlModelCardProhibitedUse').value,"
        "trainingDataReference:"
        "q('#mlModelCardTrainingDataReference').value,"
        "evaluationResults:q('#mlModelCardEvaluationResults').value,"
        "knownLimitations:q('#mlModelCardKnownLimitations').value,"
        "license:q('#mlModelCardLicense').value})));}});";
}

// Presents the native OS account sign-in form without embedding credentials.
std::string login_page() {
    return html_response(
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>MasterAI Login</title><style>" DARK_THEME_CSS
        "body{max-width:34rem;margin:8vh auto}"
        "h1{font-size:2.1rem}p{color:var(--muted)}</style></head>"
        "<body><h1>MasterAI</h1>"
        // Shown only before the first administrator exists (toggled by JS
        // after reading /health/ready's setupRequired flag). Only present
        // when the server was configured with allowLocalPasswordAccounts;
        // otherwise the server rejects POST /api/v1/setup/local anyway.
        "<section id=\"setupSection\" hidden><h2>First-time setup</h2>"
        "<p>Create the local administrator account. The one-time setup token "
        "was printed to the server's log/console when it started.</p>"
        "<form id=\"setupForm\"><label for=\"setupToken\">Setup token</label>"
        "<input id=\"setupToken\" required>"
        "<label for=\"setupUsername\">Username</label>"
        "<input id=\"setupUsername\" required pattern=\"[A-Za-z0-9_.-]+\">"
        "<label for=\"setupDisplay\">Display name</label>"
        "<input id=\"setupDisplay\" required>"
        "<label for=\"setupPassword\">Password (minimum 8 characters)</label>"
        "<input id=\"setupPassword\" type=\"password\" minlength=\"8\" required>"
        "<button title=\"Create administrator\">" ICON_PLUS_SVG " Create administrator</button></form></section>"
        "<section id=\"loginSection\"><p>Sign in with your MasterAI or "
        "operating-system account.</p><form id=\"login\"><label for=\"username\">"
        "User name</label><input id=\"username\" autocomplete=\"username\" required>"
        "<label for=\"password\">Password</label><input id=\"password\" type=\"password\" "
        "autocomplete=\"current-password\" required><button>Sign in</button></form>"
        "</section>"
        "<p id=\"status\" role=\"status\"></p><script src=\"/assets/app.js\"></script>"
        "</body></html>");
}

// Renders one sidebar link. href is a real URL (a normal page navigation),
// never a client-side hash -- "active" is decided server-side from the
// section actually being rendered on this response.
std::string nav_link(const std::string& href, const std::string& label,
                     bool active) {
    return "<a class=\"navButton" + std::string(active ? " active" : "") +
           "\" href=\"" + href + "\">" + label + "</a>";
}

// Wraps one titled group of sidebar links (Workspace, Settings, Machine
// Learning, Admin, ...) in a collapsible <details> instead of a plain <h3>,
// so a caller with many reachable sections can shrink the sidebar down to
// just the section titles that matter to them. `key` is a short stable
// identifier persisted client-side (see the sidebar-collapse script in
// application_page) so the collapsed/expanded state survives the full-page
// navigation every sidebar click already does -- <details> alone resets to
// `open` on every fresh page load otherwise. Defaults to expanded so
// nothing already reachable becomes newly hidden by this change.
std::string sidebar_section(const std::string& key, const std::string& title,
                            const std::string& body) {
    return "<details open class=\"sidebarSection\" data-key=\"" + key +
          "\"><summary>" + title + "</summary><div>" + body +
          "</div></details>";
}

// Presents one section (chat, projects, models/*, admin/*) of the workspace
// as its own full document at its own URL (see server.cpp's /app/* routes),
// with a sidebar of real links to every other section the caller's role can
// reach. Switching sections is therefore a normal full-page navigation, not
// a client-side panel swap -- there is no hash router involved.
std::string application_page(const UserRecord& user, const std::string& section,
                             const std::string& chat_id) {
    // The Admin nav entries are omitted entirely for non-administrators, and
    // the Settings entries for plain viewers. This is presentation only --
    // every admin-only or settings-only route (users.manage,
    // downloads.manage) still enforces its own permission check per request
    // regardless of what this page renders or which URL is typed directly.
    const bool is_administrator = user.role == UserRole::administrator;
    const bool is_developer = user.role == UserRole::developer;
    const bool can_manage_settings = is_administrator || is_developer;
    const bool can_write_chat = role_allows(user.role, "chats.write");
    const std::string role_attr =
        is_administrator ? "administrator" : (is_developer ? "developer" : "viewer");

    std::string body;
    if (section == "chat") {
        // Mirrors a familiar chat layout: a centered empty-state greeting
        // when nothing is selected yet, growing message history once a
        // conversation exists, and a single pill-shaped composer pinned to
        // the bottom that carries the model picker itself -- there is no
        // separate "Start chat" form to fill in first. Project and model
        // are only choosable before the first message; once the chat
        // exists both are fixed to it (see openChat() disabling them) since
        // the model is set at chat-creation time server-side.
        const bool is_new_chat = chat_id.empty();
        body =
            "<div id=\"chatShell\">"
            "<div id=\"chatTopBar\"><label>Project"
            "<select id=\"chatProject\"></select></label></div>"
            "<div id=\"chatEmpty\"" + std::string(is_new_chat ? "" : " hidden") +
            "><h1>What's on the agenda today?</h1></div>"
            "<div id=\"chatMessages\"></div>"
            "<div id=\"attachChips\" class=\"attachChips\"></div>"
            "<form id=\"newMessage\" class=\"composer\">"
            "<input type=\"hidden\" id=\"messageChat\" value=\"" +
            html_escape(chat_id) +
            "\">"
            // Text files only, matching AttachmentStore::add_text -- there is
            // no server-side path for binary content, so the picker doesn't
            // offer to select any.
            "<input type=\"file\" id=\"attachFileInput\" class=\"visuallyHidden\" "
            "multiple accept=\".txt,.md,.markdown,.c,.h,.cc,.cpp,.hpp,.hh,.py,.js,"
            ".jsx,.ts,.tsx,.json,.yaml,.yml,.toml,.ini,.cfg,.conf,.csv,.log,.rs,.go,"
            ".java,.rb,.sh,.sql,text/plain\">"
            "<button type=\"button\" id=\"attachToggle\" class=\"composerIconBtn\" "
            "title=\"Attach files\">+</button>"
            "<textarea id=\"messageContent\" class=\"composerInput\" rows=\"1\" "
            "placeholder=\"Message MasterAI...\" required></textarea>"
            "<select id=\"chatModel\" class=\"composerModelPicker\"></select>"
            // Effort/thinking-level panel: opens automatically whenever the
            // model picker changes (see openModelSettingsPanel()) and can be
            // reopened afterward with this gear button. Its settings persist
            // per model id in localStorage (see loadModelSettings()/
            // saveModelSettings()) and are read back the moment that same
            // model is chosen again, in this chat or a future one.
            "<button type=\"button\" id=\"modelSettingsToggle\" "
            "class=\"composerIconBtn\" title=\"Model settings (effort / "
            "thinking)\" aria-label=\"Model settings\">&#9881;</button>"
            "<div id=\"modelSettingsPanel\" class=\"modelSettingsPanel\" hidden>"
            "<div class=\"modelSettingsPanelHeader\">Model settings"
            "<button type=\"button\" id=\"modelSettingsClose\" "
            "class=\"modelSettingsClose\" aria-label=\"Close model settings\">"
            "&#215;</button></div>"
            "<label>Effort<select id=\"modelEffort\">"
            "<option value=\"low\">Low</option>"
            "<option value=\"medium\" selected>Medium</option>"
            "<option value=\"high\">High</option></select></label>"
            "<label>Thinking<select id=\"modelThinking\">"
            "<option value=\"off\" selected>Off</option>"
            "<option value=\"on\">On</option></select></label>"
            "<p id=\"modelSettingsNote\" class=\"modelSettingsNote\"></p>"
            "</div>"
            "<button type=\"submit\" class=\"composerSendBtn\" title=\"Send\">"
            "&#8593;</button>"
            "<button id=\"cancelMessage\" type=\"button\" class=\"composerIconBtn\" "
            "title=\"Cancel\">&times;</button>"
            "</form></div>";
    } else if (section == "projects") {
        body =
            "<section id=\"panel-projects\" class=\"panel\"><div>"
            "<h2>Projects</h2>"
            "<form id=\"newProject\"><label>Project ID<input id=\"projectId\" "
            "required pattern=\"[A-Za-z0-9_.-]+\"></label><label>Name"
            "<input id=\"projectName\" required></label>"
            "<button title=\"Create project\">" ICON_PLUS_SVG " Create project</button></form>"
            "<div id=\"projectsList\">Loading...</div></div></section>";
    } else if (section == "models-inventory") {
        body =
            "<section id=\"panel-models-inventory\" class=\"panel\"><div>"
            "<h2>Model inventory</h2><div id=\"modelsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "models-download") {
        body =
            "<section id=\"panel-models-download\" class=\"panel\"><div>"
            "<h2>Download a model</h2>"
            // Source selector: assembles a source URL/revision for the
            // operator instead of requiring one be hand-typed, without
            // changing what's actually submitted or validated -- the
            // assembled fields still go through the same required raw
            // sourceUrl/revision/sha256 fields and the same server-side
            // revision-substring and post-download SHA-256 checks as before.
            "<label>Download source<select id=\"downloadSourceType\">"
            "<option value=\"huggingface\">Hugging Face</option>"
            "<option value=\"github\">GitHub Releases</option>"
            "<option value=\"modelscope\">ModelScope</option>"
            "<option value=\"custom\">Custom URL</option>"
            "</select></label>"
            "<div id=\"hfFields\">"
            "<label>Org/repo (e.g. Qwen/Qwen2.5-Coder-7B-Instruct-GGUF)"
            "<input id=\"hfRepo\"></label>"
            "<label>Commit/revision<input id=\"hfRevision\"></label>"
            "<label>Filename<input id=\"hfFilename\"></label>"
            "<button type=\"button\" id=\"applyHfSource\">Fill source from "
            "these fields</button></div>"
            "<div id=\"githubFields\" hidden>"
            "<label>Owner/repo<input id=\"ghRepo\"></label>"
            "<label>Release tag<input id=\"ghTag\"></label>"
            "<label>Asset filename<input id=\"ghAsset\"></label>"
            "<button type=\"button\" id=\"applyGithubSource\">Fill source from "
            "these fields</button></div>"
            "<div id=\"msFields\" hidden>"
            "<label>Org/repo (e.g. second-state/StarCoder2-3B-GGUF)"
            "<input id=\"msRepo\"></label>"
            "<label>Commit/revision<input id=\"msRevision\"></label>"
            "<label>Filename<input id=\"msFilename\"></label>"
            "<button type=\"button\" id=\"applyMsSource\">Fill source from "
            "these fields</button></div>"
            // Suggestion picker: fills the raw form below from a verified,
            // hard-coded preset so the operator never has to hand-type a
            // commit hash or a 64-character digest to get a working model.
            "<h3>Or pick a curated suggestion</h3>"
            "<label>Filter suggestions by available RAM<select id=\"downloadTier\">"
            "<option value=\"test\">Tiny test model (~1 MB) - any hardware</option>"
            "<option value=\"1\">1 GB</option>"
            "<option value=\"2\">2 GB</option>"
            "<option value=\"3\">3 GB</option>"
            "<option value=\"4\">4 GB</option>"
            "<option value=\"5\">5 GB</option>"
            "<option value=\"6\">6 GB</option>"
            "<option value=\"7\">7 GB</option>"
            "<option value=\"8\">8 GB</option><option value=\"16\">16 GB</option>"
            "<option value=\"24\">24 GB</option><option value=\"32\">32 GB</option>"
            "<option value=\"64\">64 GB or more</option></select></label>"
            "<label>Suggested model<select id=\"downloadPreset\"></select></label>"
            "<button type=\"button\" id=\"applyPreset\">Reset form to suggestion"
            "</button>"
            "<form id=\"newDownload\">"
            "<label>Category<select id=\"downloadCategory\">"
            "<option value=\"general-programming\">general-programming</option>"
            "<option value=\"code-completion\">code-completion</option>"
            "<option value=\"code-review\">code-review</option>"
            "<option value=\"debugging\">debugging</option>"
            "<option value=\"documentation\">documentation</option>"
            "<option value=\"embeddings-code-search\">embeddings-code-search"
            "</option><option value=\"conversation\">conversation</option>"
            "</select></label>"
            "<label>Model ID<input id=\"downloadModelId\" required "
            "pattern=\"[A-Za-z0-9_.-]+\"></label>"
            "<label>Filename<input id=\"downloadFilename\" required "
            "pattern=\"[A-Za-z0-9_.-]+\"></label>"
            "<label>Source URL (Hugging Face, GitHub release, or ModelScope resolve URL)"
            "<input id=\"downloadSourceUrl\" type=\"url\" required></label>"
            "<label>Immutable revision (commit hash or release tag)"
            "<input id=\"downloadRevision\" required></label>"
            "<label>Expected SHA-256<input id=\"downloadSha256\" required "
            "pattern=\"[0-9a-fA-F]{64}\"></label>"
            "<label>File size (bytes)<input id=\"downloadSizeBytes\" "
            "type=\"number\" min=\"1\" required></label>"
            "<label>Minimum RAM (MiB)<input id=\"downloadMinRam\" type=\"number\" "
            "min=\"1\" required></label>"
            "<label>Recommended RAM (MiB)<input id=\"downloadRecRam\" "
            "type=\"number\" min=\"1\" required></label>"
            // These four exist so a manifest.json can be written the
            // moment the file lands -- without one ModelRegistry never
            // recognizes the download as a model at all, and it could
            // never reach Ready or be chatted with.
            "<label>Display name<input id=\"downloadDisplayName\" required "
            "maxlength=\"160\"></label>"
            "<label>Architecture (e.g. qwen2, llama, gemma, phi3)"
            "<input id=\"downloadArchitecture\" required "
            "pattern=\"[A-Za-z0-9_.-]+\"></label>"
            "<label>Quantization (e.g. Q4_K_M)<input id=\"downloadQuantization\" "
            "required pattern=\"[A-Za-z0-9_.-]+\"></label>"
            "<label>License<select id=\"downloadLicenseSpdx\" required>"
            "<option value=\"\">Choose the model's actual license</option>"
            "<option value=\"Apache-2.0\">Apache-2.0</option>"
            "<option value=\"MIT\">MIT</option>"
            "<option value=\"BSD-2-Clause\">BSD-2-Clause</option>"
            "<option value=\"BSD-3-Clause\">BSD-3-Clause</option>"
            "<option value=\"CC-BY-4.0\">CC-BY-4.0</option>"
            "<option value=\"Llama-3.1\">Llama-3.1</option>"
            "<option value=\"Llama-3.2\">Llama-3.2</option>"
            "<option value=\"Gemma\">Gemma</option></select>"
            "<p>If the model's real license isn't listed, it cannot be "
            "downloaded through this form.</p></label>"
            "<label class=\"checkboxLabel\"><input id=\"downloadLicense\" "
            "type=\"checkbox\" required> "
            "I have reviewed and accept this model's license</label>"
            "<button title=\"Queue download\">" ICON_DOWNLOAD_SVG " Queue download</button></form>"
            // Active downloads lives at the bottom of this same page --
            // queueing a download and watching it run are one continuous
            // task, not two separate destinations.
            "<h3>Active downloads</h3>"
            "<div id=\"downloadsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "models-benchmarks") {
        body =
            "<section id=\"panel-models-benchmarks\" class=\"panel\"><div>"
            "<h2>Benchmarks</h2><div id=\"benchmarksList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-dashboard") {
        // Foundation-phase acknowledgement page: confirms the Machine
        // Learning module is enabled and shows its real (currently zero)
        // counts alongside the full roadmap of interfaces from docs/PLAN.md
        // "Machine Learning Abilities" section 2, each marked available or
        // planned -- see MachineLearningRegistry's class comment in
        // masterai.hpp for why nothing here is fabricated.
        body =
            "<section id=\"panel-ml-dashboard\" class=\"panel\"><div>"
            "<h2>Machine Learning</h2>"
            "<p id=\"mlAck\">Loading...</p>"
            "<div id=\"mlStats\"></div>"
            "</div><div>"
            "<h2>Interfaces</h2>"
            "<div id=\"mlInterfaces\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-projects") {
        // Phase 38 (docs/PLAN.md "Machine Learning Abilities" section 5):
        // create and list ML projects. Only the identity/intent/status
        // fields MLProjectStore actually persists are collected here -- see
        // that class's comment in masterai.hpp for the fields deferred to
        // later phases.
        body =
            "<section id=\"panel-ml-projects\" class=\"panel\"><div>"
            "<h2>New Machine Learning project</h2>"
            "<form id=\"newMlProject\"><label>Name"
            "<input id=\"mlProjectName\" required maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlProjectDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Objective<textarea id=\"mlProjectObjective\" "
            "rows=\"2\"></textarea></label>"
            "<label>Subject domain<input id=\"mlProjectSubjectDomain\"></label>"
            "<label>Model task<input id=\"mlProjectModelTask\"></label>"
            "<button title=\"Create project\">" ICON_PLUS_SVG " Create project</button></form>"
            "</div><div>"
            "<h2>Projects</h2><div id=\"mlProjectsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-models") {
        // Phase 39 (docs/PLAN.md "Machine Learning Abilities" section 7):
        // register and list Model Registry entries. Only the
        // identity/provenance/lifecycle fields ModelRegistryStore actually
        // persists are collected here -- see that class's comment in
        // masterai.hpp for the fields deferred to later phases.
        body =
            "<section id=\"panel-ml-models\" class=\"panel\"><div>"
            "<h2>Register a model</h2>"
            "<form id=\"newMlModel\">"
            "<label>Internal name (unique identifier)"
            "<input id=\"mlModelName\" required maxlength=\"160\"></label>"
            "<label>Display name<input id=\"mlModelDisplayName\"></label>"
            "<label>Version<input id=\"mlModelVersion\" "
            "placeholder=\"e.g. 1.0.0\"></label>"
            "<label>Model family<input id=\"mlModelFamily\" "
            "placeholder=\"e.g. Llama, Mistral\"></label>"
            "<label>Model task<input id=\"mlModelTask\" "
            "placeholder=\"e.g. text-classification\"></label>"
            "<label>Model format<input id=\"mlModelFormat\" "
            "placeholder=\"e.g. GGUF, ONNX, safetensors\"></label>"
            "<label>Source<input id=\"mlModelSource\" "
            "placeholder=\"where this model came from\"></label>"
            "<label>License<input id=\"mlModelLicense\"></label>"
            "<button title=\"Register model\">" ICON_PLUS_SVG " Register model</button></form>"
            // Phase 56: live prediction against a trained model's persisted
            // weights. Feature values are entered as JSON keyed by column
            // name so the caller never has to know the internal ordering.
            "<h2>Predict with a trained model</h2>"
            "<form id=\"mlPredictForm\">"
            "<label>Model ID (a model produced by a training run)"
            "<select id=\"mlPredictModelId\" required><option value=\"\">"
            "Choose a trained model</option></select></label>"
            "<label>Feature values (JSON object, e.g. "
            "{&quot;sepal_length&quot;:5.1,&quot;sepal_width&quot;:3.5})"
            "<textarea id=\"mlPredictFeatures\" rows=\"3\" required>"
            "</textarea></label>"
            "<button title=\"Predict\">" ICON_PLAY_SVG " Predict</button></form>"
            "<p id=\"mlPredictResult\"></p>"
            "</div><div>"
            "<h2>Registered models</h2>"
            "<div id=\"mlModelsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-datasets") {
        // Phase 39 (docs/PLAN.md "Machine Learning Abilities" section 10):
        // register and list datasets, and approve or reject them. Only the
        // identity/provenance/approval fields DatasetStore actually
        // persists are collected here -- see that class's comment in
        // masterai.hpp for the fields deferred to later phases (record/file
        // count, schema, versioning, ...).
        body =
            "<section id=\"panel-ml-datasets\" class=\"panel\"><div>"
            "<h2>Register a dataset</h2>"
            "<form id=\"newMlDataset\">"
            "<label>Name<input id=\"mlDatasetName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlDatasetDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Subject area<input id=\"mlDatasetSubjectArea\" "
            "placeholder=\"e.g. customer support transcripts\"></label>"
            "<label>Source<input id=\"mlDatasetSource\" "
            "placeholder=\"where this data came from\"></label>"
            "<label>License<input id=\"mlDatasetLicense\"></label>"
            "<label>Data format<input id=\"mlDatasetFormat\" "
            "placeholder=\"e.g. JSONL, CSV\"></label>"
            "<button title=\"Register dataset\">" ICON_PLUS_SVG " Register dataset</button></form>"
            // Phase 56: real content upload. The CSV is fully parsed and
            // validated server-side before it is stored, and the returned
            // profile (rows, columns, task) is shown below the form.
            "<h2>Upload dataset content (CSV)</h2>"
            "<form id=\"newMlDatasetContent\">"
            "<label>Dataset<select id=\"mlDatasetContentId\" required>"
            "<option value=\"\">Choose a registered dataset</option>"
            "</select></label>"
            "<label>Target column (the column to predict; blank uses the "
            "last column)<input id=\"mlDatasetContentTarget\"></label>"
            "<label>CSV file (header row first; feature columns must be "
            "numeric)<input id=\"mlDatasetContentFile\" type=\"file\" "
            "accept=\".csv,text/csv\" required></label>"
            "<button title=\"Upload content\">" ICON_UPLOAD_SVG " Upload content</button></form>"
            "<p id=\"mlDatasetContentResult\"></p>"
            "</div><div>"
            "<h2>Registered datasets</h2>"
            "<div id=\"mlDatasetsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-subjects") {
        // Phase 40 (docs/PLAN.md "Machine Learning Abilities" section 12):
        // register and list subject packages, and move them through review.
        // Only the identity/scope/ownership/review-status fields
        // SubjectPackageStore actually persists are collected here -- see
        // that class's comment in masterai.hpp for the fields deferred to
        // the later Knowledge Ingestion Pipeline phase.
        body =
            "<section id=\"panel-ml-subjects\" class=\"panel\"><div>"
            "<h2>New subject package</h2>"
            "<form id=\"newMlSubject\"><label>Name"
            "<input id=\"mlSubjectName\" required maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlSubjectDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Scope<textarea id=\"mlSubjectScope\" rows=\"2\" "
            "placeholder=\"what this subject does and does not cover\">"
            "</textarea></label>"
            "<label>Target audience<input id=\"mlSubjectTargetAudience\"></label>"
            "<button title=\"Create subject package\">" ICON_PLUS_SVG " Create subject package</button></form>"
            "<h2>Ingest a knowledge file</h2>"
            "<p>Select a real local text source. MasterAI hashes it, creates "
            "overlapping chunks, generates local deterministic vectors, and "
            "persists the populated index.</p>"
            "<form id=\"newMlKnowledgeDocument\">"
            "<label>Subject package<select id=\"mlKnowledgeSubjectId\" required>"
            "<option value=\"\">Choose a subject</option></select></label>"
            "<label>Vector store<select id=\"mlKnowledgeVectorStoreId\" required>"
            "<option value=\"\">Choose a vector store</option></select></label>"
            "<label>Knowledge file<input id=\"mlKnowledgeFile\" type=\"file\" "
            "accept=\".txt,.md,.markdown,.csv,.json,.jsonl,.parquet,text/plain,text/markdown,text/csv,application/json\" "
            "required></label><p class=\"fieldHint\">Size limit is set by the "
            "administrator. Text, Markdown, CSV, JSON, JSONL, and Parquet are "
            "supported.</p>"
            "<button title=\"Ingest and index file\">" ICON_UPLOAD_SVG " Ingest and index file</button></form>"
            "<p id=\"mlKnowledgeUploadResult\"></p>"
            "</div><div>"
            "<h2>Subject packages</h2>"
            "<div id=\"mlSubjectsList\">Loading...</div>"
            "<h2>Ingested knowledge files</h2>"
            "<div id=\"mlKnowledgeDocumentsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-label-tasks") {
        // Phase 41 (docs/PLAN.md "Machine Learning Abilities" section 14):
        // create and list labeling tasks against a registered dataset, and
        // move them through a lifecycle status. Only the identity/target-
        // dataset/label-mode/assignment/status fields LabelTaskStore
        // actually persists are collected here -- see that class's comment
        // in masterai.hpp for the label-record features deferred to a later
        // phase.
        body =
            "<section id=\"panel-ml-label-tasks\" class=\"panel\"><div>"
            "<h2>New labeling task</h2>"
            "<form id=\"newMlLabelTask\">"
            "<label>Dataset<select id=\"mlLabelTaskDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Name<input id=\"mlLabelTaskName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlLabelTaskDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Label mode<input id=\"mlLabelTaskLabelMode\" "
            "placeholder=\"e.g. text_category, entity_span, bounding_box\">"
            "</label>"
            "<label>Assignee (optional)<select id=\"mlLabelTaskAssigneeId\">"
            "<option value=\"\">Unassigned</option></select></label>"
            "<button title=\"Create labeling task\">" ICON_PLUS_SVG " Create labeling task</button></form>"
            "</div><div>"
            "<h2>Labeling tasks</h2>"
            "<div id=\"mlLabelTasksList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-prep-jobs") {
        // Phase 41 (docs/PLAN.md "Machine Learning Abilities" section 15):
        // create and list data-preparation jobs against a registered
        // dataset, and move them through a lifecycle status. Only the
        // identity/target-dataset/operation/status fields
        // DataPreparationJobStore actually persists are collected here --
        // see that class's comment in masterai.hpp for the pipeline-step
        // composition, logging, and reproducibility record deferred to the
        // phase that actually executes a pipeline.
        body =
            "<section id=\"panel-ml-prep-jobs\" class=\"panel\"><div>"
            "<h2>New data preparation job</h2>"
            "<form id=\"newMlPrepJob\">"
            "<label>Dataset<select id=\"mlPrepJobDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Name<input id=\"mlPrepJobName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlPrepJobDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Operation<input id=\"mlPrepJobOperation\" required "
            "placeholder=\"e.g. remove_duplicates, redact_pii, split_dataset\">"
            "</label>"
            "<button title=\"Create preparation job\">" ICON_PLUS_SVG " Create preparation job</button></form>"
            "</div><div>"
            "<h2>Preparation jobs</h2>"
            "<div id=\"mlPrepJobsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-training-jobs") {
        // Phase 42 (docs/PLAN.md "Machine Learning Abilities" section 16):
        // create and list training jobs against a registered project and
        // dataset, and move them through a lifecycle status. Only the
        // identity/target-project/target-model/target-dataset/training-
        // type/status fields TrainingJobStore actually persists are
        // collected here -- see that class's comment in masterai.hpp for
        // the compute/hyperparameter/scheduling fields deferred to the
        // phase that actually executes a training run.
        body =
            "<section id=\"panel-ml-training-jobs\" class=\"panel\"><div>"
            "<h2>New training job</h2>"
            "<form id=\"newMlTrainingJob\">"
            "<label>Project<select id=\"mlTrainingJobProjectId\" required>"
            "<option value=\"\">Choose a project</option></select></label>"
            "<label>Existing model (optional)<select id=\"mlTrainingJobModelId\">"
            "<option value=\"\">Create a new model</option></select></label>"
            "<label>Dataset<select id=\"mlTrainingJobDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Name<input id=\"mlTrainingJobName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlTrainingJobDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Training type<input id=\"mlTrainingJobTrainingType\" "
            "placeholder=\"e.g. fine_tuning, transfer_learning, lora\">"
            "</label>"
            "<button title=\"Create training job\">" ICON_PLUS_SVG " Create training job</button></form>"
            "</div><div>"
            "<h2>Training jobs</h2>"
            "<div id=\"mlTrainingJobsList\">Loading...</div>"
            // Phase 56: the real training executor reports its genuine
            // result (method, final loss, held-out metrics) here after a
            // "Train now" click.
            "<h2>Last training result</h2>"
            "<p id=\"mlTrainingRunResult\">No training run in this session "
            "yet. Click \"Train now\" on a job whose dataset has uploaded "
            "CSV content.</p>"
            "</div></section>";
    } else if (section == "ml-evaluation-runs") {
        // Phase 43 (docs/PLAN.md "Machine Learning Abilities" section 23):
        // create and list evaluation runs against a registered model and
        // benchmark dataset, and move them through a lifecycle status. Only
        // the identity/target-model/target-dataset/category/status fields
        // EvaluationRunStore actually persists are collected here -- see
        // that class's comment in masterai.hpp for the benchmark-set/human-
        // evaluation/comparison/numeric-score fields deferred to the phase
        // that actually executes an evaluation.
        body =
            "<section id=\"panel-ml-evaluation-runs\" class=\"panel\"><div>"
            "<h2>New evaluation run</h2>"
            "<form id=\"newMlEvaluationRun\">"
            "<label>Model<select id=\"mlEvaluationRunModelId\" required>"
            "<option value=\"\">Choose a trained model</option></select></label>"
            "<label>Benchmark dataset<select id=\"mlEvaluationRunDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Name<input id=\"mlEvaluationRunName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlEvaluationRunDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Category<input id=\"mlEvaluationRunCategory\" "
            "placeholder=\"e.g. accuracy, f1_score, hallucination_rate\">"
            "</label>"
            "<button title=\"Create evaluation run\">" ICON_PLUS_SVG " Create evaluation run</button></form>"
            "</div><div>"
            "<h2>Evaluation runs</h2>"
            "<div id=\"mlEvaluationRunsList\">Loading...</div>"
            // Phase 56: the real evaluation harness reports its genuine
            // metrics here after an "Evaluate now" or "View result" click.
            "<h2>Last evaluation result</h2>"
            "<p id=\"mlEvaluationRunResult\">No evaluation shown yet. Click "
            "\"Evaluate now\" on a run whose model has been trained.</p>"
            "</div></section>";
    } else if (section == "ml-experiments") {
        // Phase 44 (docs/PLAN.md "Machine Learning Abilities" section 25):
        // create and list experiments tying a registered project and model
        // (and optionally a dataset) together, and move them through a
        // lifecycle status. Only the identity/target-project/target-model/
        // target-dataset/status fields ExperimentStore actually persists are
        // collected here -- see that class's comment in masterai.hpp for the
        // version/hyperparameter/metric/artifact/comparison fields deferred
        // to the phase that actually executes and records a run.
        body =
            "<section id=\"panel-ml-experiments\" class=\"panel\"><div>"
            "<h2>New experiment</h2>"
            "<form id=\"newMlExperiment\">"
            "<label>Project<select id=\"mlExperimentProjectId\" required>"
            "<option value=\"\">Choose a project</option></select></label>"
            "<label>Model<select id=\"mlExperimentModelId\" required>"
            "<option value=\"\">Choose a model</option></select></label>"
            "<label>Dataset (optional)<select id=\"mlExperimentDatasetId\">"
            "<option value=\"\">No dataset</option></select></label>"
            "<label>Name<input id=\"mlExperimentName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlExperimentDescription\" "
            "rows=\"2\"></textarea></label>"
            "<button title=\"Create experiment\">" ICON_PLUS_SVG " Create experiment</button></form>"
            "</div><div>"
            "<h2>Experiments</h2>"
            "<div id=\"mlExperimentsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-fine-tuning-jobs") {
        // Phase 45 (docs/PLAN.md "Machine Learning Abilities" section 18):
        // create and list fine-tuning jobs against a registered base model
        // and fine-tuning dataset, and move them through a lifecycle
        // status. Only the identity/target-project/target-model/target-
        // dataset/method/status fields FineTuningJobStore actually
        // persists are collected here -- see that class's comment in
        // masterai.hpp for the adapter-method/hyperparameter/checkpoint/
        // output-model fields deferred to the phase that actually executes
        // a fine-tuning run.
        body =
            "<section id=\"panel-ml-fine-tuning-jobs\" class=\"panel\"><div>"
            "<h2>New fine-tuning job</h2>"
            "<form id=\"newMlFineTuningJob\">"
            "<label>Base model<select id=\"mlFineTuningJobModelId\" required>"
            "<option value=\"\">Choose a base model</option></select></label>"
            "<label>Fine-tuning dataset<select id=\"mlFineTuningJobDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Project (optional)<select id=\"mlFineTuningJobProjectId\">"
            "<option value=\"\">No project</option></select></label>"
            "<label>Name<input id=\"mlFineTuningJobName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlFineTuningJobDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Method<input id=\"mlFineTuningJobMethod\" "
            "placeholder=\"e.g. subject_specialisation, code_assistant, "
            "safety_alignment\"></label>"
            "<button title=\"Create fine-tuning job\">" ICON_PLUS_SVG " Create fine-tuning job</button></form>"
            "</div><div>"
            "<h2>Fine-tuning jobs</h2>"
            "<div id=\"mlFineTuningJobsList\">Loading...</div>"
            // Phase 70: the real fine-tuning executor reports its genuine
            // result (method, final loss, held-out metrics, adapted model
            // id) here after a "Fine-tune now" click.
            "<h2>Last fine-tuning result</h2>"
            "<p id=\"mlFineTuningRunResult\">No fine-tuning run in this "
            "session yet. Click \"Fine-tune now\" on a job whose base model "
            "has been trained and whose dataset has uploaded CSV "
            "content.</p>"
            "</div></section>";
    } else if (section == "ml-model-builder-configs") {
        // Phase 46 (docs/PLAN.md "Machine Learning Abilities" section 9) at
        // full surface: create and list model builder configurations
        // against an optional project and optional base model, move them
        // through a design-time lifecycle status, and edit every build
        // setting from section 9's configuration list through the
        // "Configure build settings" form. That form provides the basic and
        // advanced configuration modes section 9 requires: basic shows the
        // six everyday fields, advanced additionally reveals the full
        // architecture/regularisation/scheduling/reproducibility surface.
        body =
            "<section id=\"panel-ml-model-builder-configs\" class=\"panel\">"
            "<div>"
            "<h2>New model builder configuration</h2>"
            "<form id=\"newMlModelBuilderConfig\">"
            "<label>Source type<input id=\"mlModelBuilderConfigSourceType\" "
            "required placeholder=\"e.g. template, imported_base_model, "
            "embedding_model\"></label>"
            "<label>Base model (optional)<select "
            "id=\"mlModelBuilderConfigBaseModelId\"><option value=\"\">"
            "No base model</option></select></label>"
            "<label>Project (optional)<select id=\"mlModelBuilderConfigProjectId\">"
            "<option value=\"\">No project</option></select></label>"
            "<label>Name<input id=\"mlModelBuilderConfigName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea "
            "id=\"mlModelBuilderConfigDescription\" rows=\"2\"></textarea>"
            "</label>"
            "<button title=\"Create configuration\">" ICON_PLUS_SVG " Create configuration</button></form>"
            "</div><div>"
            "<h2>Configure build settings</h2>"
            "<p id=\"mlMbcTarget\">Press Configure on a configuration in "
            "the list below to edit its build settings here.</p>"
            "<form id=\"configureMlModelBuilderConfig\">"
            "<label>Configuration mode<select id=\"mlMbcMode\">"
            "<option value=\"basic\">Basic</option>"
            "<option value=\"advanced\">Advanced</option></select></label>"
            "<label>Model architecture<input id=\"mlMbcArchitecture\" "
            "placeholder=\"e.g. transformer_decoder, cnn, "
            "gradient_boosted_trees\"></label>"
            "<label>Loss function<input id=\"mlMbcLossFunction\" "
            "placeholder=\"e.g. cross_entropy, mse\"></label>"
            "<label>Optimiser<input id=\"mlMbcOptimiser\" "
            "placeholder=\"e.g. adamw, sgd\"></label>"
            "<label>Batch size<input id=\"mlMbcBatchSize\" type=\"number\" "
            "min=\"0\" placeholder=\"0 = executor default\"></label>"
            "<label>Epoch count<input id=\"mlMbcEpochCount\" "
            "type=\"number\" min=\"0\" placeholder=\"0 = executor default\">"
            "</label>"
            "<label>Sequence length<input id=\"mlMbcSequenceLength\" "
            "type=\"number\" min=\"0\" placeholder=\"0 = executor default\">"
            "</label>"
            "<div id=\"mlMbcAdvanced\" style=\"display:none\">"
            "<label>Layer configuration<textarea "
            "id=\"mlMbcLayerConfiguration\" rows=\"2\" placeholder=\"e.g. "
            "24 decoder layers\"></textarea></label>"
            "<label>Hidden dimensions<input id=\"mlMbcHiddenDimensions\" "
            "type=\"number\" min=\"0\" placeholder=\"0 = executor default\">"
            "</label>"
            "<label>Attention configuration<input "
            "id=\"mlMbcAttentionConfiguration\" placeholder=\"e.g. 16 heads, "
            "grouped-query attention\"></label>"
            "<label>Vocabulary and tokenizer<input "
            "id=\"mlMbcVocabularyTokenizer\" placeholder=\"e.g. 32000-entry "
            "BPE tokenizer\"></label>"
            "<label>Activation functions<input "
            "id=\"mlMbcActivationFunctions\" placeholder=\"e.g. silu, "
            "gelu\"></label>"
            "<label>Dropout (0.0 to 1.0)<input id=\"mlMbcDropout\" "
            "type=\"number\" min=\"0\" max=\"1\" step=\"0.01\" "
            "placeholder=\"0 = disabled\"></label>"
            "<label>Initialisation strategy<input "
            "id=\"mlMbcInitialisationStrategy\" placeholder=\"e.g. xavier, "
            "kaiming\"></label>"
            "<label>Learning-rate scheduler<input "
            "id=\"mlMbcLearningRateScheduler\" placeholder=\"e.g. cosine "
            "with warmup\"></label>"
            "<label>Gradient accumulation steps<input "
            "id=\"mlMbcGradientAccumulation\" type=\"number\" min=\"0\" "
            "placeholder=\"0 = disabled\"></label>"
            "<label>Gradient clipping (max norm)<input "
            "id=\"mlMbcGradientClipping\" type=\"number\" min=\"0\" "
            "step=\"0.1\" placeholder=\"0 = disabled\"></label>"
            "<label>Mixed precision<input id=\"mlMbcMixedPrecision\" "
            "type=\"checkbox\"></label>"
            "<label>Checkpoint frequency (steps)<input "
            "id=\"mlMbcCheckpointFrequency\" type=\"number\" min=\"0\" "
            "placeholder=\"0 = executor default\"></label>"
            "<label>Validation frequency (steps)<input "
            "id=\"mlMbcValidationFrequency\" type=\"number\" min=\"0\" "
            "placeholder=\"0 = executor default\"></label>"
            "<label>Early stopping<input id=\"mlMbcEarlyStopping\" "
            "type=\"checkbox\"></label>"
            "<label>Random seed<input id=\"mlMbcRandomSeed\" "
            "type=\"number\" min=\"0\" placeholder=\"0 = not fixed\">"
            "</label>"
            "<label>Reproducibility settings<textarea "
            "id=\"mlMbcReproducibilitySettings\" rows=\"2\" "
            "placeholder=\"e.g. deterministic kernels, pinned library "
            "versions\"></textarea></label>"
            "<label>Distributed-training settings<textarea "
            "id=\"mlMbcDistributedTrainingSettings\" rows=\"2\" "
            "placeholder=\"e.g. 2-node data parallel\"></textarea></label>"
            "</div>"
            "<button title=\"Save build settings\">" ICON_SAVE_SVG " Save build settings</button></form>"
            "</div><div>"
            "<h2>Model builder configurations</h2>"
            "<div id=\"mlModelBuilderConfigsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-instruction-examples") {
        // Phase 47 (docs/PLAN.md "Machine Learning Abilities" section 19):
        // create and list instruction examples against a registered
        // dataset, and move them through a reviewer-approval lifecycle
        // status. Only the identity/target-dataset/subject-classification/
        // status fields InstructionExampleStore actually persists are
        // collected here -- see that class's comment in masterai.hpp for
        // the system-instruction/user-instruction/context/expected-
        // response/rejected-response/tool-call/output-format fields
        // deferred to the phase that actually creates example records.
        body =
            "<section id=\"panel-ml-instruction-examples\" class=\"panel\">"
            "<div>"
            "<h2>New instruction example</h2>"
            "<form id=\"newMlInstructionExample\">"
            "<label>Dataset<select id=\"mlInstructionExampleDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Name<input id=\"mlInstructionExampleName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea "
            "id=\"mlInstructionExampleDescription\" rows=\"2\"></textarea>"
            "</label>"
            "<label>Subject classification<input "
            "id=\"mlInstructionExampleSubjectClassification\" "
            "placeholder=\"e.g. cpp_code_review, customer_support\"></label>"
            "<button title=\"Create instruction example\">" ICON_PLUS_SVG " Create instruction example</button></form>"
            "</div><div>"
            "<h2>Instruction examples</h2>"
            "<div id=\"mlInstructionExamplesList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-synthetic-records") {
        // Phase 48 (docs/PLAN.md "Machine Learning Abilities" section 20):
        // create and list synthetic records generated against a registered
        // dataset, and move them through the same reviewer-approval
        // lifecycle status Prompt and Instruction Training uses. Only the
        // identity/target-dataset/generation-technique/status fields
        // SyntheticRecordStore actually persists are collected here -- see
        // that class's comment in masterai.hpp for the generator-model/
        // generator-version/prompt/generation-settings/confidence-score/
        // original-source-linkage fields deferred to the phase that
        // actually creates generated records.
        body =
            "<section id=\"panel-ml-synthetic-records\" class=\"panel\">"
            "<div>"
            "<h2>New synthetic record</h2>"
            "<form id=\"newMlSyntheticRecord\">"
            "<label>Dataset<select id=\"mlSyntheticRecordDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Name<input id=\"mlSyntheticRecordName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea "
            "id=\"mlSyntheticRecordDescription\" rows=\"2\"></textarea>"
            "</label>"
            "<label>Generation technique<input "
            "id=\"mlSyntheticRecordGenerationTechnique\" "
            "placeholder=\"e.g. paraphrase, edge_case, counterexample\">"
            "</label>"
            "<button title=\"Create synthetic record\">" ICON_PLUS_SVG " Create synthetic record</button></form>"
            "</div><div>"
            "<h2>Synthetic records</h2>"
            "<div id=\"mlSyntheticRecordsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-vector-stores") {
        // Phases 49/61 (Machine Learning Abilities section 21):
        // register and list vector stores, and move them through the same
        // three-state pending/approved/rejected approval workflow Dataset
        // Manager uses, since a vector store is a standalone registered
        // resource rather than a target-scoped content record. Only the
        // identity/embedding-model/distance-metric/status fields
        // VectorStoreStore actually persists are collected here -- see that
        // class's comment in masterai.hpp for the document-import/chunking/
        // index profile is execution evidence from Phases 59/61; the form
        // now offers the authored method plus verified ready embedding GGUFs.
        body =
            "<section id=\"panel-ml-vector-stores\" class=\"panel\">"
            "<div>"
            "<h2>New vector store</h2>"
            "<form id=\"newMlVectorStore\">"
            "<label>Name<input id=\"mlVectorStoreName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea "
            "id=\"mlVectorStoreDescription\" rows=\"2\"></textarea></label>"
            "<label>Embedding method<select id=\"mlVectorStoreEmbeddingModel\">"
            "<option value=\"authored_hashing_vectorizer_v1\">MasterAI "
            "authored hashing vectorizer v1 (128 dimensions)</option>"
            "</select></label>"
            "<label>Distance metric<select id=\"mlVectorStoreDistanceMetric\">"
            "<option value=\"cosine\">Cosine similarity</option>"
            "</select></label>"
            "<button title=\"Create vector store\">" ICON_PLUS_SVG " Create vector store</button></form>"
            "</div><div>"
            "<h2>Vector stores</h2>"
            "<div id=\"mlVectorStoresList\">Loading...</div>"
            "<h3>Index profile</h3><p id=\"mlVectorIndexResult\">Select "
            "View index on a vector store to inspect its populated index.</p>"
            "</div></section>";
    } else if (section == "ml-rag-configs") {
        // Phase 50 (docs/PLAN.md "Machine Learning Abilities" section 22):
        // register and list RAG configurations, and move them through the
        // same three-state pending/approved/rejected approval workflow
        // Vector Stores use, since a RAG configuration is a standalone
        // registered resource rather than a target-scoped content record.
        // Only the identity/search-strategy/vector-store-reference/status
        // fields RagConfigStore actually persists are collected here -- see
        // that class's comment in masterai.hpp for the retrieval-testing/
        // reranking/citation fields deferred to the phase that actually
        // generates retrieval results.
        body =
            "<section id=\"panel-ml-rag-configs\" class=\"panel\">"
            "<div>"
            "<h2>New RAG configuration</h2>"
            "<form id=\"newMlRagConfig\">"
            "<label>Name<input id=\"mlRagConfigName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea "
            "id=\"mlRagConfigDescription\" rows=\"2\"></textarea></label>"
            "<label>Search strategy<select id=\"mlRagConfigSearchStrategy\">"
            "<option value=\"hybrid\">Hybrid vector + keyword</option>"
            "<option value=\"vector\">Vector only</option>"
            "<option value=\"keyword\">Keyword only</option>"
            "</select></label>"
            "<label>Vector store<select id=\"mlRagConfigVectorStoreId\" required>"
            "<option value=\"\">Choose a vector store</option></select></label>"
            "<button title=\"Create RAG configuration\">" ICON_PLUS_SVG " Create RAG configuration</button></form>"
            "<h2>Test retrieval</h2>"
            "<form id=\"mlRagQueryForm\">"
            "<label>Approved RAG configuration<select id=\"mlRagQueryConfigId\" required>"
            "<option value=\"\">Choose an approved RAG configuration</option>"
            "</select></label>"
            "<label>Question<textarea id=\"mlRagQueryText\" rows=\"3\" "
            "required maxlength=\"8192\"></textarea></label>"
            "<label>Retrieved chunks<input id=\"mlRagQueryTopK\" type=\"number\" "
            "min=\"1\" max=\"20\" value=\"5\" required></label>"
            "<button title=\"Retrieve grounded context\">" ICON_PLAY_SVG " Retrieve grounded context</button></form>"
            "</div><div>"
            "<h2>RAG configurations</h2>"
            "<div id=\"mlRagConfigsList\">Loading...</div>"
            "<h3>Retrieval evidence</h3><pre id=\"mlRagQueryResult\">No query "
            "executed yet.</pre>"
            "</div></section>";
    } else if (section == "ml-subject-exams") {
        // Phase 51 (docs/PLAN.md "Machine Learning Abilities" section 24):
        // create and list subject exams and move them through the same
        // five-state reviewer-approval workflow Instruction Examples use,
        // since an exam is authored content a reviewer approves before it
        // may examine anything. Only the identity/subject-reference/
        // question-format/status fields SubjectExamStore actually persists
        // are collected here -- see that class's comment in masterai.hpp
        // for the question-bank/scoring fields deferred to the phase that
        // actually administers exams.
        body =
            "<section id=\"panel-ml-subject-exams\" class=\"panel\">"
            "<div>"
            "<h2>New subject exam</h2>"
            "<form id=\"newMlSubjectExam\">"
            "<label>Name<input id=\"mlSubjectExamName\" required "
            "maxlength=\"160\"></label>"
            "<label>Subject package<select id=\"mlSubjectExamSubjectId\" required>"
            "<option value=\"\">Choose a subject</option></select></label>"
            "<label>Description<textarea "
            "id=\"mlSubjectExamDescription\" rows=\"2\"></textarea></label>"
            "<label>Question format<input "
            "id=\"mlSubjectExamQuestionFormat\" "
            "placeholder=\"e.g. multiple_choice, short_answer, code_task\">"
            "</label>"
            "<button title=\"Create subject exam\">" ICON_PLUS_SVG " Create subject exam</button></form>"
            "</div><div>"
            "<h2>Subject exams</h2>"
            "<div id=\"mlSubjectExamsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-hyperparameter-searches") {
        // Phase 52 (docs/PLAN.md "Machine Learning Abilities" section 26):
        // create and list hyperparameter searches and move them through the
        // same eleven-state job lifecycle Training Jobs use, since a search
        // queues, runs, and pauses like any other job. Only the identity/
        // training-job-reference/strategy/status fields
        // HyperparameterSearchStore actually persists are collected here --
        // see that class's comment in masterai.hpp for the search-space/
        // trial-history fields deferred to the phase that actually runs
        // searches.
        body =
            "<section id=\"panel-ml-hyperparameter-searches\" "
            "class=\"panel\">"
            "<div>"
            "<h2>New hyperparameter search</h2>"
            "<form id=\"newMlHyperparameterSearch\">"
            "<label>Name<input id=\"mlHyperparameterSearchName\" required "
            "maxlength=\"160\"></label>"
            "<label>Training job<select id=\"mlHyperparameterSearchTrainingJobId\" required>"
            "<option value=\"\">Choose a training job</option></select></label>"
            "<label>Description<textarea "
            "id=\"mlHyperparameterSearchDescription\" rows=\"2\">"
            "</textarea></label>"
            "<label>Search strategy<input "
            "id=\"mlHyperparameterSearchStrategy\" "
            "placeholder=\"e.g. grid, random, bayesian\"></label>"
            "<button title=\"Create hyperparameter search\">" ICON_PLUS_SVG " Create hyperparameter search</button></form>"
            "</div><div>"
            "<h2>Hyperparameter searches</h2>"
            "<div id=\"mlHyperparameterSearchesList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-model-optimizations") {
        // Phase 53 (docs/PLAN.md "Machine Learning Abilities" section 28):
        // create and list model optimization runs and move them through the
        // same eleven-state job lifecycle Training Jobs use, since an
        // optimization run executes like any other job. Only the identity/
        // model-reference/operation/status fields ModelOptimizationStore
        // actually persists are collected here -- see that class's comment
        // in masterai.hpp for the before/after-comparison fields deferred
        // to the phase that actually optimizes models.
        body =
            "<section id=\"panel-ml-model-optimizations\" class=\"panel\">"
            "<div>"
            "<h2>New model optimization</h2>"
            "<form id=\"newMlModelOptimization\">"
            "<label>Name<input id=\"mlModelOptimizationName\" required "
            "maxlength=\"160\"></label>"
            "<label>Model<select id=\"mlModelOptimizationModelId\" required>"
            "<option value=\"\">Choose a model</option></select></label>"
            "<label>Description<textarea "
            "id=\"mlModelOptimizationDescription\" rows=\"2\"></textarea>"
            "</label>"
            "<label>Operation<input id=\"mlModelOptimizationOperation\" "
            "placeholder=\"e.g. quantization, pruning, distillation\">"
            "</label>"
            "<button title=\"Create model optimization\">" ICON_PLUS_SVG " Create model optimization</button></form>"
            "</div><div>"
            "<h2>Model optimization runs</h2>"
            "<div id=\"mlModelOptimizationsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-checkpoints") {
        // Phase 54 (docs/PLAN.md "Machine Learning Abilities" section 33):
        // record and list training checkpoints and move them through a
        // bespoke three-state retention lifecycle (active/pinned/archived)
        // -- pinned is section 33's own "protect" operation, exempting a
        // checkpoint from retention deletion. Only the identity/training-
        // job-reference/capture-reason/status fields
        // TrainingCheckpointStore actually persists are collected here --
        // see that class's comment in masterai.hpp for the step/epoch/hash/
        // resume fields deferred to the phase that actually captures
        // checkpoints.
        body =
            "<section id=\"panel-ml-checkpoints\" class=\"panel\">"
            "<div>"
            "<h2>New checkpoint record</h2>"
            "<form id=\"newMlCheckpoint\">"
            "<label>Name<input id=\"mlCheckpointName\" required "
            "maxlength=\"160\"></label>"
            "<label>Training job<select id=\"mlCheckpointTrainingJobId\" required>"
            "<option value=\"\">Choose a training job</option></select></label>"
            "<label>Description<textarea "
            "id=\"mlCheckpointDescription\" rows=\"2\"></textarea></label>"
            "<label>Capture reason<input id=\"mlCheckpointCaptureReason\" "
            "placeholder=\"e.g. epoch_end, best_metric, manual\"></label>"
            "<button title=\"Create checkpoint record\">" ICON_PLUS_SVG " Create checkpoint record</button></form>"
            "</div><div>"
            "<h2>Checkpoints</h2>"
            "<div id=\"mlCheckpointsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-deployments") {
        // Phase 55 (docs/PLAN.md "Machine Learning Abilities" section 34):
        // record and list deployments and move them through the same
        // three-state pending/approved/rejected approval workflow Vector
        // Stores use, since section 34 explicitly names approval as part of
        // the deployment record. Only the identity/model-reference/
        // environment/strategy/status fields DeploymentStore actually
        // persists are collected here -- see that class's comment in
        // masterai.hpp for the health/rollback fields deferred to the phase
        // that actually promotes models.
        body =
            "<section id=\"panel-ml-deployments\" class=\"panel\">"
            "<div>"
            "<h2>New deployment</h2>"
            "<form id=\"newMlDeployment\">"
            "<label>Name<input id=\"mlDeploymentName\" required "
            "maxlength=\"160\"></label>"
            "<label>Model<select id=\"mlDeploymentModelId\" required>"
            "<option value=\"\">Choose a model</option></select></label>"
            "<label>Description<textarea "
            "id=\"mlDeploymentDescription\" rows=\"2\"></textarea></label>"
            "<label>Environment<input id=\"mlDeploymentEnvironment\" "
            "placeholder=\"e.g. development, staging, production\"></label>"
            "<label>Strategy<input id=\"mlDeploymentStrategy\" "
            "placeholder=\"e.g. direct, blue_green, canary\"></label>"
            "<button title=\"Create deployment\">" ICON_PLUS_SVG " Create deployment</button></form>"
            "</div><div>"
            "<h2>Deployments</h2>"
            "<div id=\"mlDeploymentsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-model-comparisons") {
        // Phase 57 (docs/PLAN.md "Machine Learning Abilities" section 27):
        // a REAL executor page like Training Jobs' "Train now" -- "Compare
        // now" evaluates both trained artifacts against the shared
        // benchmark dataset and reports the measured winner. Every field is
        // labeled with what it must reference so the form stays
        // self-explanatory.
        body =
            "<section id=\"panel-ml-model-comparisons\" class=\"panel\">"
            "<div>"
            "<h2>New model comparison</h2>"
            "<form id=\"newMlModelComparison\">"
            "<label>Name<input id=\"mlModelComparisonName\" required "
            "maxlength=\"160\"></label>"
            "<label>Baseline model<select id=\"mlModelComparisonBaselineModelId\" required>"
            "<option value=\"\">Choose the baseline model</option></select></label>"
            "<label>Candidate model<select id=\"mlModelComparisonCandidateModelId\" required>"
            "<option value=\"\">Choose the candidate model</option></select></label>"
            "<label>Benchmark dataset<select id=\"mlModelComparisonDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Description<textarea "
            "id=\"mlModelComparisonDescription\" rows=\"2\"></textarea>"
            "</label>"
            "<button title=\"Create model comparison\">" ICON_PLUS_SVG " Create model comparison</button></form>"
            "</div><div>"
            "<h2>Model comparisons</h2>"
            "<div id=\"mlModelComparisonsList\">Loading...</div>"
            // Phase 57: the real comparison verdict renders here after a
            // "Compare now" or "View result" click.
            "<h3>Comparison result</h3>"
            "<p id=\"mlModelComparisonResult\">No comparison shown yet. "
            "Click \"Compare now\" on a comparison whose two models have "
            "both been trained.</p>"
            "</div></section>";
    } else if (section == "ml-inference-endpoints") {
        // Phase 62 (docs/PLAN.md "Machine Learning Abilities" section 35):
        // records an administrator's intent to expose a model behind a
        // controlled endpoint -- see InferenceEndpoint's class comment in
        // masterai.hpp for why this does not open a real network listener.
        body =
            "<section id=\"panel-ml-inference-endpoints\" class=\"panel\">"
            "<div>"
            "<h2>New inference endpoint</h2>"
            "<form id=\"newMlInferenceEndpoint\">"
            "<label>Name<input id=\"mlEndpointName\" required "
            "maxlength=\"160\"></label>"
            "<label>Model<select id=\"mlEndpointModelId\" required>"
            "<option value=\"\">Choose a model</option></select></label>"
            "<label>Runtime<input id=\"mlEndpointRuntime\" "
            "placeholder=\"e.g. llama.cpp\"></label>"
            "<label>Host<input id=\"mlEndpointHost\" "
            "placeholder=\"e.g. 127.0.0.1\"></label>"
            "<label>Port<input id=\"mlEndpointPort\" type=\"number\" min=\"0\" "
            "max=\"65535\"></label>"
            "<label>Protocol<input id=\"mlEndpointProtocol\" "
            "placeholder=\"e.g. rest, websocket, mcp\"></label>"
            "<label>Authentication method<input "
            "id=\"mlEndpointAuthenticationMethod\" "
            "placeholder=\"e.g. api_token\"></label>"
            "<label>Rate limit, requests per minute<input "
            "id=\"mlEndpointRateLimit\" type=\"number\" min=\"0\"></label>"
            "<button title=\"Create inference endpoint\">" ICON_PLUS_SVG " Create inference endpoint</button></form>"
            "</div><div>"
            "<h2>Inference endpoints</h2>"
            "<div id=\"mlInferenceEndpointsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-compute-nodes") {
        // Phase 63 (docs/PLAN.md "Machine Learning Abilities" section 30):
        // a static compute-node registry. Phase 67 adds real live telemetry
        // for whichever single node is flagged as this MasterAI process's
        // own host -- see ComputeNode's class comment in masterai.hpp for
        // why a remote node still cannot be polled.
        body =
            "<section id=\"panel-ml-compute-nodes\" class=\"panel\">"
            "<div>"
            "<h2>New compute node</h2>"
            "<form id=\"newMlComputeNode\">"
            "<label>Name<input id=\"mlComputeNodeName\" required "
            "maxlength=\"160\"></label>"
            "<label>Address<input id=\"mlComputeNodeAddress\" "
            "placeholder=\"e.g. 127.0.0.1 or hostname\"></label>"
            "<label>Operating system<input "
            "id=\"mlComputeNodeOperatingSystem\"></label>"
            "<label>CPU description<input id=\"mlComputeNodeCpuDescription\">"
            "</label>"
            "<label>GPU description<input id=\"mlComputeNodeGpuDescription\">"
            "</label>"
            "<label>System memory, MiB<input id=\"mlComputeNodeMemoryMib\" "
            "type=\"number\" min=\"0\"></label>"
            "<label class=\"checkboxLabel\">"
            "<input type=\"checkbox\" id=\"mlComputeNodeIsLocal\">"
            " This is the local node MasterAI is running on (enables live "
            "CPU/RAM/GPU telemetry)</label>"
            "<button title=\"Create compute node\">" ICON_PLUS_SVG " Create compute node</button></form>"
            "</div><div>"
            "<h2>Compute nodes</h2>"
            "<div id=\"mlComputeNodesList\">Loading...</div>"
            "<div id=\"mlComputeNodeTelemetry\"></div>"
            "</div></section>";
    } else if (section == "ml-automation-pipelines") {
        // Phase 64/69/71 (docs/PLAN.md "Machine Learning Abilities" section
        // 37): a pipeline names an ordered subset of the section's sixteen
        // lifecycle stages. "Run" genuinely executes "Train model",
        // "Evaluate model", "Validate data", "Validate model", "Safety
        // tests", "Request approval", "Deploy staging", "Deploy
        // production", "Rollback", and "Monitor" (matched case-
        // insensitively); every other named stage is honestly recorded as
        // skipped -- see AutomationPipeline's class comment in
        // masterai.hpp. A run executes on a background thread and reports
        // live per-stage progress while it works.
        body =
            "<section id=\"panel-ml-automation-pipelines\" class=\"panel\">"
            "<div>"
            "<h2>New automation pipeline</h2>"
            "<form id=\"newMlAutomationPipeline\">"
            "<label>Name<input id=\"mlPipelineName\" required "
            "maxlength=\"160\"></label>"
            "<label>Project<select id=\"mlPipelineProjectId\">"
            "<option value=\"\">None / choose a project</option></select>"
            "</label>"
            "<label>Description<textarea id=\"mlPipelineDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Stages, comma-separated<input id=\"mlPipelineStages\" "
            "placeholder=\"e.g. Validate data,Train model,Evaluate model,"
            "Validate model,Safety tests,Request approval,Deploy staging,"
            "Deploy production,Monitor\"></label>"
            "<label>Training/evaluation dataset (for Train model / Evaluate "
            "model / Validate data stages)<select id=\"mlPipelineDatasetId\">"
            "<option value=\"\">None / choose a dataset</option></select>"
            "</label>"
            "<label>Starting model, optional (for stages that need a model "
            "with no prior Train model stage)<select id=\"mlPipelineModelId\">"
            "<option value=\"\">None / choose a model</option></select>"
            "</label>"
            "<button title=\"Create automation pipeline\">" ICON_PLUS_SVG " Create automation pipeline</button></form>"
            "</div><div>"
            "<h2>Automation pipelines</h2>"
            "<div id=\"mlAutomationPipelinesList\">Loading...</div>"
            "<div id=\"mlPipelineRunDetail\"></div>"
            "</div></section>";
    } else if (section == "ml-safety-governance") {
        // Phase 65 (docs/PLAN.md "Machine Learning Abilities" section 40):
        // a governance policy plus per-model disclosure cards -- see
        // SafetyPolicy/ModelCard's class comment in masterai.hpp for the
        // content-scanning fields this scoped-down registry defers.
        body =
            "<section id=\"panel-ml-safety-governance\" class=\"panel\">"
            "<div>"
            "<h2>New safety policy</h2>"
            "<form id=\"newMlSafetyPolicy\">"
            "<label>Name<input id=\"mlSafetyPolicyName\" required "
            "maxlength=\"160\"></label>"
            "<label>Scope<input id=\"mlSafetyPolicyScope\" "
            "placeholder=\"e.g. project name or 'global'\"></label>"
            "<label>Restricted data categories<textarea "
            "id=\"mlSafetyPolicyRestrictedDataCategories\" rows=\"2\" "
            "placeholder=\"e.g. personal information, credentials, "
            "copyrighted text\"></textarea></label>"
            "<button title=\"Create safety policy\">" ICON_PLUS_SVG " Create safety policy</button></form>"
            "</div><div>"
            "<h2>Safety policies</h2>"
            "<div id=\"mlSafetyPoliciesList\">Loading...</div>"
            "</div>"
            "<div>"
            "<h2>New model card</h2>"
            "<form id=\"newMlModelCard\">"
            "<label>Model<select id=\"mlModelCardModelId\" required>"
            "<option value=\"\">Choose a model</option></select></label>"
            "<label>Purpose<textarea id=\"mlModelCardPurpose\" rows=\"2\" "
            "required></textarea></label>"
            "<label>Intended use<textarea id=\"mlModelCardIntendedUse\" "
            "rows=\"2\"></textarea></label>"
            "<label>Prohibited use<textarea id=\"mlModelCardProhibitedUse\" "
            "rows=\"2\"></textarea></label>"
            "<label>Training data reference<input "
            "id=\"mlModelCardTrainingDataReference\"></label>"
            "<label>Evaluation results<textarea "
            "id=\"mlModelCardEvaluationResults\" rows=\"2\"></textarea>"
            "</label>"
            "<label>Known limitations<textarea "
            "id=\"mlModelCardKnownLimitations\" rows=\"2\"></textarea>"
            "</label>"
            "<label>License<input id=\"mlModelCardLicense\"></label>"
            "<button title=\"Create model card\">" ICON_PLUS_SVG " Create model card</button></form>"
            "</div><div>"
            "<h2>Model cards</h2>"
            "<div id=\"mlModelCardsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-monitoring") {
        // Phase 68 (docs/PLAN.md "Machine Learning Abilities" section 44):
        // a read-only aggregation over data other real phases already
        // measured -- no new store, no fabricated numbers. See the
        // /api/v1/ml/monitoring handler's own comment in server.cpp for the
        // exact boundary between what is real here and what remains
        // planned.
        body =
            "<section id=\"panel-ml-monitoring\" class=\"panel\">"
            "<div id=\"mlMonitoringPanel\">"
            "<h2>System resources</h2>"
            "<p>Live CPU/RAM/GPU/disk for the host this MasterAI process is "
            "running on, probed fresh on every page load.</p>"
            "<p id=\"mlMonitoringSystem\">Loading...</p>"
            "<h2>Training job status</h2>"
            "<p>Real status counts from every registered training job.</p>"
            "<div id=\"mlMonitoringTrainingJobs\">Loading...</div>"
            "<h2>Evaluation results</h2>"
            "<p>Genuinely measured metrics from completed Evaluation Lab "
            "runs.</p>"
            "<div id=\"mlMonitoringEvaluations\">Loading...</div>"
            "<h2>Inference throughput (benchmarks)</h2>"
            "<p>Real prompt/generation tokens-per-second from actual "
            "Benchmark runs -- the closest measured inference-performance "
            "evidence this build has. Not live production request "
            "telemetry: requests-per-second, latency percentiles, queue "
            "depth, cache-hit rate, safety-filter rate, tool-call success, "
            "retrieval latency, and model-loading time remain planned, "
            "since no request path in this codebase is currently "
            "instrumented to measure them.</p>"
            "<div id=\"mlMonitoringBenchmarks\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-audit-logs") {
        // Phase 66 (docs/PLAN.md "Machine Learning Abilities" section 43):
        // read-only over the AuditLog every ml.* mutation already writes to
        // -- there is no create/edit form here by design.
        body =
            "<section id=\"panel-ml-audit-logs\" class=\"panel\">"
            "<div>"
            "<h2>Machine Learning audit log</h2>"
            "<p>The most recent 200 recorded Machine Learning administrator "
            "actions, newest first. Every ml.* action across every "
            "interface on this page is recorded here automatically -- "
            "there is nothing to configure.</p>"
            "<div id=\"mlAuditLogsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-settings") {
        // Machine Learning Settings (docs/PLAN.md "Machine Learning
        // Abilities" section 49): reuses the exact #systemConfigForm/
        // #systemConfigStatus machinery the general System Configuration
        // page (settings-config, above) uses -- renderSystemConfig()/
        // submitSystemConfig() read and write by data-path regardless of
        // which subset of fields a given page shows, so this page can
        // scope to only the ML-relevant fields without new JS. Fields not
        // shown here are preserved unchanged on save.
        body =
            "<section id=\"panel-ml-settings\" class=\"panel\">"
            "<div><h2>Machine Learning settings</h2>"
            "<p>The Machine Learning subset of the System Configuration "
            "page. See Settings &gt; System Configuration for every other "
            "server setting.</p>"
            "<form id=\"systemConfigForm\">"
            "<label>Tabular dataset upload limit, bytes &mdash; the "
            "Dataset Manager's CSV content-upload cap"
            "<input id=\"cfgTabularDatasetMaximumCsvBytes\" type=\"number\" "
            "min=\"1\" "
            "data-path=\"machineLearning.tabularDatasetMaximumCsvBytes\">"
            "</label>"
            "<label>Knowledge document upload limit, bytes &mdash; the "
            "Subject Knowledge Manager's ingestion cap"
            "<input id=\"cfgKnowledgeMaximumDocumentBytes\" type=\"number\" "
            "min=\"1\" data-path=\"knowledge.maximumDocumentBytes\"></label>"
            "<label>Parquet helper executable (restart required) &mdash; "
            "the DuckDB CLI path used to convert Parquet knowledge uploads; "
            "leave empty to disable Parquet ingestion"
            "<input id=\"cfgParquetHelperExecutable\" type=\"text\" "
            "data-path=\"knowledge.parquetHelperExecutable\"></label>"
            "<button title=\"Save Machine Learning settings\">" ICON_SAVE_SVG " Save Machine Learning settings</button>"
            "</form>"
            "<p id=\"systemConfigStatus\" role=\"status\"></p>"
            "</div></section>";
    } else if (section == "settings-config") {
        // Phase 30A: administrator-only local-configuration editor backed by
        // GET/POST /api/v1/admin/config (server.cpp's admin_config_get()/
        // admin_config_put()) -- the same settings.json `masterai configure`
        // writes. Every input carries data-path="section.field" matching
        // that JSON document's own shape; application_script()'s
        // renderSystemConfig()/submitSystemConfig() read/write by that path
        // instead of one hand-written line per field. Fields not shown here
        // (host/port, TLS material, storage roots, allow-lists, ...) are
        // preserved unchanged -- the submit handler overlays only the
        // visible fields onto the full document it originally loaded.
        body =
            "<section id=\"panel-settings-config\" class=\"panel\">"
            "<div><h2>System configuration</h2>"
            "<p>Local server configuration. Changes are written to "
            "settings.json immediately; fields marked (restart required) "
            "only take effect the next time MasterAI is started.</p>"
            "<form id=\"systemConfigForm\">"
            "<h3>Hardware</h3>"
            "<label>Accelerator policy"
            "<select id=\"cfgAcceleratorPolicy\" "
            "data-path=\"hardware.acceleratorPolicy\">"
            "<option value=\"auto\">auto (use GPU when available)</option>"
            "<option value=\"cpu_only\">cpu_only (no GPU allocation)</option>"
            "<option value=\"gpu_allowed\">gpu_allowed</option>"
            "</select></label>"
            "<h3>Memory (restart required)</h3>"
            "<label>Resource profile"
            "<select id=\"cfgResourceProfile\" data-path=\"memory.profile\">"
            "<option value=\"minimal\">minimal (one request, smallest "
            "footprint)</option>"
            "<option value=\"balanced\">balanced</option>"
            "<option value=\"performance\">performance</option>"
            "</select></label>"
            "<label>Hard memory limit, MiB (0 = automatic)"
            "<input id=\"cfgMemoryHardLimitMiB\" type=\"number\" min=\"0\" "
            "data-path=\"memory.hardLimitMiB\"></label>"
            "<label>Minimum free RAM percent"
            "<input id=\"cfgMinimumFreePercent\" type=\"number\" min=\"0\" "
            "max=\"50\" data-path=\"memory.minimumFreePercent\"></label>"
            "<label>Critical pressure percent"
            "<input id=\"cfgCriticalPercent\" type=\"number\" min=\"80\" "
            "max=\"99\" data-path=\"memory.criticalPressurePercent\"></label>"
            "<h3>Inference</h3>"
            "<label>Chat context length, tokens"
            "<input id=\"cfgChatContextLength\" type=\"number\" min=\"1\" "
            "data-path=\"inference.chatContextLength\"></label>"
            "<label>Chat maximum reply tokens"
            "<input id=\"cfgChatMaxReplyTokens\" type=\"number\" min=\"1\" "
            "data-path=\"inference.chatMaxReplyTokens\"></label>"
            "<label>Runner startup timeout, seconds (restart required)"
            "<input id=\"cfgStartupTimeout\" type=\"number\" min=\"1\" "
            "data-path=\"inference.startupTimeoutSeconds\"></label>"
            "<label>Runner stall timeout, seconds"
            "<input id=\"cfgStallTimeout\" type=\"number\" min=\"1\" "
            "data-path=\"inference.stallTimeoutSeconds\"></label>"
            "<h3>Storage</h3>"
            "<label>PageFile location (restart required) &mdash; a folder "
            "MasterAI uses instead of the system pagefile/temp area for its "
            "own disk-backed cache and model data; leave empty to use the "
            "default cache folder"
            "<input id=\"cfgPageFileRoot\" type=\"text\" "
            "placeholder=\"(default cache folder)\" "
            "data-path=\"storage.pageFileRoot\"></label>"
            "<h3>Retrieval</h3>"
            "<label class=\"checkboxLabel\"><input id=\"cfgRetrievalEnabled\" "
            "type=\"checkbox\" data-path=\"retrieval.enabled\" "
            "data-type=\"bool\"> Retrieval enabled</label>"
            "<label>Retrieval deadline, milliseconds"
            "<input id=\"cfgRetrievalDeadline\" type=\"number\" min=\"1\" "
            "data-path=\"retrieval.deadlineMilliseconds\"></label>"
            "<label>Retrieval maximum context, bytes"
            "<input id=\"cfgRetrievalMaxContext\" type=\"number\" min=\"1\" "
            "data-path=\"retrieval.maximumContextBytes\"></label>"
            "<label>Retrieval maximum chunks per source"
            "<input id=\"cfgRetrievalMaxChunksPerSource\" type=\"number\" "
            "min=\"1\" data-path=\"retrieval.maximumChunksPerSource\"></label>"
            "<label>Retrieval maximum total chunks"
            "<input id=\"cfgRetrievalMaxTotalChunks\" type=\"number\" "
            "min=\"1\" data-path=\"retrieval.maximumTotalChunks\"></label>"
            "<h3>Cache</h3>"
            "<label class=\"checkboxLabel\"><input id=\"cfgCacheEnabled\" "
            "type=\"checkbox\" data-path=\"cache.enabled\" "
            "data-type=\"bool\"> Retrieval/prompt cache enabled</label>"
            "<h3>Session reuse</h3>"
            "<label class=\"checkboxLabel\"><input id=\"cfgSessionEnabled\" "
            "type=\"checkbox\" data-path=\"session.enabled\" "
            "data-type=\"bool\"> Prompt-prefix/KV session reuse enabled"
            "</label>"
            "<label>Maximum reusable slots (restart required)"
            "<input id=\"cfgSessionMaxSlots\" type=\"number\" min=\"1\" "
            "data-path=\"session.maxSlots\"></label>"
            "<label>Idle retention, seconds (restart required)"
            "<input id=\"cfgSessionIdleRetention\" type=\"number\" min=\"1\" "
            "data-path=\"session.idleRetentionSeconds\"></label>"
            "<h3>Performance</h3>"
            "<label class=\"checkboxLabel\"><input id=\"cfgAutoTune\" "
            "type=\"checkbox\" data-path=\"performance.autoTune\" "
            "data-type=\"bool\"> Automatic calibration enabled</label>"
            "<h3>Sign-in</h3>"
            "<label class=\"checkboxLabel\"><input "
            "id=\"cfgAllowLocalPasswordAccounts\" type=\"checkbox\" "
            "data-path=\"auth.allowLocalPasswordAccounts\" "
            "data-type=\"bool\"> Allow locally stored password accounts"
            "</label>"
            "<label class=\"checkboxLabel\"><input "
            "id=\"cfgAllowOsIdentityAccounts\" type=\"checkbox\" "
            "data-path=\"auth.allowOsIdentityAccounts\" data-type=\"bool\"> "
            "Allow OS-integrated sign-in</label>"
            "<h3>Server</h3>"
            "<label>Rate limit, requests per minute"
            "<input id=\"cfgRateLimit\" type=\"number\" min=\"1\" "
            "data-path=\"server.rateLimitPerMinute\"></label>"
            "<label>Maximum request size, bytes"
            "<input id=\"cfgMaxRequestBytes\" type=\"number\" min=\"1024\" "
            "data-path=\"server.maxRequestBytes\"></label>"
            "<button title=\"Save configuration\">" ICON_SAVE_SVG " Save configuration</button>"
            "</form>"
            "<p id=\"systemConfigStatus\" role=\"status\"></p>"
            "</div></section>";
    } else if (section == "report-system") {
        body =
            "<section id=\"panel-report-system\" class=\"panel\">"
            "<div><h2>System Report</h2>"
            "<p>A consolidated, point-in-time read of this MasterAI "
            "instance: hardware, memory pressure, the system pagefile, the "
            "MasterAI-scoped virtual PageFile, and which features are "
            "currently enabled.</p>"
            "<div id=\"systemReport\">Loading...</div>"
            "</div></section>";
    } else if (section == "performance") {
        body =
            "<section id=\"panel-performance\" class=\"panel\">"
            "<div><h2>Performance</h2>"
            "<p>Live visibility and manual override for the local runner "
            "pool (Phase 33), the intranet worker pool (Phase 33), and the "
            "adaptive performance controller (Phase 34). Every figure below "
            "comes directly from the same routes those phases expose -- "
            "nothing here is a separately maintained display-only value.</p>"
            "<div class=\"reportSection\"><h3>Adaptive controller</h3>"
            "<div id=\"perfAdaptive\">Loading...</div>"
            "<form id=\"perfModeForm\"><label>Mode<select id=\"perfMode\">"
            "<option value=\"automatic\">Automatic</option>"
            "<option value=\"balanced\">Balanced</option>"
            "<option value=\"minimal_memory\">Minimal Memory</option>"
            "<option value=\"lowest_latency\">Lowest Latency</option>"
            "<option value=\"maximum_throughput\">Maximum Throughput</option>"
            "<option value=\"battery_saver\">Battery Saver</option>"
            "<option value=\"quiet_thermal_conservative\">"
            "Quiet/Thermal Conservative</option>"
            "<option value=\"administrator_custom\">Administrator Custom"
            "</option></select></label>"
            "<button type=\"submit\" title=\"Apply performance mode\">" ICON_SAVE_SVG
            " Apply mode</button>"
            "<button type=\"button\" id=\"perfRollback\" "
            "title=\"Revert the most recent automatic change\">Rollback last "
            "change</button></form>"
            "</div>"
            "<div class=\"reportSection\"><h3>Local runner pool</h3>"
            "<div id=\"perfRunnerPool\">Loading...</div></div>"
            "<div class=\"reportSection\"><h3>Intranet worker pool</h3>"
            "<div id=\"perfWorkerPool\">Loading...</div></div>"
            "</div></section>"
            // Self-contained: fetched and rendered independently of the
            // page's shared load() pipeline (see fetchFor()'s own comment
            // on why that shared Promise.all only fetches elements present
            // on the current page) -- this page's placeholders above are
            // only ever present when section=='performance', so a plain,
            // unconditional api() call here never fires on any other page.
            "<script>(function(){"
            "function row(label,value){return '<tr><td class=\"reportLabel\">'+"
            "label+'</td><td class=\"reportValue\">'+value+'</td></tr>';}"
            "function renderAdaptive(r){const el=q('#perfAdaptive');if(!el)return;"
            "let html='<table><tbody>'+"
            "row('Active mode',r.activeMode)+row('Selected mode',r.selectedMode)+"
            "row('Applied this cycle',r.applied.length)+"
            "row('Proposed (not yet applied)',r.proposedNotYetApplied.length)+"
            "'</tbody></table>';"
            "if(r.applied.length){html+='<h4>Applied</h4><ul>'+r.applied.map(a=>"
            "'<li>'+a.parameter+': '+a.previousValue+' \\u2192 '+a.proposedValue+"
            "' ('+a.reason+')</li>').join('')+'</ul>';}"
            "if(r.proposedNotYetApplied.length){html+='<h4>Recommended</h4><ul>'+"
            "r.proposedNotYetApplied.map(a=>'<li>'+a.parameter+': '+"
            "a.previousValue+' \\u2192 '+a.proposedValue+' ('+a.reason+"
            "', confidence '+Math.round(a.confidence*100)+'%)</li>').join('')+"
            "'</ul>';}"
            "el.innerHTML=html;q('#perfMode').value=r.activeMode;}"
            "function renderPool(elementId,list,noun){const el=q(elementId);"
            "if(!el)return;if(!list.length){el.textContent='No '+noun+' configured.';"
            "return;}"
            "el.innerHTML='<table><thead><tr><th>Id</th><th>State</th>"
            "<th>Model</th><th>Healthy</th><th>Failures</th></tr></thead><tbody>'+"
            "list.map(x=>'<tr><td>'+x.id+'</td><td>'+x.state+'</td><td>'+"
            "(x.modelId||'')+'</td><td>'+(x.healthy?'yes':'no')+'</td><td>'+"
            "x.consecutiveFailures+'</td></tr>').join('')+'</tbody></table>';}"
            "async function refresh(){"
            "try{renderAdaptive(await api('/api/v1/performance/adaptive'));}"
            "catch(e){}"
            "try{const p=await api('/api/v1/runner/pool');"
            "renderPool('#perfRunnerPool',p.runners,'local runners');}catch(e){}"
            "try{const w=await api('/api/v1/worker/pool');"
            "renderPool('#perfWorkerPool',w.workers,'intranet workers');}catch(e){}"
            "}"
            "if(q('#panel-performance')){refresh();"
            "const form=q('#perfModeForm');"
            "if(form)form.addEventListener('submit',async(e)=>{e.preventDefault();"
            "try{await api('/api/v1/performance/adaptive/mode','POST',"
            "{mode:q('#perfMode').value});await refresh();}"
            "catch(err){showSystemError(err.message);}});"
            "const rollback=q('#perfRollback');"
            "if(rollback)rollback.addEventListener('click',async()=>{"
            "try{await api('/api/v1/performance/adaptive/rollback','POST',{});"
            "await refresh();}catch(err){showSystemError(err.message);}});}"
            "})();</script>";
    } else if (section == "admin-create") {
        body =
            "<section id=\"panel-admin-create\" class=\"panel\">"
            "<div><h2>Create user</h2>"
            "<form id=\"newUser\"><label>Username"
            "<input id=\"newUserName\" required "
            "pattern=\"[A-Za-z0-9_.-]+\"></label>"
            "<label>Display name<input id=\"newUserDisplay\" required></label>"
            "<label>Role<select id=\"newUserRole\">"
            "<option value=\"administrator\">administrator</option>"
            "<option value=\"developer\">developer</option>"
            "<option value=\"viewer\">viewer</option></select></label>"
            "<label>Password (minimum 8 characters)"
            "<input id=\"newUserPassword\" type=\"password\" "
            "minlength=\"8\" required></label>"
            "<button title=\"Create local account\">" ICON_PLUS_SVG " Create local account</button></form>"
            "</div></section>";
    } else if (section == "admin-users") {
        body =
            "<section id=\"panel-admin-users\" class=\"panel\">"
            "<div><h2>User list</h2>"
            "<div id=\"usersList\">Loading...</div></div></section>";
    }

    const std::string memory_controls =
        "<details id=\"chatMemorySection\"><summary>Memory</summary>"
        "<p class=\"memoryHelp\">Saved details are available to every model. "
        "Type <code>save to memory: your detail</code> in chat, or manage them "
        "here.</p>" +
        std::string(can_write_chat
                        ? "<form id=\"newMemory\"><label for=\"memoryContent\">"
                          "Detail to remember</label><textarea id=\"memoryContent\" "
                          "rows=\"2\" maxlength=\"512\" required></textarea>"
                          "<button title=\"Save detail\">" ICON_SAVE_SVG " Save detail</button></form>"
                        : "") +
        "<div id=\"memoryList\">Loading...</div></details>";

    std::string sidebar_links =
        nav_link("/app", "+ New chat", section == "chat" && chat_id.empty()) +
        sidebar_section(
            "chats", "Chats",
            "<div id=\"chatList\"></div>"
            // Older conversations (anything past the most recent 20) render
            // here instead, so the primary list above stays a fixed,
            // scannable size. Hidden by default; renderChatList() reveals
            // it once there is anything to show.
            "<details id=\"chatHistorySection\" hidden><summary>History</summary>"
            "<div id=\"chatHistoryList\"></div></details>" + memory_controls);
    if (can_manage_settings) {
        // Administrator-only entries (system configuration, user
        // management) are appended to this same Settings group instead of
        // their own separate "Admin" sidebar section -- Settings is already
        // gated to developer+administrator, so it needed a permission
        // boundary inside the group either way, and one settings-shaped
        // destination is easier for an administrator to find than two
        // sibling groups that both hold configuration. Every entry still
        // enforces its own server-side permission check regardless of what
        // this sidebar renders (see the route handlers), so a developer who
        // guesses one of these URLs directly still gets redirected/403'd.
        std::string settings_links =
            nav_link("/app/models/inventory", "Model inventory",
                     section == "models-inventory") +
            nav_link("/app/models/download", "Download a model",
                     section == "models-download") +
            nav_link("/app/models/benchmarks", "Benchmarks",
                     section == "models-benchmarks");
        if (is_administrator) {
            settings_links +=
                nav_link("/app/settings/config", "System configuration",
                         section == "settings-config") +
                nav_link("/app/admin/create", "Create user",
                         section == "admin-create") +
                nav_link("/app/admin/users", "User list",
                         section == "admin-users");
        }
        sidebar_links +=
            sidebar_section("workspace", "Workspace",
                            nav_link("/app/projects", "Projects",
                                     section == "projects")) +
            sidebar_section("settings", "Settings", settings_links);
    }
    if (is_administrator) {
        sidebar_links += sidebar_section(
            "report", "Report",
            nav_link("/app/report/system", "System Report",
                     section == "report-system"));
        // Phase 35: Performance administration -- overview, local runner
        // pool (Phase 33), intranet worker pool (Phase 33), and the
        // adaptive controller (Phase 34), each backed by the real routes
        // those phases already exposed.
        sidebar_links += sidebar_section(
            "performance", "Performance",
            nav_link("/app/performance", "Overview", section == "performance"));
        // Ordered to match the chronological order of actually building a
        // model -- an administrator works top to bottom, project through
        // deployment/monitoring, with the two packaging/inspection steps
        // that only make sense once a model exists ("Model Builder", which
        // produces the finished GGUF, and "Model Registry", which reports
        // on it) placed last rather than grouped near the top.
        sidebar_links += sidebar_section(
            "ml", "Machine Learning",
            nav_link("/app/ml", "Dashboard", section == "ml-dashboard") +
                nav_link("/app/ml/projects", "Projects",
                         section == "ml-projects") +
                nav_link("/app/ml/datasets", "Dataset Manager",
                         section == "ml-datasets") +
                nav_link("/app/ml/subjects", "Subject Knowledge Manager",
                         section == "ml-subjects") +
                nav_link("/app/ml/label-tasks", "Data Labeling",
                         section == "ml-label-tasks") +
                nav_link("/app/ml/prep-jobs", "Data Preparation",
                         section == "ml-prep-jobs") +
                nav_link("/app/ml/synthetic-records",
                         "Synthetic Data Generation",
                         section == "ml-synthetic-records") +
                nav_link("/app/ml/instruction-examples",
                         "Prompt and Instruction Training",
                         section == "ml-instruction-examples") +
                nav_link("/app/ml/vector-stores",
                         "Embeddings and Vector Stores",
                         section == "ml-vector-stores") +
                nav_link("/app/ml/rag-configs",
                         "Retrieval-Augmented Generation",
                         section == "ml-rag-configs") +
                nav_link("/app/ml/hyperparameter-searches",
                         "Hyperparameter Optimization",
                         section == "ml-hyperparameter-searches") +
                nav_link("/app/ml/training-jobs", "Training Jobs",
                         section == "ml-training-jobs") +
                nav_link("/app/ml/fine-tuning-jobs", "Fine-Tuning",
                         section == "ml-fine-tuning-jobs") +
                nav_link("/app/ml/checkpoints",
                         "Checkpoint Management",
                         section == "ml-checkpoints") +
                nav_link("/app/ml/evaluation-runs", "Evaluation Lab",
                         section == "ml-evaluation-runs") +
                nav_link("/app/ml/experiments", "Experiment Tracking",
                         section == "ml-experiments") +
                nav_link("/app/ml/subject-exams",
                         "Subject Examination",
                         section == "ml-subject-exams") +
                nav_link("/app/ml/model-optimizations",
                         "Model Optimization",
                         section == "ml-model-optimizations") +
                nav_link("/app/ml/model-comparisons",
                         "Model Comparison",
                         section == "ml-model-comparisons") +
                nav_link("/app/ml/safety-governance",
                         "Safety and Governance",
                         section == "ml-safety-governance") +
                nav_link("/app/ml/deployments",
                         "Deployment Manager",
                         section == "ml-deployments") +
                nav_link("/app/ml/inference-endpoints",
                         "Inference Endpoints",
                         section == "ml-inference-endpoints") +
                nav_link("/app/ml/compute-nodes",
                         "Hardware and Compute",
                         section == "ml-compute-nodes") +
                nav_link("/app/ml/automation-pipelines",
                         "Automation Pipelines",
                         section == "ml-automation-pipelines") +
                nav_link("/app/ml/model-builder-configs", "Model Builder",
                         section == "ml-model-builder-configs"));
        // Logs, settings, and statistics aren't build-pipeline steps -- an
        // administrator visits them to check on or configure the system,
        // not as part of working through a model build in order -- so they
        // get their own tree instead of trailing the pipeline above.
        sidebar_links += sidebar_section(
            "ml-admin", "Machine Learning Logs and Settings",
            nav_link("/app/ml/monitoring", "Monitoring and Diagnostics",
                     section == "ml-monitoring") +
                nav_link("/app/ml/audit-logs", "Audit Logs",
                         section == "ml-audit-logs") +
                nav_link("/app/ml/settings", "Machine Learning Settings",
                         section == "ml-settings") +
                nav_link("/app/ml/models", "Model Registry (Statistics)",
                         section == "ml-models"));
    }

    return html_response(
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>MasterAI Workspace</title><style>" DARK_THEME_CSS
        "body{max-width:none;padding:0}"
        // #shell is pinned to exactly one viewport height (not just a
        // min-height) so #content's flex children -- #actionStatus plus
        // whichever section body follows it -- have a real bounded box to
        // divide up. Without this, #chatShell's own flex:1 below has no
        // fixed ancestor height to size against, and the composer ends up
        // pushed below the fold instead of pinned to the bottom of the
        // visible window.
        "#shell{display:flex;height:100vh}"
        "#sidebar{width:16rem;flex:none;padding:1.25rem 1rem;"
        "border-right:1px solid var(--panel-border);"
        "display:flex;flex-direction:column;gap:.5rem;overflow-y:auto}"
        // A thin draggable gutter between the sidebar and the content pane
        // -- the sidebar script persists the chosen width (and the
        // sidebar's scroll position) in localStorage so both survive the
        // full-page navigation every sidebar click performs.
        "#sidebarResizer{flex:none;width:6px;cursor:col-resize;"
        "background:transparent;touch-action:none}"
        "#sidebarResizer:hover,#sidebarResizer.dragging{"
        "background:var(--panel-border)}"
        "#sidebar h1{font-size:1.3rem}"
        "#sidebar h3{margin-top:1rem}"
        // Collapsible sidebar section groups (Chats/Workspace/Settings/
        // Machine Learning/Admin) replace the old plain <h3> group
        // headings -- the marker plus summary text takes the same visual
        // role the h3 used to, but a click now shrinks the whole group away
        // instead of just being a label.
        ".sidebarSection{margin-top:1rem}"
        ".sidebarSection summary{font-weight:600;cursor:pointer;"
        "list-style-position:outside;padding:.15rem 0;user-select:none}"
        ".sidebarSection summary:hover{color:var(--text)}"
        ".sidebarSection>div{margin-top:.15rem}"
        // The content pane spans the full remaining browser width (no fixed
        // max-width cap), so every panel stretches consistently to the
        // window at any browser size -- the .panel grid below re-flows its
        // cards to fill whatever width this yields, and the chat page keeps
        // its own comfortable reading cap via #chatShell's max-width.
        // min-width:0 overrides the flex-item default of min-width:auto,
        // which sizes to the widest child's intrinsic (unwrapped) width --
        // without this, a wide ML table pushed #content (and #shell with
        // it) past the viewport instead of scrolling inside its own box.
        "#content{flex:1;min-width:0;padding:1.5rem;overflow-y:auto;"
        "display:flex;flex-direction:column;min-height:0}"
        "#actionStatus{margin-bottom:1rem;flex:none}"
        // An empty status line still reserved a full line of height (see the
        // global #actionStatus,#status min-height rule) even though it had
        // nothing to show -- on the chat page that shrank the message area
        // for no visible reason, so give the space back when there's no
        // status text.
        "#actionStatus:empty{margin-bottom:0;min-height:0}"
        ".navButton{display:block;background:transparent;color:var(--text);"
        "border:1px solid transparent;text-align:left;margin-top:.15rem;"
        "padding:.5rem .6rem;text-decoration:none}"
        ".navButton:hover{background:var(--panel)}"
        ".navButton.active{background:var(--panel);border-color:var(--panel-border)}"
        ".chatListItem{display:flex;align-items:center;gap:.15rem}"
        ".chatListItem .navButton{flex:1;min-width:0;margin-top:0;overflow:hidden;"
        "text-overflow:ellipsis;white-space:nowrap}"
        ".chatDeleteBtn{flex:none;width:1.8rem;height:1.8rem;padding:0;"
        "display:flex;align-items:center;justify-content:center;"
        "background:transparent;border:1px solid transparent;border-radius:.4rem;"
        "color:var(--muted);line-height:1;cursor:pointer}"
        ".chatDeleteBtn:hover{background:var(--panel);border-color:var(--panel-border);"
        "color:#e5657a}"
        // Compact icon-button toolbar used across every Machine Learning
        // list (status pickers, approve/reject/delete actions) in place of
        // full-width text buttons -- keeps each row a single tidy line
        // instead of stretching table cells and wrapping under the window.
        // nowrap keeps every row's action buttons on one line -- wrap let
        // a cramped column fold the second/third button onto its own line,
        // which is the "toolbar buttons need to be side by side" bug;
        // table() already wraps the whole table in an overflow-x:auto div,
        // so a genuinely too-narrow row scrolls instead of stacking.
        ".rowToolbar{display:flex;align-items:center;gap:.3rem;flex-wrap:nowrap}"
        ".rowToolbar select{max-width:11rem}"
        // margin-top:0 overrides the generic button{margin-top:.75rem}
        // rule (see DARK_THEME_CSS) that otherwise pushes every icon
        // button down out of vertical center with the select/text next to
        // it in the same .rowToolbar row.
        ".iconBtn{flex:none;width:1.8rem;height:1.8rem;padding:0;margin-top:0;"
        "display:inline-flex;align-items:center;justify-content:center;"
        "background:transparent;border:1px solid var(--panel-border);"
        "border-radius:.4rem;color:var(--muted);line-height:1;cursor:pointer}"
        // The global "button svg{margin-right:.4rem}" rule (see
        // DARK_THEME_CSS) exists to space an icon from the visible label
        // text that follows it on full-width form buttons -- iconBtn
        // buttons carry no label text, so that same margin just shoves the
        // icon off-center inside the square button. Zero it here.
        ".iconBtn svg{margin-right:0}"
        ".iconBtn:hover{background:var(--panel);color:var(--text)}"
        ".iconBtn-apply:hover{color:#4caf6a;border-color:#4caf6a}"
        ".iconBtn-delete:hover{color:#e5657a;border-color:#e5657a}"
        // Table cells hold either short controls or long free-text/id
        // values; cap width and ellipsize the latter (escTrim already
        // shortens the text itself, this is the belt-and-suspenders CSS
        // backstop for values that arrive unexpectedly long) so a wide
        // table never forces the page past the window boundary -- the
        // surrounding div.style=overflow-x:auto in table() still scrolls
        // as a last resort, but should rarely be needed now.
        "table td,table th{max-width:16rem;overflow:hidden;"
        "text-overflow:ellipsis}"
        "table td:has(.rowToolbar),table td:has(select),table td:has(button)"
        "{overflow:visible;white-space:normal}"
        "#chatList{display:flex;flex-direction:column;gap:.15rem;max-height:14rem;"
        "overflow-y:auto}"
        "#chatHistorySection{margin-top:1rem}"
        "#chatHistorySection summary{color:var(--muted);font-size:.85rem;"
        "text-transform:uppercase;letter-spacing:.06em;cursor:pointer}"
        "#chatHistoryList{display:flex;flex-direction:column;gap:.15rem;"
        "max-height:14rem;overflow-y:auto;margin-top:.4rem}"
        "#chatMemorySection{margin-top:1rem}"
        "#chatMemorySection summary{color:var(--muted);font-size:.85rem;"
        "text-transform:uppercase;letter-spacing:.06em;cursor:pointer}"
        ".memoryHelp,.memoryEmpty{color:var(--muted);font-size:.75rem;"
        "line-height:1.4;margin:.45rem 0}"
        "#newMemory label{margin-top:.4rem}"
        "#newMemory textarea{font-size:.78rem;resize:vertical}"
        "#newMemory button{font-size:.78rem;padding:.4rem;margin-top:.4rem}"
        "#memoryList{display:flex;flex-direction:column;gap:.3rem;"
        "max-height:12rem;overflow-y:auto;margin-top:.55rem}"
        ".memoryItem{display:flex;align-items:flex-start;gap:.3rem;"
        "padding:.4rem;border:1px solid var(--panel-border);border-radius:.4rem;"
        "font-size:.75rem;line-height:1.35}"
        ".memoryItem span{flex:1;min-width:0;overflow-wrap:anywhere}"
        ".memoryDeleteBtn{flex:none;width:1.4rem;height:1.4rem;margin:0;padding:0;"
        "background:transparent;color:var(--muted);line-height:1}"
        ".memoryDeleteBtn:hover{background:#3a0a0a;color:#ffd54a}"
        ".panel{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));"
        "gap:1.25rem}"
        // Same min-width:auto default problem one level down: a grid track
        // otherwise still grows to fit a wide table inside it instead of
        // letting the table's own overflow-x scrollbar (see table()) do
        // the job, so a single wide ML card could blow out the whole row.
        ".panel>div{min-width:0}"
        // Every Machine Learning panel that pairs a form with a list
        // (:has(>div:nth-child(2)) excludes the handful of ML panels that
        // are just a single list/settings card, e.g. ml-audit-logs,
        // ml-settings) was giving the narrow form column the same 1fr
        // share as the list column, so a list's table -- often the widest
        // thing on the page -- got squeezed into half the panel width.
        // Capping the form column keeps it comfortably readable while
        // handing the rest of the row to the list.
        "section[id^=\"panel-ml-\"].panel:has(>div:nth-child(2)){"
        "grid-template-columns:minmax(260px,22rem) minmax(0,1fr)}"
        "@media (max-width:900px){"
        "section[id^=\"panel-ml-\"].panel:has(>div:nth-child(2)){"
        "grid-template-columns:1fr}}"
        // Chat gets its own full-height flex column (centered empty-state
        // greeting, growing history, a pill composer pinned to the bottom)
        // instead of the generic multi-card grid every settings page uses,
        // so it reads like a normal chat client rather than a forms screen.
        "#chatShell{display:flex;flex-direction:column;flex:1;min-height:0;"
        "max-width:64rem;margin:0 auto;width:100%}"
        "#chatTopBar{margin-bottom:.5rem}"
        "#chatTopBar label{display:inline-flex;align-items:center;gap:.5rem;"
        "margin:0;color:var(--muted);font-size:.8rem;width:auto}"
        "#chatTopBar select{width:auto;padding:.3rem .5rem}"
        // The [hidden] attribute alone can't win here: this ID selector's own
        // display:flex outranks the UA stylesheet's [hidden]{display:none},
        // so the greeting stayed visible even after openChat() hid it. The
        // explicit :not([hidden]) rule below is what actually applies the
        // flex layout, leaving [hidden] free to hide it as normal.
        "#chatEmpty:not([hidden]){display:flex;align-items:center;"
        "justify-content:center;text-align:center}"
        "#chatEmpty{flex:1}"
        "#chatEmpty h1{font-size:1.8rem}"
        "#chatMessages{flex:1;overflow-y:auto;padding:.25rem .6rem;"
        "display:flex;flex-direction:column;gap:.35rem}"
        "#chatMessages:empty{flex:0}"
        ".chatMsg{position:relative;max-width:80%;margin:0;padding:.45rem .8rem;"
        "line-height:1.45;border-radius:.9rem;overflow-wrap:anywhere}"
        // Copy button sits in the bubble's own corner and stays out of the
        // way until hovered/focused -- matches the rest of the chat UI
        // staying uncluttered while every message and code block still
        // gets one.
        ".msgCopyBtn{position:absolute;top:.35rem;right:.5rem;margin:0;"
        "width:auto;padding:.15rem .5rem;font-size:.7rem;font-weight:600;"
        "border-radius:.4rem;border:1px solid var(--panel-border);"
        "background:rgba(0,0,0,.25);color:inherit;opacity:0;"
        "transition:opacity .15s}"
        ".chatMsg:hover>.msgCopyBtn,.msgCopyBtn:focus{opacity:1}"
        ".chatMsg-user>.msgCopyBtn{background:rgba(0,0,0,.25);color:#fff}"
        ".chatMsg-user{margin-left:auto;margin-right:.6rem;background:#3a3a3e;"
        "border:1px solid #505055;color:#fff}"
        ".chatMsg-assistant{margin-right:auto;background:var(--panel);"
        "border:1px solid var(--panel-border);color:#8fe6c9}"
        ".chatMsg-responseTitle{font-weight:800;letter-spacing:.03em;"
        "margin-bottom:.25rem;opacity:.85}"
        ".chatMsg-system{margin:0 auto;color:var(--muted);font-style:italic;"
        "background:none;white-space:pre-wrap}"
        ".chatMsg-error{background:#3a0a0a;border:1px solid #ffd54a;color:#ffd54a}"
        ".chatMsg-errorTitle{font-weight:800;letter-spacing:.03em;"
        "margin-bottom:.25rem}"
        ".chatMsg-modelChange{margin:0 auto;background:rgba(40,167,69,.12);"
        "border:1px solid rgba(40,167,69,.5);color:#4ade80;font-size:.82rem;"
        "text-align:center}"
        // Rendered Markdown structure inside a bubble: paragraphs/lists need
        // their own spacing since the bubble itself no longer relies on
        // white-space:pre-wrap for line breaks (renderMarkdown() emits real
        // <p>/<br>/<ul> elements instead).
        ".chatMsg p{margin:0 0 .5rem}.chatMsg>*:last-child{margin-bottom:0}"
        ".chatMsg ul,.chatMsg ol{margin:0 0 .5rem;padding-left:1.3rem}"
        ".chatMsg li{margin:.15rem 0}"
        ".chatMsg code{background:rgba(0,0,0,.25);border-radius:.25rem;"
        "padding:.1rem .3rem;font-size:.85em;font-family:ui-monospace,"
        "SFMono-Regular,Consolas,monospace}"
        // overflow-x/overflow-y are both set explicitly (rather than relying
        // on the shorthand from the generic pre{} rule above cascading in
        // for the axis this rule doesn't mention) so a long reply's code
        // block is unambiguously scrollable on both axes instead of being
        // clipped. overscroll-behavior stops the scroll from chaining into
        // the outer #chatMessages once the block's own scroll hits its end,
        // which otherwise makes it feel like the block itself won't scroll.
        ".chatMsg pre{position:relative;margin:0 0 .5rem;background:#0e0e11;"
        "border:1px solid var(--panel-border);border-radius:.5rem;"
        "padding:.6rem .75rem;overflow-x:auto;overflow-y:auto;max-height:24rem;"
        "overscroll-behavior:contain;scrollbar-width:thin}"
        ".chatMsg pre::-webkit-scrollbar{width:.5rem;height:.5rem}"
        ".chatMsg pre::-webkit-scrollbar-thumb{background:var(--panel-border);"
        "border-radius:.5rem}"
        ".chatMsg pre code{background:none;padding:0;color:#c8c8d4;"
        "white-space:pre}"
        // Same corner-button treatment as .msgCopyBtn, but scoped to the
        // individual code block so a snippet can be copied without the
        // surrounding reply -- visible on hovering the block itself,
        // independent of whether the whole bubble is hovered.
        ".codeCopyBtn{position:absolute;top:.4rem;right:.5rem;margin:0;"
        "width:auto;padding:.15rem .5rem;font-size:.7rem;font-weight:600;"
        "border-radius:.4rem;border:1px solid var(--panel-border);"
        "background:rgba(255,255,255,.08);color:#c8c8d4;opacity:0;"
        "transition:opacity .15s}"
        ".chatMsg pre:hover>.codeCopyBtn,.codeCopyBtn:focus{opacity:1}"
        // Best-effort inline/block math (see renderMathExpr()): a plain
        // serif-leaning span for inline expressions, a centered block for
        // \\[...\\] with a little breathing room above/below.
        ".math{font-family:'Cambria Math',ui-serif,Georgia,serif}"
        ".mathBlock{font-family:'Cambria Math',ui-serif,Georgia,serif;"
        "text-align:center;margin:.5rem 0;padding:.3rem 0}"
        // A hand-rolled fraction: numerator over denominator with a rule
        // between them, since there is no TeX engine here to lay one out.
        ".frac{display:inline-flex;flex-direction:column;vertical-align:middle;"
        "text-align:center;margin:0 .15em;line-height:1.1}"
        ".fracNum,.fracDen{display:block;padding:0 .2em}"
        ".fracNum{border-bottom:1px solid currentColor}"
        // The "Thinking..." placeholder shown in the assistant bubble from
        // the moment a message is sent until the first token streams back
        // (see streamMessage()) -- a spinner plus label instead of an
        // empty-looking bubble, since reasoning-heavy models can take a
        // real amount of time before their first visible token.
        ".chatThinking{display:inline-flex;align-items:center;gap:.5rem;"
        "color:var(--muted)}"
        ".chatThinkingSpinner{width:.9rem;height:.9rem;flex:none;"
        "border-radius:50%;border:2px solid currentColor;"
        "border-top-color:transparent;animation:chatThinkingSpin .7s linear infinite}"
        "@keyframes chatThinkingSpin{to{transform:rotate(360deg)}}"
        // The composer: a single rounded pill carrying the attach toggle,
        // the message box, the model picker, and send/cancel -- no separate
        // "start chat" form above it.
        ".composer{position:relative;display:flex;align-items:center;gap:.4rem;"
        "background:var(--panel);border:1px solid var(--panel-border);"
        "border-radius:1.5rem;padding:.4rem .5rem .4rem 1rem}"
        // Floats above the composer pill (anchored to its bottom-right
        // corner via the parent's position:relative above) instead of
        // pushing the message list around when it opens.
        ".modelSettingsPanel{position:absolute;bottom:100%;right:0;"
        "margin-bottom:.5rem;width:14rem;background:var(--panel);"
        "border:1px solid var(--panel-border);border-radius:.75rem;"
        "padding:.75rem .9rem;box-shadow:0 8px 24px rgba(0,0,0,.4);"
        "display:flex;flex-direction:column;gap:.4rem;z-index:20}"
        // display:flex above beats the browser's default display:none for
        // [hidden] (author styles win over the UA stylesheet), so without
        // this the panel showed regardless of its hidden attribute --
        // including on page load and after the close button set it.
        ".modelSettingsPanel[hidden]{display:none}"
        ".modelSettingsPanel label{margin-top:.3rem}"
        ".modelSettingsPanelHeader{display:flex;align-items:center;"
        "justify-content:space-between;font-size:.75rem;color:var(--muted);"
        "text-transform:uppercase;letter-spacing:.05em}"
        ".modelSettingsClose{width:auto;margin:0;padding:0 .3rem;"
        "background:transparent;color:var(--muted);font-weight:700;"
        "border:none;cursor:pointer}"
        ".modelSettingsNote{margin:.2rem 0 0;font-size:.75rem;"
        "color:var(--muted)}"
        ".composerInput{flex:1;border:none;background:transparent;resize:none;"
        "max-height:8rem;padding:.6rem 0;margin:0;box-shadow:none}"
        ".composerInput:focus{box-shadow:none}"
        ".composerModelPicker{width:auto;max-width:11rem;border:none;"
        "background:transparent;color:var(--muted);font-size:.8rem;margin:0;"
        "padding:.3rem .4rem}"
        ".composerIconBtn,.composerSendBtn{width:2.25rem;height:2.25rem;flex:none;"
        "border-radius:50%;padding:0;margin:0;font-size:1.15rem;line-height:1}"
        ".composerIconBtn{background:transparent;color:var(--muted);"
        "border:1px solid var(--panel-border)}"
        ".composerIconBtn:hover{background:var(--bg)}"
        ".visuallyHidden{position:absolute;width:1px;height:1px;padding:0;"
        "margin:-1px;overflow:hidden;clip:rect(0,0,0,0);border:0}"
        // Pending attachments render as removable chips between the message
        // history and the composer, mirroring the mainstream chat clients
        // this is meant to match -- the file itself has already been
        // uploaded by the time its chip appears (see attachFile()), so
        // removing a chip only drops it from this message, not the store.
        ".attachChips{display:flex;flex-wrap:wrap;gap:.4rem}"
        ".attachChips:empty{display:none}"
        ".attachChip{display:inline-flex;align-items:center;gap:.4rem;"
        "background:var(--panel);border:1px solid var(--panel-border);"
        "border-radius:1rem;padding:.25rem .4rem .25rem .75rem;"
        "font-size:.8rem;max-width:16rem}"
        ".attachChip span{overflow:hidden;text-overflow:ellipsis;"
        "white-space:nowrap}"
        ".attachChip button{width:1.3rem;height:1.3rem;flex:none;padding:0;"
        "margin:0;border-radius:50%;border:none;background:transparent;"
        "color:var(--muted);line-height:1;font-size:1rem}"
        ".attachChip button:hover{background:var(--bg)}"
        // Tables replace the raw JSON dumps every list page used to show --
        // this is a user-facing screen, not a debugging console.
        "table{width:100%;border-collapse:collapse;font-size:.85rem}"
        "th,td{text-align:left;padding:.5rem .6rem;border-bottom:1px solid "
        "var(--panel-border)}"
        "th{color:var(--muted);font-weight:600;text-transform:uppercase;"
        "font-size:.7rem;letter-spacing:.05em}"
        ".stateTag{padding:.15rem .5rem;border-radius:1rem;font-size:.75rem}"
        ".stateTag-ready,.stateTag-approved,.stateTag-production,"
        ".stateTag-verified{background:#0d3321;color:#5fe3a4}"
        ".stateTag-invalid,.stateTag-failed,.stateTag-quarantined,"
        ".stateTag-rejected{background:#3a1414;color:#f299a0}"
        ".stateTag-downloading,.stateTag-unverified,.stateTag-training,"
        ".stateTag-evaluation,.stateTag-pending,.stateTag-imported,"
        ".stateTag-staging{background:#3a2f0d;color:#f2c96d}"
        "#hfFields,#githubFields{border:1px solid var(--panel-border);"
        "border-radius:.5rem;padding:.5rem .75rem;margin-top:.5rem}"
        // System Report: each section is its own bordered card with a fixed
        // label-column width shared across every card, so values line up
        // in a single straight column instead of each section's table
        // sizing its own first column independently off whatever labels
        // happen to be longest in that section.
        "#systemReport{display:flex;flex-direction:column;gap:1rem}"
        "#systemReport .reportSection{background:var(--panel);"
        "border:1px solid var(--panel-border);border-radius:.6rem;"
        "padding:.9rem 1.1rem}"
        "#systemReport .reportSection h3{margin:0 0 .4rem;font-size:.75rem;"
        "font-weight:600;text-transform:uppercase;letter-spacing:.05em;"
        "color:var(--muted)}"
        "#systemReport table{table-layout:fixed}"
        "#systemReport td{border-bottom:1px solid var(--panel-border);"
        "vertical-align:top}"
        "#systemReport tr:last-child td{border-bottom:none}"
        "#systemReport td.reportLabel{width:18rem;color:var(--muted)}"
        "#systemReport td.reportValue{font-variant-numeric:tabular-nums}"
        "@media (max-width:640px){"
        "#systemReport td.reportLabel{width:9rem}}"
        // The sidebar's user-resized width (persisted in localStorage, see
        // the sidebar script below) can otherwise still eat most of a
        // narrow window's space -- these two breakpoints claw it back
        // before content is squeezed to nothing.
        "@media (max-width:900px){#sidebar{width:12rem!important}}"
        "@media (max-width:640px){#shell{flex-direction:column;height:auto;"
        "min-height:100vh}"
        "#sidebar{width:auto!important;max-height:40vh;"
        "border-right:none;border-bottom:1px solid var(--panel-border)}"
        "#sidebarResizer{display:none}}"
        "</style></head><body>"
        "<div id=\"shell\" data-role=\"" + role_attr + "\"><nav id=\"sidebar\">"
        // The sidebar's width and scroll position both persist client-side
        // (see the sidebar script in application_script()): a drag handle
        // (#sidebarResizer below) lets the user widen or narrow the
        // sidebar, and both settings survive the full-page navigation every
        // sidebar click performs.
        "<h1>MasterAI</h1><p id=\"who\">" +
        html_escape(user.display_name) +
        "</p>" +
        sidebar_links +
        "</nav><div id=\"sidebarResizer\" "
        "title=\"Drag to resize the sidebar\"></div><main id=\"content\">"
        "<p id=\"actionStatus\" role=\"status\"></p>" +
        body +
        "</main></div><script src=\"/assets/app.js\"></script></body></html>");
}

}  // namespace masterai::server_internal
