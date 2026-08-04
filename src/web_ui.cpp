// MasterAI dependency-free loopback browser presentation.
//
// This unit contains only static HTML and JavaScript documents. Moving browser
// presentation out of the HTTP router keeps transport, policy, and UI concerns
// independently reviewable without introducing a web framework.
#include "server_internal.hpp"

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
    /* Compact icon-only action buttons (Machine Learning table rows): \
       overrides the full-width default above so a row of actions stays \
       small; the action name lives in the title/aria-label hint. One \
       icon per action kind, consistent across every ML page. */ \
    ".iconBtn{width:auto;margin:.1rem .15rem .1rem 0;padding:.35rem .45rem;" \
    "line-height:0;display:inline-flex;align-items:center;" \
    "justify-content:center;vertical-align:middle}" \
    ".iconBtn svg{width:14px;height:14px;fill:currentColor}" \
    "progress{width:100%;height:.6rem;margin-top:.6rem;accent-color:var(--accent)}" \
    "#actionStatus,#status{color:var(--muted);min-height:1.2em}" \
    /* Fixed top-of-viewport banner every action-failure catch block now \
       raises through showSystemError() instead of the easy-to-miss \
       #actionStatus line -- maroon body with yellow text so a failure is \
       unmissable regardless of which page/section triggered it. */ \
    "#systemErrorBanner{position:fixed;top:0;left:0;right:0;z-index:9999;" \
    "background:#3a0a0a;color:#ffd54a;padding:.75rem 1rem;" \
    "border-bottom:2px solid #ffd54a;box-shadow:0 2px 10px rgba(0,0,0,.4);" \
    "display:flex;align-items:flex-start;gap:.75rem}" \
    "#systemErrorBanner .systemErrorTitle{font-weight:800;letter-spacing:.03em}" \
    "#systemErrorBanner .systemErrorBody{flex:1;overflow-wrap:anywhere}" \
    "#systemErrorBanner .systemErrorClose{margin:0;padding:0 .4rem;" \
    "background:transparent;color:#ffd54a;font-weight:700;cursor:pointer}" \
    ".checkboxLabel{display:flex;align-items:center;gap:.5rem}" \
    ".checkboxLabel input{width:auto}"

namespace masterai::server_internal {
// Supplies the small fetch/streaming client used by both browser documents.
std::string application_script() {
    return
        "const q=s=>document.querySelector(s);let csrf='',generation=null,"
        "lastAppliedPreset=null;const downloadSizes={};"
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
        // Every action-failure catch block across the app raises through
        // here instead of quietly setting the small #actionStatus line --
        // a fixed banner pinned to the very top of the viewport, maroon
        // body/yellow text, titled 'SYSTEM ERROR!' so a failure is
        // impossible to miss regardless of scroll position or which
        // section triggered it. Reuses one banner element (created lazily)
        // so a second failure while the first is still showing just
        // replaces the message rather than stacking banners.
        "function showSystemError(message){let el=q('#systemErrorBanner');"
        "if(!el){el=document.createElement('div');el.id='systemErrorBanner';"
        "const title=document.createElement('div');"
        "title.className='systemErrorTitle';title.textContent='SYSTEM ERROR!';"
        "const body=document.createElement('div');body.className='systemErrorBody';"
        "const close=document.createElement('button');close.type='button';"
        "close.className='systemErrorClose';close.textContent='\\u00d7';"
        "close.setAttribute('aria-label','Dismiss error');"
        "close.addEventListener('click',()=>{el.hidden=true;});"
        "el.append(title,body,close);document.body.prepend(el);}"
        "el.querySelector('.systemErrorBody').textContent=message;el.hidden=false;}"
        "async function login(e){e.preventDefault();try{const d=await api('/api/v1/auth/login','POST',"
        "{username:q('#username').value,password:q('#password').value});"
        "sessionStorage.setItem('csrf',d.csrfToken);location.href='/app';}"
        "catch(x){showSystemError('Login failed. Check your username and password.');}}"
        // Toggles the setup-vs-login sections on the initial page load by
        // asking the already-public /health/ready route whether the first
        // administrator still needs to be created.
        "async function initLogin(){try{const r=await fetch('/health/ready');"
        "const d=await r.json();if(d.setupRequired){q('#setupSection').hidden=false;"
        "q('#loginSection').hidden=true;}}catch(x){}}"
        "async function setupLocalAdmin(e){e.preventDefault();const s=q('#status');"
        "try{await api('/api/v1/setup/local','POST',{setupToken:q('#setupToken').value,"
        "username:q('#setupUsername').value,password:q('#setupPassword').value,"
        "displayName:q('#setupDisplay').value});"
        "s.textContent='Administrator created. Sign in below.';"
        "q('#setupSection').hidden=true;q('#loginSection').hidden=false;}"
        "catch(x){showSystemError('Setup failed: '+x.message);}}"
        "async function load(){csrf=sessionStorage.getItem('csrf')||'';try{"
        "const [me,p,c,m,b,d,u,ml,mlp,mlm,mld,mls,mllt,mlpj,mltj,mler,mlex,mlft,mlmb,mlie,mlsr,mlvs,mlrag,mlse,mlhs,mlmo,mlck,mldp,mlcmp,cfg,report]="
        "await Promise.all([api('/api/v1/users/me'),"
        "api('/api/v1/projects').catch(()=>({projects:[]})),"
        "api('/api/v1/chats'),"
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
        "fetchFor('#mlProjectsList','/api/v1/ml/projects',{projects:[]}),"
        "fetchFor('#mlModelsList','/api/v1/ml/models',{models:[]}),"
        "fetchFor('#mlDatasetsList','/api/v1/ml/datasets',{datasets:[]}),"
        "fetchFor('#mlSubjectsList','/api/v1/ml/subjects',{subjects:[]}),"
        "fetchFor('#mlLabelTasksList','/api/v1/ml/label-tasks',{labelTasks:[]}),"
        "fetchFor('#mlPrepJobsList','/api/v1/ml/prep-jobs',{prepJobs:[]}),"
        "fetchFor('#mlTrainingJobsList','/api/v1/ml/training-jobs',"
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
        "fetchFor('#mlVectorStoresList','/api/v1/ml/vector-stores',"
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
        // Every ML list above just re-rendered its rows; swap their text
        // action buttons for the compact consistent icon set.
        "iconifyMlButtons();"
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
        "const ready=m.models.filter(x=>x.state==='ready');"
        "fill('#chatModel',ready,x=>x.id,"
        "x=>(x.diagnostic&&x.diagnostic.startsWith('Warning:')?'\\u26a0\\ufe0f ':'')+"
        "x.displayName+' ('+Math.round(x.recommendedRamMiB/1024)+'GB)');"
        "renderChatList(c.chats);"
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
        "modelSelect.value=chat.modelId;}"
        "if(q('#chatProject'))q('#chatProject').disabled=true;"
        "const box=q('#chatMessages');box.replaceChildren();"
        "for(const message of chat.messages)appendMessage(box,message.role,message.content);"
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
        "function appendMessage(box,role,content){const p=document.createElement('div');"
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
        "title.textContent=role==='assistant'?'Response':'Query';"
        "p.append(title);}"
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
        // Inline span-level formatting within a single line: backtick code
        // spans, **bold**, and \\(...\\) inline math. Operates on
        // already-HTML-escaped text so the replacement groups never need
        // escaping themselves.
        "function renderInline(text){let t=esc(text);"
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
        "while(i<lines.length&&lines[i].trim()!==''&&!/^```/.test(lines[i])&&"
        "!/^\\s*[-*]\\s+/.test(lines[i])&&!/^\\s*\\d+[.)]\\s+/.test(lines[i])){"
        "para.push(renderInline(lines[i]));i++;}"
        "html+='<p>'+para.join('<br>')+'</p>';}"
        "return html;}"
        // Escapes text for safe insertion as HTML content elsewhere in this
        // file (table cells built from trusted-looking but user-supplied
        // strings such as display names and diagnostics).
        "function esc(s){const d=document.createElement('div');"
        "d.textContent=s==null?'':String(s);return d.innerHTML;}"
        // Builds a simple two-column-plus-actions HTML table from rows,
        // replacing the raw JSON dumps every list used to show verbatim --
        // this is a user-facing screen, not a debugging console.
        "function table(headers,rows){if(!rows.length)return'<p>Nothing here yet.</p>';"
        "let h='<table><thead><tr>';"
        "for(const header of headers)h+='<th>'+esc(header)+'</th>';"
        "h+='</tr></thead><tbody>';"
        "for(const row of rows){h+='<tr>';for(const cell of row)h+='<td>'+cell+'</td>';h+='</tr>';}"
        "return h+'</tbody></table>';}"
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
        "'<button type=\"button\" data-delete-ml-project=\"'+x.id+'\">Delete"
        "</button>']));"
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
        "el.innerHTML=table(['Name','Version','Family','Task','State','Set state',''],"
        "models.map(x=>[esc(x.displayName||x.name),esc(x.version),"
        "esc(x.family),esc(x.task),"
        "'<span class=\"stateTag stateTag-'+esc(x.state)+'\">'+esc(x.state)+"
        "'</span>',"
        "'<select data-state-for=\"'+x.id+'\">'+MODEL_STATES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.state?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-state=\"'+x.id+'\">Apply</button>',"
        "'<button type=\"button\" data-delete-ml-model=\"'+x.id+'\">Delete"
        "</button>']));"
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
        "'<button type=\"button\" data-approve-ml-dataset=\"'+x.id+'\">Approve"
        "</button> '+"
        "'<button type=\"button\" data-reject-ml-dataset=\"'+x.id+'\">Reject"
        "</button> '+"
        "'<button type=\"button\" data-delete-ml-dataset=\"'+x.id+'\">Delete"
        "</button>']));"
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
        "status','Set status',''],"
        "subjects.map(x=>[esc(x.name),esc(x.scope),esc(x.targetAudience),"
        "'<span class=\"stateTag stateTag-'+esc(x.reviewStatus)+'\">'+"
        "esc(x.reviewStatus)+'</span>',"
        "'<select data-review-status-for=\"'+x.id+'\">'+"
        "SUBJECT_REVIEW_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.reviewStatus?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-review-status=\"'+x.id+"
        "'\">Apply</button>',"
        "'<button type=\"button\" data-delete-ml-subject=\"'+x.id+'\">Delete"
        "</button>']));"
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
        "'Status','Set status',''],"
        "tasks.map(x=>[esc(x.name),esc(x.datasetId),esc(x.labelMode),"
        "esc(x.assigneeId),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "'<select data-label-task-status-for=\"'+x.id+'\">'+"
        "LABEL_TASK_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-label-task-status=\"'+x.id+"
        "'\">Apply</button>',"
        "'<button type=\"button\" data-delete-ml-label-task=\"'+x.id+"
        "'\">Delete</button>']));"
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
        "'Set status',''],"
        "jobs.map(x=>[esc(x.name),esc(x.datasetId),esc(x.operation),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "'<select data-prep-job-status-for=\"'+x.id+'\">'+"
        "PREP_JOB_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-prep-job-status=\"'+x.id+"
        "'\">Apply</button>',"
        "'<button type=\"button\" data-delete-ml-prep-job=\"'+x.id+"
        "'\">Delete</button>']));"
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
        // Compact iconic row actions for every Machine Learning page. The
        // SVG path data is Bootstrap Icons (bi-trash, bi-check-lg,
        // bi-check-circle, bi-x-circle, bi-play-fill, bi-eye), embedded
        // inline so the UI needs no CDN and keeps working offline. One
        // icon per action kind, applied uniformly: delete=trash,
        // apply=check, approve=check-circle, reject=x-circle,
        // run/train/evaluate=play, view=eye. The original button label
        // becomes the hover hint (title) and the accessible name.
        "const BI_ICON_PATHS={"
        "trash:'<path d=\"M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 "
        "0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 "
        ".5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z\"/>"
        "<path d=\"M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 "
        "1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H6a1 1 0 0 1 1-1h2a1 1 "
        "0 0 1 1 1h3.5a1 1 0 0 1 1 1zM4.118 4 4 4.059V13a1 1 0 0 0 1 "
        "1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z\"/>',"
        "apply:'<path d=\"M12.736 3.97a.733.733 0 0 1 1.047 0c.286.289.29"
        ".756.01 1.05L7.88 12.01a.733.733 0 0 1-1.065.02L3.217 8.384a.757"
        ".757 0 0 1 0-1.06.733.733 0 0 1 1.047 0l3.052 3.093 5.4-6.425a"
        ".247.247 0 0 1 .02-.022z\"/>',"
        "approve:'<path d=\"M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 "
        "1 0 8 0a8 8 0 0 0 0 16\"/>"
        "<path d=\"M10.97 4.97a.235.235 0 0 0-.02.022L7.477 9.417 5.384 "
        "7.323a.75.75 0 0 0-1.06 1.06L6.97 11.03a.75.75 0 0 0 1.079-.02l"
        "3.992-4.99a.75.75 0 0 0-1.071-1.05z\"/>',"
        "reject:'<path d=\"M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 "
        "0 8 0a8 8 0 0 0 0 16\"/>"
        "<path d=\"M4.646 4.646a.5.5 0 0 1 .708 0L8 7.293l2.646-2.647a.5"
        ".5 0 0 1 .708.708L8.707 8l2.647 2.646a.5.5 0 0 1-.708.708L8 "
        "8.707l-2.646 2.647a.5.5 0 0 1-.708-.708L7.293 8 4.646 5.354a.5.5 "
        "0 0 1 0-.708\"/>',"
        "run:'<path d=\"m11.596 8.697-6.363 3.692c-.54.313-1.233-.066-"
        "1.233-.697V4.308c0-.63.692-1.01 1.233-.696l6.363 3.692a.802.802 "
        "0 0 1 0 1.393\"/>',"
        "view:'<path d=\"M16 8s-3-5.5-8-5.5S0 8 0 8s3 5.5 8 5.5S16 8 16 8M"
        "1.173 8a13 13 0 0 1 1.66-2.043C4.12 4.668 5.88 3.5 8 3.5s3.879 "
        "1.168 5.168 2.457A13 13 0 0 1 14.828 8q-.086.13-.195.288c-.335"
        ".48-.83 1.12-1.465 1.755C11.879 11.332 10.119 12.5 8 12.5s-3.879"
        "-1.168-5.168-2.457A13 13 0 0 1 1.172 8z\"/>"
        "<path d=\"M8 5.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5M4.5 8a3.5 "
        "3.5 0 1 1 7 0 3.5 3.5 0 0 1-7 0\"/>'};"
        "const ML_BUTTON_ICONS=[[/^delete/,'trash'],[/^approve/,'approve'],"
        "[/^reject/,'reject'],[/^apply/,'apply'],[/^run/,'run'],"
        "[/^view/,'view']];"
        "function iconifyMlButtons(){"
        "for(const btn of document.querySelectorAll("
        "'section[id^=\"panel-ml-\"] button')){"
        "if(btn.dataset.iconified)continue;"
        "const key=Object.keys(btn.dataset)[0];if(!key)continue;"
        "const match=ML_BUTTON_ICONS.find(pair=>pair[0].test(key));"
        "if(!match)continue;"
        "const label=btn.textContent.trim();"
        "btn.title=label;btn.setAttribute('aria-label',label);"
        "btn.classList.add('iconBtn');"
        "btn.innerHTML='<svg viewBox=\"0 0 16 16\" aria-hidden=\"true\">'+"
        "BI_ICON_PATHS[match[1]]+'</svg>';"
        "btn.dataset.iconified='1';}}"
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
        "type','Status','Set status',''],"
        "jobs.map(x=>[esc(x.name),esc(x.projectId),esc(x.modelId),"
        "esc(x.datasetId),esc(x.trainingType),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "'<select data-training-job-status-for=\"'+x.id+'\">'+"
        "TRAINING_JOB_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-training-job-status=\"'+x.id+"
        "'\">Apply</button>',"
        "'<button type=\"button\" data-run-ml-training-job=\"'+x.id+"
        "'\">Train now</button> '+"
        "'<button type=\"button\" data-delete-ml-training-job=\"'+x.id+"
        "'\">Delete</button>']));"
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
        "'Set status',''],"
        "runs.map(x=>[esc(x.name),esc(x.modelId),esc(x.datasetId),"
        "esc(x.category),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "'<select data-evaluation-run-status-for=\"'+x.id+'\">'+"
        "EVALUATION_RUN_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-evaluation-run-status=\"'+x.id+"
        "'\">Apply</button>',"
        "'<button type=\"button\" data-run-ml-evaluation=\"'+x.id+"
        "'\">Evaluate now</button> '+"
        "'<button type=\"button\" data-view-ml-evaluation=\"'+x.id+"
        "'\">View result</button> '+"
        "'<button type=\"button\" data-delete-ml-evaluation-run=\"'+x.id+"
        "'\">Delete</button>']));"
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
        "'Set status',''],"
        "experiments.map(x=>[esc(x.name),esc(x.projectId),esc(x.modelId),"
        "esc(x.datasetId),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "'<select data-experiment-status-for=\"'+x.id+'\">'+"
        "EXPERIMENT_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-experiment-status=\"'+x.id+"
        "'\">Apply</button>',"
        "'<button type=\"button\" data-delete-ml-experiment=\"'+x.id+"
        "'\">Delete</button>']));"
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
        // fine-tuning is a training-job variant.
        "const FINE_TUNING_JOB_STATUSES=['draft','queued','preparing',"
        "'running','paused','canceling','canceled','failed','completed',"
        "'awaiting_evaluation','archived'];"
        "function renderMlFineTuningJobs(jobs){"
        "const el=q('#mlFineTuningJobsList');if(!el)return;"
        "if(!jobs.length){el.innerHTML='<p>No fine-tuning jobs created "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Project','Base model','Dataset',"
        "'Method','Status','Set status',''],"
        "jobs.map(x=>[esc(x.name),esc(x.projectId),esc(x.modelId),"
        "esc(x.datasetId),esc(x.method),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "'<select data-fine-tuning-job-status-for=\"'+x.id+'\">'+"
        "FINE_TUNING_JOB_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-fine-tuning-job-status=\"'+x.id+"
        "'\">Apply</button>',"
        "'<button type=\"button\" data-delete-ml-fine-tuning-job=\"'+x.id+"
        "'\">Delete</button>']));"
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
        // never runs.
        "const MODEL_BUILDER_CONFIG_STATUSES=['draft','configuring','ready',"
        "'submitted','archived'];"
        "function renderMlModelBuilderConfigs(configs){"
        "const el=q('#mlModelBuilderConfigsList');if(!el)return;"
        "if(!configs.length){el.innerHTML='<p>No model builder "
        "configurations created yet.</p>';return;}"
        "el.innerHTML=table(['Name','Project','Base model','Source type',"
        "'Status','Set status',''],"
        "configs.map(x=>[esc(x.name),esc(x.projectId),esc(x.baseModelId),"
        "esc(x.sourceType),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "'<select data-model-builder-config-status-for=\"'+x.id+'\">'+"
        "MODEL_BUILDER_CONFIG_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-model-builder-config-status=\"'+"
        "x.id+'\">Apply</button>',"
        "'<button type=\"button\" data-delete-ml-model-builder-config=\"'+"
        "x.id+'\">Delete</button>']));"
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
        "'Status','Set status',''],"
        "examples.map(x=>[esc(x.name),esc(x.datasetId),"
        "esc(x.subjectClassification),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "'<select data-instruction-example-status-for=\"'+x.id+'\">'+"
        "INSTRUCTION_EXAMPLE_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-instruction-example-status=\"'+"
        "x.id+'\">Apply</button>',"
        "'<button type=\"button\" data-delete-ml-instruction-example=\"'+"
        "x.id+'\">Delete</button>']));"
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
        "'Status','Set status',''],"
        "records.map(x=>[esc(x.name),esc(x.datasetId),"
        "esc(x.generationTechnique),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "'<select data-synthetic-record-status-for=\"'+x.id+'\">'+"
        "SYNTHETIC_RECORD_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-synthetic-record-status=\"'+"
        "x.id+'\">Apply</button>',"
        "'<button type=\"button\" data-delete-ml-synthetic-record=\"'+"
        "x.id+'\">Delete</button>']));"
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
        "'Status','Set status',''],"
        "stores.map(x=>[esc(x.name),esc(x.embeddingModel),"
        "esc(x.distanceMetric),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "'<select data-vector-store-status-for=\"'+x.id+'\">'+"
        "VECTOR_STORE_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-vector-store-status=\"'+"
        "x.id+'\">Apply</button>',"
        "'<button type=\"button\" data-delete-ml-vector-store=\"'+"
        "x.id+'\">Delete</button>']));"
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
        "if(!configs.length){el.innerHTML='<p>No RAG configurations "
        "registered yet.</p>';return;}"
        "el.innerHTML=table(['Name','Search strategy','Vector store',"
        "'Status','Set status',''],"
        "configs.map(x=>[esc(x.name),esc(x.searchStrategy),"
        "esc(x.vectorStoreId),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "'<select data-rag-config-status-for=\"'+x.id+'\">'+"
        "RAG_CONFIG_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-rag-config-status=\"'+"
        "x.id+'\">Apply</button>',"
        "'<button type=\"button\" data-delete-ml-rag-config=\"'+"
        "x.id+'\">Delete</button>']));"
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
        "'Status','Set status',''],"
        "exams.map(x=>[esc(x.name),esc(x.subjectId),"
        "esc(x.questionFormat),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "'<select data-subject-exam-status-for=\"'+x.id+'\">'+"
        "SUBJECT_EXAM_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-subject-exam-status=\"'+"
        "x.id+'\">Apply</button>',"
        "'<button type=\"button\" data-delete-ml-subject-exam=\"'+"
        "x.id+'\">Delete</button>']));"
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
        "'Status','Set status',''],"
        "searches.map(x=>[esc(x.name),esc(x.trainingJobId),"
        "esc(x.strategy),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "'<select data-hyperparameter-search-status-for=\"'+x.id+'\">'+"
        "HYPERPARAMETER_SEARCH_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-hyperparameter-search-status=\"'+"
        "x.id+'\">Apply</button>',"
        "'<button type=\"button\" data-delete-ml-hyperparameter-search=\"'+"
        "x.id+'\">Delete</button>']));"
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
        "'Status','Set status',''],"
        "runs.map(x=>[esc(x.name),esc(x.modelId),esc(x.operation),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "'<select data-model-optimization-status-for=\"'+x.id+'\">'+"
        "MODEL_OPTIMIZATION_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-model-optimization-status=\"'+"
        "x.id+'\">Apply</button>',"
        "'<button type=\"button\" data-delete-ml-model-optimization=\"'+"
        "x.id+'\">Delete</button>']));"
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
        "'Status','Set status',''],"
        "checkpoints.map(x=>[esc(x.name),esc(x.trainingJobId),"
        "esc(x.captureReason),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "'<select data-checkpoint-status-for=\"'+x.id+'\">'+"
        "CHECKPOINT_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-checkpoint-status=\"'+"
        "x.id+'\">Apply</button>',"
        "'<button type=\"button\" data-delete-ml-checkpoint=\"'+"
        "x.id+'\">Delete</button>']));"
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
        "'Status','Set status',''],"
        "deployments.map(x=>[esc(x.name),esc(x.modelId),"
        "esc(x.environment),esc(x.strategy),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "'<select data-deployment-status-for=\"'+x.id+'\">'+"
        "DEPLOYMENT_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-deployment-status=\"'+"
        "x.id+'\">Apply</button>',"
        "'<button type=\"button\" data-delete-ml-deployment=\"'+"
        "x.id+'\">Delete</button>']));"
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
        "'Benchmark dataset','Status','Set status',''],"
        "comparisons.map(x=>[esc(x.name),esc(x.baselineModelId),"
        "esc(x.candidateModelId),esc(x.datasetId),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "'<select data-comparison-status-for=\"'+x.id+'\">'+"
        "MODEL_COMPARISON_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select> '+"
        "'<button type=\"button\" data-apply-comparison-status=\"'+x.id+"
        "'\">Apply</button>',"
        "'<button type=\"button\" data-run-ml-comparison=\"'+x.id+"
        "'\">Compare now</button> '+"
        "'<button type=\"button\" data-view-ml-comparison=\"'+x.id+"
        "'\">View result</button> '+"
        "'<button type=\"button\" data-delete-ml-comparison=\"'+x.id+"
        "'\">Delete</button>']));"
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
        "'<button type=\"button\" data-run-id=\"'+x.id+'\" '+"
        "(x.state==='complete'?'disabled':'')+'>'+"
        "(x.state==='complete'?'Complete':'Start / resume')+'</button>'+"
        "(x.state==='transferring'?"
        "' <button type=\"button\" data-pause-id=\"'+x.id+'\">Pause</button>':'')+"
        "(ACTIVE.has(x.state)?"
        "' <button type=\"button\" data-stop-id=\"'+x.id+'\">Stop</button>':'')+"
        "' <button type=\"button\" data-remove-id=\"'+x.id+'\">Remove</button>']));"
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
        "}catch(x){}};"
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
        "appendMessage(box,'user',content);box.scrollTop=box.scrollHeight;"
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
        "body:JSON.stringify({content,attachmentIds}),signal:generation.signal});"
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
        "bodyEl.innerHTML=renderMarkdown(assistantEl.dataset.raw);"
        "addCodeCopyButtons(bodyEl);"
        "box.scrollTop=box.scrollHeight;}"
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
        "ready.some(x=>x.id===st.modelId))defaultId=st.modelId;}catch(x){}"
        "if(!defaultId){"
        "const lastChat=chats.find(x=>x.modelId&&ready.some(r=>r.id===x.modelId));"
        "if(lastChat)defaultId=lastChat.modelId;}"
        "if(defaultId)picker.value=defaultId;}"
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
        "' is ready.');}}catch(x){}}"
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
        "if(q('#chatModel'))q('#chatModel').addEventListener('change',changeChatModel);"
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
        "out.textContent='Uploading and validating...';"
        "try{const r=await api('/api/v1/ml/datasets/'+"
        "encodeURIComponent(q('#mlDatasetContentId').value.trim())+'/content',"
        "'POST',{csv:q('#mlDatasetContentCsv').value,"
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
        "description:q('#mlModelComparisonDescription').value})));}});";
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
        "<button>Create administrator</button></form></section>"
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
            "<button>Create project</button></form>"
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
            "<button>Queue download</button></form>"
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
            "<button>Create project</button></form>"
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
            "<button>Register model</button></form>"
            // Phase 56: live prediction against a trained model's persisted
            // weights. Feature values are entered as JSON keyed by column
            // name so the caller never has to know the internal ordering.
            "<h2>Predict with a trained model</h2>"
            "<form id=\"mlPredictForm\">"
            "<label>Model ID (a model produced by a training run)"
            "<input id=\"mlPredictModelId\" required></label>"
            "<label>Feature values (JSON object, e.g. "
            "{&quot;sepal_length&quot;:5.1,&quot;sepal_width&quot;:3.5})"
            "<textarea id=\"mlPredictFeatures\" rows=\"3\" required>"
            "</textarea></label>"
            "<button>Predict</button></form>"
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
            "<button>Register dataset</button></form>"
            // Phase 56: real content upload. The CSV is fully parsed and
            // validated server-side before it is stored, and the returned
            // profile (rows, columns, task) is shown below the form.
            "<h2>Upload dataset content (CSV)</h2>"
            "<form id=\"newMlDatasetContent\">"
            "<label>Dataset ID<input id=\"mlDatasetContentId\" required "
            "placeholder=\"dataset id from the list on the right\"></label>"
            "<label>Target column (the column to predict; blank uses the "
            "last column)<input id=\"mlDatasetContentTarget\"></label>"
            "<label>CSV content (header row first; feature columns must be "
            "numeric)<textarea id=\"mlDatasetContentCsv\" rows=\"6\" required "
            "placeholder=\"sepal_length,sepal_width,species&#10;"
            "5.1,3.5,setosa&#10;6.2,2.9,versicolor\"></textarea></label>"
            "<button>Upload content</button></form>"
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
            "<button>Create subject package</button></form>"
            "</div><div>"
            "<h2>Subject packages</h2>"
            "<div id=\"mlSubjectsList\">Loading...</div>"
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
            "<label>Dataset ID<input id=\"mlLabelTaskDatasetId\" required "
            "placeholder=\"dataset id from Dataset Manager\"></label>"
            "<label>Name<input id=\"mlLabelTaskName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlLabelTaskDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Label mode<input id=\"mlLabelTaskLabelMode\" "
            "placeholder=\"e.g. text_category, entity_span, bounding_box\">"
            "</label>"
            "<label>Assignee ID<input id=\"mlLabelTaskAssigneeId\" "
            "placeholder=\"reviewer user id (optional)\"></label>"
            "<button>Create labeling task</button></form>"
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
            "<label>Dataset ID<input id=\"mlPrepJobDatasetId\" required "
            "placeholder=\"dataset id from Dataset Manager\"></label>"
            "<label>Name<input id=\"mlPrepJobName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlPrepJobDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Operation<input id=\"mlPrepJobOperation\" required "
            "placeholder=\"e.g. remove_duplicates, redact_pii, split_dataset\">"
            "</label>"
            "<button>Create preparation job</button></form>"
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
            "<label>Project ID<input id=\"mlTrainingJobProjectId\" required "
            "placeholder=\"project id from Projects\"></label>"
            "<label>Model ID<input id=\"mlTrainingJobModelId\" "
            "placeholder=\"model registry id (optional)\"></label>"
            "<label>Dataset ID<input id=\"mlTrainingJobDatasetId\" required "
            "placeholder=\"dataset id from Dataset Manager\"></label>"
            "<label>Name<input id=\"mlTrainingJobName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlTrainingJobDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Training type<input id=\"mlTrainingJobTrainingType\" "
            "placeholder=\"e.g. fine_tuning, transfer_learning, lora\">"
            "</label>"
            "<button>Create training job</button></form>"
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
            "<label>Model ID<input id=\"mlEvaluationRunModelId\" required "
            "placeholder=\"model registry id\"></label>"
            "<label>Dataset ID<input id=\"mlEvaluationRunDatasetId\" required "
            "placeholder=\"benchmark dataset id from Dataset Manager\">"
            "</label>"
            "<label>Name<input id=\"mlEvaluationRunName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlEvaluationRunDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Category<input id=\"mlEvaluationRunCategory\" "
            "placeholder=\"e.g. accuracy, f1_score, hallucination_rate\">"
            "</label>"
            "<button>Create evaluation run</button></form>"
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
            "<label>Project ID<input id=\"mlExperimentProjectId\" required "
            "placeholder=\"project id from Projects\"></label>"
            "<label>Model ID<input id=\"mlExperimentModelId\" required "
            "placeholder=\"model registry id\"></label>"
            "<label>Dataset ID<input id=\"mlExperimentDatasetId\" "
            "placeholder=\"dataset id from Dataset Manager (optional)\">"
            "</label>"
            "<label>Name<input id=\"mlExperimentName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlExperimentDescription\" "
            "rows=\"2\"></textarea></label>"
            "<button>Create experiment</button></form>"
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
            "<label>Base model ID<input id=\"mlFineTuningJobModelId\" "
            "required placeholder=\"model registry id to adapt\"></label>"
            "<label>Fine-tuning dataset ID<input "
            "id=\"mlFineTuningJobDatasetId\" required "
            "placeholder=\"dataset id from Dataset Manager\"></label>"
            "<label>Project ID<input id=\"mlFineTuningJobProjectId\" "
            "placeholder=\"project id from Projects (optional)\"></label>"
            "<label>Name<input id=\"mlFineTuningJobName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlFineTuningJobDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Method<input id=\"mlFineTuningJobMethod\" "
            "placeholder=\"e.g. subject_specialisation, code_assistant, "
            "safety_alignment\"></label>"
            "<button>Create fine-tuning job</button></form>"
            "</div><div>"
            "<h2>Fine-tuning jobs</h2>"
            "<div id=\"mlFineTuningJobsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-model-builder-configs") {
        // Phase 46 (docs/PLAN.md "Machine Learning Abilities" section 9):
        // create and list model builder configurations against an optional
        // project and optional base model, and move them through a design-
        // time lifecycle status. Only the identity/target-project/base-
        // model/source-type/status fields ModelBuilderConfigStore actually
        // persists are collected here -- see that class's comment in
        // masterai.hpp for the architecture/layer/tokenizer/optimiser/
        // scheduling fields deferred to the phase that actually executes a
        // model build.
        body =
            "<section id=\"panel-ml-model-builder-configs\" class=\"panel\">"
            "<div>"
            "<h2>New model builder configuration</h2>"
            "<form id=\"newMlModelBuilderConfig\">"
            "<label>Source type<input id=\"mlModelBuilderConfigSourceType\" "
            "required placeholder=\"e.g. template, imported_base_model, "
            "embedding_model\"></label>"
            "<label>Base model ID<input "
            "id=\"mlModelBuilderConfigBaseModelId\" "
            "placeholder=\"model registry id to start from (optional)\">"
            "</label>"
            "<label>Project ID<input id=\"mlModelBuilderConfigProjectId\" "
            "placeholder=\"project id from Projects (optional)\"></label>"
            "<label>Name<input id=\"mlModelBuilderConfigName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea "
            "id=\"mlModelBuilderConfigDescription\" rows=\"2\"></textarea>"
            "</label>"
            "<button>Create configuration</button></form>"
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
            "<label>Dataset ID<input id=\"mlInstructionExampleDatasetId\" "
            "required placeholder=\"dataset id from Dataset Manager\">"
            "</label>"
            "<label>Name<input id=\"mlInstructionExampleName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea "
            "id=\"mlInstructionExampleDescription\" rows=\"2\"></textarea>"
            "</label>"
            "<label>Subject classification<input "
            "id=\"mlInstructionExampleSubjectClassification\" "
            "placeholder=\"e.g. cpp_code_review, customer_support\"></label>"
            "<button>Create instruction example</button></form>"
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
            "<label>Dataset ID<input id=\"mlSyntheticRecordDatasetId\" "
            "required placeholder=\"dataset id from Dataset Manager\">"
            "</label>"
            "<label>Name<input id=\"mlSyntheticRecordName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea "
            "id=\"mlSyntheticRecordDescription\" rows=\"2\"></textarea>"
            "</label>"
            "<label>Generation technique<input "
            "id=\"mlSyntheticRecordGenerationTechnique\" "
            "placeholder=\"e.g. paraphrase, edge_case, counterexample\">"
            "</label>"
            "<button>Create synthetic record</button></form>"
            "</div><div>"
            "<h2>Synthetic records</h2>"
            "<div id=\"mlSyntheticRecordsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-vector-stores") {
        // Phase 49 (docs/PLAN.md "Machine Learning Abilities" section 21):
        // register and list vector stores, and move them through the same
        // three-state pending/approved/rejected approval workflow Dataset
        // Manager uses, since a vector store is a standalone registered
        // resource rather than a target-scoped content record. Only the
        // identity/embedding-model/distance-metric/status fields
        // VectorStoreStore actually persists are collected here -- see that
        // class's comment in masterai.hpp for the document-import/chunking/
        // indexing fields deferred to the phase that actually generates
        // embeddings.
        body =
            "<section id=\"panel-ml-vector-stores\" class=\"panel\">"
            "<div>"
            "<h2>New vector store</h2>"
            "<form id=\"newMlVectorStore\">"
            "<label>Name<input id=\"mlVectorStoreName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea "
            "id=\"mlVectorStoreDescription\" rows=\"2\"></textarea></label>"
            "<label>Embedding model<input id=\"mlVectorStoreEmbeddingModel\" "
            "placeholder=\"e.g. text-embedding-3-small\"></label>"
            "<label>Distance metric<input "
            "id=\"mlVectorStoreDistanceMetric\" "
            "placeholder=\"e.g. cosine, dot_product, euclidean\"></label>"
            "<button>Create vector store</button></form>"
            "</div><div>"
            "<h2>Vector stores</h2>"
            "<div id=\"mlVectorStoresList\">Loading...</div>"
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
            "<label>Search strategy<input id=\"mlRagConfigSearchStrategy\" "
            "placeholder=\"e.g. hybrid, vector_only, keyword_only\"></label>"
            "<label>Vector store ID<input "
            "id=\"mlRagConfigVectorStoreId\" "
            "placeholder=\"optional -- id of a registered vector store\">"
            "</label>"
            "<button>Create RAG configuration</button></form>"
            "</div><div>"
            "<h2>RAG configurations</h2>"
            "<div id=\"mlRagConfigsList\">Loading...</div>"
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
            "<label>Subject ID<input id=\"mlSubjectExamSubjectId\" required "
            "placeholder=\"id of a registered subject package\"></label>"
            "<label>Description<textarea "
            "id=\"mlSubjectExamDescription\" rows=\"2\"></textarea></label>"
            "<label>Question format<input "
            "id=\"mlSubjectExamQuestionFormat\" "
            "placeholder=\"e.g. multiple_choice, short_answer, code_task\">"
            "</label>"
            "<button>Create subject exam</button></form>"
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
            "<label>Training job ID<input "
            "id=\"mlHyperparameterSearchTrainingJobId\" required "
            "placeholder=\"id of the training job this search tunes\">"
            "</label>"
            "<label>Description<textarea "
            "id=\"mlHyperparameterSearchDescription\" rows=\"2\">"
            "</textarea></label>"
            "<label>Search strategy<input "
            "id=\"mlHyperparameterSearchStrategy\" "
            "placeholder=\"e.g. grid, random, bayesian\"></label>"
            "<button>Create hyperparameter search</button></form>"
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
            "<label>Model ID<input id=\"mlModelOptimizationModelId\" "
            "required placeholder=\"id of a registered model\"></label>"
            "<label>Description<textarea "
            "id=\"mlModelOptimizationDescription\" rows=\"2\"></textarea>"
            "</label>"
            "<label>Operation<input id=\"mlModelOptimizationOperation\" "
            "placeholder=\"e.g. quantization, pruning, distillation\">"
            "</label>"
            "<button>Create model optimization</button></form>"
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
            "<label>Training job ID<input "
            "id=\"mlCheckpointTrainingJobId\" required "
            "placeholder=\"id of the training job that produced it\">"
            "</label>"
            "<label>Description<textarea "
            "id=\"mlCheckpointDescription\" rows=\"2\"></textarea></label>"
            "<label>Capture reason<input id=\"mlCheckpointCaptureReason\" "
            "placeholder=\"e.g. epoch_end, best_metric, manual\"></label>"
            "<button>Create checkpoint record</button></form>"
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
            "<label>Model ID<input id=\"mlDeploymentModelId\" required "
            "placeholder=\"id of a registered model\"></label>"
            "<label>Description<textarea "
            "id=\"mlDeploymentDescription\" rows=\"2\"></textarea></label>"
            "<label>Environment<input id=\"mlDeploymentEnvironment\" "
            "placeholder=\"e.g. development, staging, production\"></label>"
            "<label>Strategy<input id=\"mlDeploymentStrategy\" "
            "placeholder=\"e.g. direct, blue_green, canary\"></label>"
            "<button>Create deployment</button></form>"
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
            "<label>Baseline model ID<input "
            "id=\"mlModelComparisonBaselineModelId\" required "
            "placeholder=\"id of the registered model to compare against\">"
            "</label>"
            "<label>Candidate model ID<input "
            "id=\"mlModelComparisonCandidateModelId\" required "
            "placeholder=\"id of the registered model being evaluated\">"
            "</label>"
            "<label>Benchmark dataset ID<input "
            "id=\"mlModelComparisonDatasetId\" required "
            "placeholder=\"id of the dataset both models are scored on\">"
            "</label>"
            "<label>Description<textarea "
            "id=\"mlModelComparisonDescription\" rows=\"2\"></textarea>"
            "</label>"
            "<button>Create model comparison</button></form>"
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
            "<button>Save configuration</button>"
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
            "<button>Create local account</button></form>"
            "</div></section>";
    } else if (section == "admin-users") {
        body =
            "<section id=\"panel-admin-users\" class=\"panel\">"
            "<div><h2>User list</h2>"
            "<div id=\"usersList\">Loading...</div></div></section>";
    }

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
            "<div id=\"chatHistoryList\"></div></details>");
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
        sidebar_links += sidebar_section(
            "ml", "Machine Learning",
            nav_link("/app/ml", "Dashboard", section == "ml-dashboard") +
                nav_link("/app/ml/projects", "Projects",
                         section == "ml-projects") +
                nav_link("/app/ml/models", "Model Registry",
                         section == "ml-models") +
                nav_link("/app/ml/datasets", "Dataset Manager",
                         section == "ml-datasets") +
                nav_link("/app/ml/subjects", "Subject Knowledge Manager",
                         section == "ml-subjects") +
                nav_link("/app/ml/label-tasks", "Data Labeling",
                         section == "ml-label-tasks") +
                nav_link("/app/ml/prep-jobs", "Data Preparation",
                         section == "ml-prep-jobs") +
                nav_link("/app/ml/training-jobs", "Training Jobs",
                         section == "ml-training-jobs") +
                nav_link("/app/ml/evaluation-runs", "Evaluation Lab",
                         section == "ml-evaluation-runs") +
                nav_link("/app/ml/experiments", "Experiment Tracking",
                         section == "ml-experiments") +
                nav_link("/app/ml/fine-tuning-jobs", "Fine-Tuning",
                         section == "ml-fine-tuning-jobs") +
                nav_link("/app/ml/model-builder-configs", "Model Builder",
                         section == "ml-model-builder-configs") +
                nav_link("/app/ml/instruction-examples",
                         "Prompt and Instruction Training",
                         section == "ml-instruction-examples") +
                nav_link("/app/ml/synthetic-records",
                         "Synthetic Data Generation",
                         section == "ml-synthetic-records") +
                nav_link("/app/ml/vector-stores",
                         "Embeddings and Vector Stores",
                         section == "ml-vector-stores") +
                nav_link("/app/ml/rag-configs",
                         "Retrieval-Augmented Generation",
                         section == "ml-rag-configs") +
                nav_link("/app/ml/subject-exams",
                         "Subject Examination",
                         section == "ml-subject-exams") +
                nav_link("/app/ml/hyperparameter-searches",
                         "Hyperparameter Optimization",
                         section == "ml-hyperparameter-searches") +
                nav_link("/app/ml/model-optimizations",
                         "Model Optimization",
                         section == "ml-model-optimizations") +
                nav_link("/app/ml/checkpoints",
                         "Checkpoint Management",
                         section == "ml-checkpoints") +
                nav_link("/app/ml/deployments",
                         "Deployment Manager",
                         section == "ml-deployments") +
                nav_link("/app/ml/model-comparisons",
                         "Model Comparison",
                         section == "ml-model-comparisons"));
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
        "#content{flex:1;padding:1.5rem;overflow-y:auto;"
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
        "#chatList{display:flex;flex-direction:column;gap:.15rem;max-height:14rem;"
        "overflow-y:auto}"
        "#chatHistorySection{margin-top:1rem}"
        "#chatHistorySection summary{color:var(--muted);font-size:.85rem;"
        "text-transform:uppercase;letter-spacing:.06em;cursor:pointer}"
        "#chatHistoryList{display:flex;flex-direction:column;gap:.15rem;"
        "max-height:14rem;overflow-y:auto;margin-top:.4rem}"
        ".panel{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));"
        "gap:1.25rem}"
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
        ".composer{display:flex;align-items:center;gap:.4rem;"
        "background:var(--panel);border:1px solid var(--panel-border);"
        "border-radius:1.5rem;padding:.4rem .5rem .4rem 1rem}"
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
