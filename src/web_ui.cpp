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
    "progress{width:100%;height:.6rem;margin-top:.6rem;accent-color:var(--accent)}" \
    "#actionStatus,#status{color:var(--muted);min-height:1.2em}" \
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
        "async function login(e){e.preventDefault();try{const d=await api('/api/v1/auth/login','POST',"
        "{username:q('#username').value,password:q('#password').value});"
        "sessionStorage.setItem('csrf',d.csrfToken);location.href='/app';}"
        "catch(x){q('#status').textContent='Login failed. Check your username and password.';}}"
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
        "catch(x){s.textContent='Setup failed: '+x.message;}}"
        "async function load(){csrf=sessionStorage.getItem('csrf')||'';try{"
        "const [me,p,c,m,b,d,u,ml,mlp,mlm,mld]=await Promise.all([api('/api/v1/users/me'),"
        "api('/api/v1/projects').catch(()=>({projects:[]})),"
        "api('/api/v1/chats'),"
        "api('/api/v1/models').catch(()=>({models:[]})),"
        "api('/api/v1/benchmarks').catch(()=>({benchmarks:[]})),"
        "api('/api/v1/model-downloads').catch(()=>({downloads:[]})),"
        "api('/api/v1/users').catch(()=>({users:[]})),"
        // 403s for anyone who isn't an administrator (see role_allows'
        // ml.dashboard.view) -- the catch keeps that a quiet, expected
        // no-op rather than failing the whole page load for every role.
        "api('/api/v1/ml/dashboard').catch("
        "()=>({enabled:false,phase:'',activeProjects:0,modelsTraining:0,"
        "modelsAwaitingEvaluation:0,modelsAwaitingApproval:0,"
        "deployedModels:0,failedTrainingJobs:0,interfaces:[]})),"
        "api('/api/v1/ml/projects').catch(()=>({projects:[]})),"
        "api('/api/v1/ml/models').catch(()=>({models:[]})),"
        "api('/api/v1/ml/datasets').catch(()=>({datasets:[]}))]);"
        "q('#who').textContent=me.displayName+' ('+me.role+')';"
        // A viewer's sidebar renders none of the settings pages, so these
        // elements legitimately don't exist -- guard every write instead of
        // assuming every page is present.
        "renderProjects(p.projects);renderModels(m.models);"
        "renderBenchmarks(b.benchmarks);renderDownloads(d.downloads);"
        "renderUsers(u.users);renderMlDashboard(ml);renderMlProjects(mlp.projects);"
        "renderMlModels(mlm.models);renderMlDatasets(mld.datasets);"
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
        "administrator or developer to download one from Settings.';}}"
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
        "if(s)s.textContent='Delete failed: '+x.message;}});"
        "row.append(a,del);return row;}"
        "function renderChatList(chats){const list=q('#chatList');if(!list)return;"
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
        "box.scrollTop=box.scrollHeight;}"
        "catch(x){s.textContent='Failed to load chat: '+x.message;}}"
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
        "else{p.dataset.raw=content;p.innerHTML=renderMarkdown(content);}"
        "box.append(p);return p;}"
        // Inline span-level formatting within a single line: backtick code
        // spans and **bold**. Operates on already-HTML-escaped text so the
        // replacement groups never need escaping themselves.
        "function renderInline(text){let t=esc(text);"
        "t=t.replace(/`([^`]+)`/g,(m,c)=>'<code>'+c+'</code>');"
        "t=t.replace(/\\*\\*([^*]+)\\*\\*/g,(m,b)=>'<strong>'+b+'</strong>');"
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
        "catch(x){s.textContent='Delete Machine Learning project failed: '+"
        "x.message;}});}}"
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
        "catch(x){s.textContent='Update model state failed: '+x.message;}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-model]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/models/'+"
        "encodeURIComponent(btn.dataset.deleteMlModel)+'/delete','POST');"
        "await load();}"
        "catch(x){s.textContent='Delete model failed: '+x.message;}});}}"
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
        "catch(x){s.textContent='Approve dataset failed: '+x.message;}});}"
        "for(const btn of el.querySelectorAll('[data-reject-ml-dataset]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/datasets/'+"
        "encodeURIComponent(btn.dataset.rejectMlDataset)+'/approve','POST',"
        "{status:'rejected'});await load();}"
        "catch(x){s.textContent='Reject dataset failed: '+x.message;}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-dataset]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/datasets/'+"
        "encodeURIComponent(btn.dataset.deleteMlDataset)+'/delete','POST');"
        "await load();}"
        "catch(x){s.textContent='Delete dataset failed: '+x.message;}});}}"
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
        "catch(x){s.textContent='Queue failed: '+x.message;}}"
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
        "catch(x){s.textContent='Download failed: '+x.message;}"
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
        "catch(x){s.textContent='Pause failed: '+x.message;}}"
        "async function stopDownload(id){const s=q('#actionStatus');"
        "try{await api('/api/v1/model-downloads/'+encodeURIComponent(id)+'/cancel','POST');"
        "s.textContent='Stopping download '+id+'...';await load();}"
        "catch(x){s.textContent='Stop failed: '+x.message;}}"
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
        "catch(x){s.textContent='Remove failed: '+x.message;}}"
        "async function createUser(e){e.preventDefault();const s=q('#actionStatus');"
        "try{await api('/api/v1/users','POST',{username:q('#newUserName').value,"
        "displayName:q('#newUserDisplay').value,role:q('#newUserRole').value,"
        "password:q('#newUserPassword').value});"
        "s.textContent='Local account created.';q('#newUser').reset();await load();}"
        "catch(x){s.textContent='Create account failed: '+x.message;}}"
        "function fill(sel,items,key,label){const e=q(sel);if(!e)return;e.replaceChildren();"
        "for(const x of items){const o=document.createElement('option');o.value=key(x);"
        "o.textContent=label(x);e.append(o);}}"
        "async function submit(e,path,body){e.preventDefault();const s=q('#actionStatus');"
        "try{const r=await api(path,'POST',body());s.textContent='Completed: '+JSON.stringify(r);"
        "await load();}catch(x){s.textContent='Action failed: '+x.message;}}"
        // Uploads one picked file as a project attachment (text only, per
        // AttachmentStore::add_text) and adds it to the pending list for the
        // message currently being composed; attachmentIds are read off that
        // list at send time in streamMessage() rather than a hidden field.
        "async function attachFile(file){const s=q('#actionStatus');"
        "try{const content=await file.text();"
        "const record=await api('/api/v1/attachments','POST',"
        "{projectId:currentProjectId(),filename:file.name,content});"
        "attachedFiles.push({id:record.id,filename:file.name});renderAttachChips();}"
        "catch(x){s.textContent='Attach \\''+file.name+'\\' failed: '+x.message;}}"
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
        // Sync the hidden chat-id field before reloading so load() takes the
        // openChat() branch (hides the empty-state greeting, clears/repopulates
        // #chatMessages, disables the now-fixed project/model pickers) instead
        // of re-running the brand-new-chat branch, which would leave the
        // greeting visible at the same time as the message about to be
        // appended below.
        "q('#messageChat').value=chatId;"
        "history.pushState(null,'','/app/chat/'+encodeURIComponent(chatId));"
        "await load();}"
        "q('#messageContent').value='';"
        "const attachmentIds=attachedFiles.map(x=>x.id);"
        "attachedFiles=[];renderAttachChips();"
        "appendMessage(box,'user',content);box.scrollTop=box.scrollHeight;"
        "generation=new AbortController();"
        "assistantEl=appendMessage(box,'assistant','');"
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
        "assistantEl.innerHTML=renderMarkdown(assistantEl.dataset.raw);"
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
        "s.textContent=cancelled?'Cancelled.':'Action failed: '+x.message;"
        // A cancelled turn already left whatever partial reply the user saw
        // in assistantEl (the server persists it too, see streamed_text in
        // send_chat_message) -- overwriting it here would erase text the
        // user already read. A real failure, though, previously left an
        // empty bubble with the only explanation in the easy-to-miss status
        // line above the composer; showing it inside the reply's own bubble
        // (or as a new one, if the failure happened before any bubble
        // existed -- e.g. no project/model chosen yet) puts it where the
        // reply itself would have appeared.
        "if(!cancelled){const msg='\\u26a0\\ufe0f '+x.message;"
        "const el=assistantEl||appendMessage(box,'assistant','');"
        "el.className='chatMsg chatMsg-assistant chatMsg-error';"
        "el.dataset.raw=msg;el.innerHTML=renderMarkdown(msg);"
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
        // Fires when the composer's model picker changes while a chat is
        // already open -- persists the new model against the chat so it's
        // still selected (and used) on the next message and after a reload.
        // A brand-new chat (no id yet) has nothing to persist against, so
        // it's left alone; the chosen model just gets used when the chat is
        // created on first send, as before.
        "async function changeChatModel(){const chatId=q('#messageChat').value;"
        "if(!chatId)return;const s=q('#actionStatus');const select=q('#chatModel');"
        "const modelId=select.value;"
        "try{await api('/api/v1/chats/'+encodeURIComponent(chatId)+'/model','POST',"
        "{modelId});"
        // Confirms the switch inline, in the transcript itself, rather than
        // only in the easy-to-miss status line -- so it's obvious which
        // replies below this point came from the new model.
        "const box=q('#chatMessages');"
        "if(box){const opt=select.options[select.selectedIndex];"
        "const card=document.createElement('div');"
        "card.className='chatMsg chatMsg-modelChange';"
        "card.textContent='Model switched to '+(opt?opt.textContent:modelId);"
        "box.append(card);box.scrollTop=box.scrollHeight;}}"
        "catch(x){s.textContent='Failed to switch model: '+x.message;}}"
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
        "dataFormat:q('#mlDatasetFormat').value})));}});";
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
            "</div><div>"
            "<h2>Registered datasets</h2>"
            "<div id=\"mlDatasetsList\">Loading...</div>"
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
        "<h3>Chats</h3><div id=\"chatList\"></div>"
        // Older conversations (anything past the most recent 20) render
        // here instead, so the primary list above stays a fixed, scannable
        // size. Hidden by default; renderChatList() reveals it once there
        // is anything to show.
        "<details id=\"chatHistorySection\" hidden><summary>History</summary>"
        "<div id=\"chatHistoryList\"></div></details>";
    if (can_manage_settings) {
        sidebar_links += "<h3>Workspace</h3>" +
                         nav_link("/app/projects", "Projects", section == "projects") +
                         "<h3>Settings</h3>" +
                         nav_link("/app/models/inventory", "Model inventory",
                                  section == "models-inventory") +
                         nav_link("/app/models/download", "Download a model",
                                  section == "models-download") +
                         nav_link("/app/models/benchmarks", "Benchmarks",
                                  section == "models-benchmarks");
    }
    if (is_administrator) {
        sidebar_links += "<h3>Machine Learning</h3>" +
                         nav_link("/app/ml", "Dashboard",
                                  section == "ml-dashboard") +
                         nav_link("/app/ml/projects", "Projects",
                                  section == "ml-projects") +
                         nav_link("/app/ml/models", "Model Registry",
                                  section == "ml-models") +
                         nav_link("/app/ml/datasets", "Dataset Manager",
                                  section == "ml-datasets") +
                         "<h3>Admin</h3>" +
                         nav_link("/app/admin/create", "Create user",
                                  section == "admin-create") +
                         nav_link("/app/admin/users", "User list",
                                  section == "admin-users");
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
        "#sidebar h1{font-size:1.3rem}"
        "#sidebar h3{margin-top:1rem}"
        "#content{flex:1;padding:1.5rem;overflow-y:auto;max-width:1200px;"
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
        "#chatMessages{flex:1;overflow-y:auto;padding:.25rem 0;"
        "display:flex;flex-direction:column;gap:.35rem}"
        "#chatMessages:empty{flex:0}"
        ".chatMsg{max-width:80%;margin:0;padding:.45rem .8rem;line-height:1.45;"
        "border-radius:.9rem;overflow-wrap:anywhere}"
        ".chatMsg-user{margin-left:auto;background:var(--accent);color:#fff}"
        ".chatMsg-assistant{margin-right:auto;background:var(--panel);"
        "border:1px solid var(--panel-border);color:#8fe6c9}"
        ".chatMsg-system{margin:0 auto;color:var(--muted);font-style:italic;"
        "background:none;white-space:pre-wrap}"
        ".chatMsg-error{background:rgba(220,53,69,.12);"
        "border:1px solid rgba(220,53,69,.5);color:#e5657a}"
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
        ".chatMsg pre{margin:0 0 .5rem;background:#0e0e11;"
        "border:1px solid var(--panel-border);border-radius:.5rem;"
        "padding:.6rem .75rem;overflow-x:auto;overflow-y:auto;max-height:24rem;"
        "overscroll-behavior:contain;scrollbar-width:thin}"
        ".chatMsg pre::-webkit-scrollbar{width:.5rem;height:.5rem}"
        ".chatMsg pre::-webkit-scrollbar-thumb{background:var(--panel-border);"
        "border-radius:.5rem}"
        ".chatMsg pre code{background:none;padding:0;color:#c8c8d4;"
        "white-space:pre}"
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
        "</style></head><body>"
        "<div id=\"shell\" data-role=\"" + role_attr + "\"><nav id=\"sidebar\">"
        "<h1>MasterAI</h1><p id=\"who\">" +
        html_escape(user.display_name) +
        "</p>" +
        sidebar_links +
        "</nav><main id=\"content\">"
        "<p id=\"actionStatus\" role=\"status\"></p>" +
        body +
        "</main></div><script src=\"/assets/app.js\"></script></body></html>");
}

}  // namespace masterai::server_internal
