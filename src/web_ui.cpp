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
    "#actionStatus,#status{color:var(--muted);min-height:1.2em}"

namespace masterai::server_internal {
// Supplies the small fetch/streaming client used by both browser documents.
std::string application_script() {
    return
        "const q=s=>document.querySelector(s);let csrf='',generation=null,"
        "lastAppliedPreset=null;const downloadSizes={};"
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
        "const [me,p,c,m,b,d,u]=await Promise.all([api('/api/v1/users/me'),api('/api/v1/projects'),"
        "api('/api/v1/chats'),api('/api/v1/models'),api('/api/v1/benchmarks'),"
        "api('/api/v1/model-downloads').catch(()=>({downloads:[]})),"
        "api('/api/v1/users').catch(()=>({users:[]}))]);"
        "q('#who').textContent=me.displayName+' ('+me.role+')';"
        "q('#projects').textContent=JSON.stringify(p.projects,null,2);"
        "q('#models').textContent=JSON.stringify(m.models,null,2);"
        "q('#benchmarks').textContent=JSON.stringify(b.benchmarks,null,2);"
        "if(q('#downloads'))q('#downloads').textContent=JSON.stringify(d.downloads,null,2);"
        "if(q('#users'))q('#users').textContent=u.users.length?"
        "JSON.stringify(u.users,null,2):'(none, or administrator access required)';"
        "fill('#chatProject',p.projects,x=>x.id,x=>x.displayName);"
        "fill('#attachmentProject',p.projects,x=>x.id,x=>x.displayName);"
        "fill('#chatModel',m.models.filter(x=>x.state==='ready'),x=>x.id,x=>x.displayName);"
        "renderChatList(c.chats);}"
        "catch(x){location.href='/';}}"
        // Renders the sidebar's chat-history list; clicking an entry
        // navigates to '#chat/<id>', which the hash router below resolves
        // by fetching that chat's full message history.
        "function renderChatList(chats){const list=q('#chatList');if(!list)return;"
        "list.replaceChildren();"
        "for(const c of chats){const btn=document.createElement('button');"
        "btn.type='button';btn.className='navButton';"
        "btn.textContent=c.id+' ('+c.modelId+')';"
        "btn.addEventListener('click',()=>{location.hash='chat/'+c.id;});"
        "list.append(btn);}}"
        // Loads one chat's full message history (via the GET
        // /api/v1/chats/{id} route) into the chat panel.
        "async function openChat(id){const s=q('#actionStatus');"
        "try{const chat=await api('/api/v1/chats/'+encodeURIComponent(id));"
        "q('#messageChat').value=id;q('#currentChatId').textContent=id;"
        "const box=q('#chatMessages');box.replaceChildren();"
        "for(const message of chat.messages){const p=document.createElement('p');"
        "p.className='chatMsg chatMsg-'+message.role;"
        "p.textContent=message.role+': '+message.content;box.append(p);}"
        "box.scrollTop=box.scrollHeight;}"
        "catch(x){s.textContent='Failed to load chat: '+x.message;}}"
        // Shows exactly one top-level panel and highlights the matching nav
        // button, driven by the '#<panel>' or '#chat/<id>' location hash so
        // switching sections never reloads the page.
        "function showPanel(name){"
        "for(const id of ['chat','projects','models','admin']){"
        "const el=q('#panel-'+id);if(el)el.hidden=id!==name;}"
        "for(const btn of document.querySelectorAll('#sidebar [data-panel]')){"
        "btn.classList.toggle('active',btn.dataset.panel===name);}}"
        "function route(){const hash=(location.hash||'').replace(/^#/,'')||'chat';"
        "const [kind,id]=hash.split('/');"
        "showPanel(kind==='chat'?'chat':kind);"
        "if(kind==='chat'&&id)openChat(id);}"
        "async function createChatHandler(e){e.preventDefault();const s=q('#actionStatus');"
        "try{const r=await api('/api/v1/chats','POST',"
        "{projectId:q('#chatProject').value,modelId:q('#chatModel').value});"
        "s.textContent='Chat created: '+r.id;await load();location.hash='chat/'+r.id;}"
        "catch(x){s.textContent='Action failed: '+x.message;}}"
        // Curated, size-tiered GGUF suggestions. Every commit hash and SHA-256
        // below was read directly from the Hugging Face API so the server's
        // immutable-revision and digest checks pass without hand-editing.
        "const PRESETS=["
        "{id:'tiny-test',tier:'test',label:'ggml-org/models tinyllamas stories260K (~1 MB)',"
        "category:'general-programming',modelId:'stories260k-test',filename:'stories260K.gguf',"
        "sourceUrl:'https://huggingface.co/ggml-org/models/resolve/499bc8821c6b12b4e53c5bffcb21ec206f212d81/tinyllamas/stories260K.gguf',"
        "revision:'499bc8821c6b12b4e53c5bffcb21ec206f212d81',"
        "sha256:'270cba1bd5109f42d03350f60406024560464db173c0e387d91f0426d3bd256d',"
        "minRam:64,recRam:128,sizeBytes:1185376},"
        "{id:'qwen25-1.5b-q4km',tier:'2',label:'Qwen2.5-Coder-1.5B-Instruct Q4_K_M (~1.1 GiB)',"
        "category:'general-programming',modelId:'qwen25-coder-1.5b-q4km',"
        "filename:'qwen2.5-coder-1.5b-instruct-q4_k_m.gguf',"
        "sourceUrl:'https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/f86cb2c1fa58255f8052cc32aeede1b7482d4361/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf',"
        "revision:'f86cb2c1fa58255f8052cc32aeede1b7482d4361',"
        "sha256:'cc324af070c2ecbfd324a30884d2f951a7ff756aba85cb811a6ec436933bb046',"
        "minRam:1536,recRam:2048,sizeBytes:1117320768},"
        "{id:'qwen25-3b-q4km',tier:'4',label:'Qwen2.5-Coder-3B-Instruct Q4_K_M (~2.0 GiB)',"
        "category:'general-programming',modelId:'qwen25-coder-3b-q4km',"
        "filename:'qwen2.5-coder-3b-instruct-q4_k_m.gguf',"
        "sourceUrl:'https://huggingface.co/Qwen/Qwen2.5-Coder-3B-Instruct-GGUF/resolve/f74adce6aa16316c625447af059dbebe4983757c/qwen2.5-coder-3b-instruct-q4_k_m.gguf',"
        "revision:'f74adce6aa16316c625447af059dbebe4983757c',"
        "sha256:'724fb256bec1ff062b2f65e4569e871ad2e95ab2a3989723d1769c54294730b7',"
        "minRam:3072,recRam:4096,sizeBytes:2104932800},"
        "{id:'qwen25-7b-q4km',tier:'8',label:'Qwen2.5-Coder-7B-Instruct Q4_K_M (~4.4 GiB)',"
        "category:'general-programming',modelId:'qwen25-coder-7b-q4km',"
        "filename:'qwen2.5-coder-7b-instruct-q4_k_m.gguf',"
        "sourceUrl:'https://huggingface.co/Qwen/Qwen2.5-Coder-7B-Instruct-GGUF/resolve/13fb94bfda8c8cf22497dc57b78f391a9acb426a/qwen2.5-coder-7b-instruct-q4_k_m.gguf',"
        "revision:'13fb94bfda8c8cf22497dc57b78f391a9acb426a',"
        "sha256:'509287f78cb4d4cf6b3843734733b914b2c158e43e22a7f4bf5e963800894d3c',"
        "minRam:6144,recRam:8192,sizeBytes:4683073536},"
        "{id:'qwen25-14b-q4km',tier:'16',label:'Qwen2.5-Coder-14B-Instruct Q4_K_M (~8.4 GiB)',"
        "category:'general-programming',modelId:'qwen25-coder-14b-q4km',"
        "filename:'qwen2.5-coder-14b-instruct-q4_k_m.gguf',"
        "sourceUrl:'https://huggingface.co/Qwen/Qwen2.5-Coder-14B-Instruct-GGUF/resolve/d0a692ef765eefbf2fabb130b3cb2e8917e3d225/qwen2.5-coder-14b-instruct-q4_k_m.gguf',"
        "revision:'d0a692ef765eefbf2fabb130b3cb2e8917e3d225',"
        "sha256:'c1e659736d89ac1065fb495330fb824d94001974a4bfa78e7270e43476a8d940',"
        "minRam:11264,recRam:16384,sizeBytes:8988110272},"
        "{id:'qwen25-32b-q4km',tier:'24',label:'Qwen2.5-Coder-32B-Instruct Q4_K_M (~18.5 GiB)',"
        "category:'general-programming',modelId:'qwen25-coder-32b-q4km',"
        "filename:'qwen2.5-coder-32b-instruct-q4_k_m.gguf',"
        "sourceUrl:'https://huggingface.co/Qwen/Qwen2.5-Coder-32B-Instruct-GGUF/resolve/9d3053fce650fe1cdbdb75998c2a87add9d178ef/qwen2.5-coder-32b-instruct-q4_k_m.gguf',"
        "revision:'9d3053fce650fe1cdbdb75998c2a87add9d178ef',"
        "sha256:'4d64b316b5e6319d9613e0d97935d9ebd631fc7e334da400d00085eca749d085',"
        "minRam:20480,recRam:24576,sizeBytes:19851335872},"
        "{id:'qwen25-32b-q6k',tier:'32',label:'Qwen2.5-Coder-32B-Instruct Q6_K (~25 GiB)',"
        "category:'general-programming',modelId:'qwen25-coder-32b-q6k',"
        "filename:'qwen2.5-coder-32b-instruct-q6_k.gguf',"
        "sourceUrl:'https://huggingface.co/Qwen/Qwen2.5-Coder-32B-Instruct-GGUF/resolve/9d3053fce650fe1cdbdb75998c2a87add9d178ef/qwen2.5-coder-32b-instruct-q6_k.gguf',"
        "revision:'9d3053fce650fe1cdbdb75998c2a87add9d178ef',"
        "sha256:'38c1555adabcc7e9dfdae217cfbfdea53c97996a1ca17bd00125cf32bbdc63c2',"
        "minRam:27648,recRam:32768,sizeBytes:26886154432},"
        "{id:'qwen25-32b-q8',tier:'64',label:'Qwen2.5-Coder-32B-Instruct Q8_0 (~32.4 GiB)',"
        "category:'general-programming',modelId:'qwen25-coder-32b-q8',"
        "filename:'qwen2.5-coder-32b-instruct-q8_0.gguf',"
        "sourceUrl:'https://huggingface.co/Qwen/Qwen2.5-Coder-32B-Instruct-GGUF/resolve/9d3053fce650fe1cdbdb75998c2a87add9d178ef/qwen2.5-coder-32b-instruct-q8_0.gguf',"
        "revision:'9d3053fce650fe1cdbdb75998c2a87add9d178ef',"
        "sha256:'ae6e5cee79233499b41502ea7270e665dc402b2c35c48d43ef2d5a0a10842725',"
        "minRam:36864,recRam:49152,sizeBytes:34820884672}];"
        // Rebuilds the suggestion dropdown for the selected RAM tier and
        // immediately applies the first match. Auto-applying here (rather
        // than requiring a separate button click) is what prevents the
        // stale-form trap: switching tiers can no longer leave the form
        // holding a previous tier's model while looking like it was updated.
        "function refreshPresets(){const sel=q('#downloadPreset');if(!sel)return;"
        "sel.replaceChildren();const tier=q('#downloadTier').value;"
        "for(const p of PRESETS.filter(x=>x.tier===tier)){"
        "const o=document.createElement('option');o.value=p.id;o.textContent=p.label;"
        "sel.append(o);}"
        "if(sel.options.length)applyPreset();}"
        // Copies the selected preset's verified fields into the raw form;
        // license acceptance is left to the operator, never auto-checked.
        "function applyPreset(){const p=PRESETS.find(x=>x.id===q('#downloadPreset').value);"
        "if(!p)return;lastAppliedPreset=p;q('#downloadCategory').value=p.category;"
        "q('#downloadModelId').value=p.modelId;q('#downloadFilename').value=p.filename;"
        "q('#downloadSourceUrl').value=p.sourceUrl;q('#downloadRevision').value=p.revision;"
        "q('#downloadSha256').value=p.sha256;q('#downloadMinRam').value=p.minRam;"
        "q('#downloadRecRam').value=p.recRam;"
        "q('#actionStatus').textContent='Loaded suggestion: '+p.label+"
        "'. Review its license on Hugging Face before accepting.';}"
        "async function queueDownload(e){e.preventDefault();const s=q('#actionStatus');"
        "try{const body={filename:q('#downloadFilename').value,"
        "category:q('#downloadCategory').value,modelId:q('#downloadModelId').value,"
        "sourceUrl:q('#downloadSourceUrl').value,immutableRevision:q('#downloadRevision').value,"
        "expectedSha256:q('#downloadSha256').value,licenseAccepted:q('#downloadLicense').checked,"
        "minimumRamMiB:Number(q('#downloadMinRam').value),"
        "recommendedRamMiB:Number(q('#downloadRecRam').value)};"
        "const r=await api('/api/v1/model-downloads','POST',body);"
        "q('#downloadRunId').value=r.id;"
        // Only trust a preset's known total size for the progress bar when
        // the form still matches that preset (the operator may have edited
        // the source URL by hand after loading a suggestion).
        "downloadSizes[r.id]=lastAppliedPreset&&"
        "lastAppliedPreset.sourceUrl===body.sourceUrl?lastAppliedPreset.sizeBytes:null;"
        "s.textContent='Queued download '+r.id+' ('+r.hardwareRecommendation+'). "
        "Click Start / resume transfer to begin.';await load();}"
        "catch(x){s.textContent='Queue failed: '+x.message;}}"
        // Polls the existing job-list route (the /run response itself only
        // arrives once the whole transfer finishes) and renders a progress
        // bar from each job's completedBytes, using the known total size
        // when available or falling back to a plain byte counter.
        "async function pollDownloadProgress(id){const bar=q('#downloadProgress'),"
        "text=q('#downloadProgressText');if(!bar||!text)return null;"
        "const total=downloadSizes[id];bar.hidden=false;text.hidden=false;"
        "if(total){bar.removeAttribute('indeterminate');bar.max=total;}"
        "else bar.removeAttribute('max');"
        "const tick=async()=>{try{const d=await api('/api/v1/model-downloads');"
        "const job=d.downloads.find(x=>x.id===id);if(!job)return;"
        "const mib=(job.completedBytes/1048576).toFixed(1);"
        "if(total){bar.value=job.completedBytes;"
        "text.textContent=job.state+': '+mib+' / '+(total/1048576).toFixed(1)+"
        "' MiB ('+Math.min(100,Math.round(job.completedBytes/total*100))+'%)';}"
        "else text.textContent=job.state+': '+mib+' MiB downloaded';"
        "}catch(x){}};"
        "await tick();return setInterval(tick,1000);}"
        // The server runs the transfer synchronously and only responds once
        // the job finishes, so this request can legitimately stay pending
        // for a long time on a large model; the progress bar above is what
        // fills that gap.
        "async function runDownloadJob(e){e.preventDefault();const s=q('#actionStatus');"
        "const id=q('#downloadRunId').value;"
        "s.textContent='Downloading '+id+'... this can take a long time for large models; "
        "the page will wait for it to finish.';"
        "const timer=await pollDownloadProgress(id);"
        "try{const r=await api('/api/v1/model-downloads/'+encodeURIComponent(id)+'/run','POST');"
        "s.textContent='Download '+id+' finished with state: '+r.state;await load();}"
        "catch(x){s.textContent='Download failed: '+x.message;}"
        "finally{if(timer)clearInterval(timer);const bar=q('#downloadProgress'),"
        "text=q('#downloadProgressText');if(bar)bar.hidden=true;if(text)text.hidden=true;}}"
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
        "async function streamMessage(e){e.preventDefault();const s=q('#actionStatus');"
        "generation=new AbortController();s.textContent='';try{const r=await fetch("
        "'/api/v1/chats/'+encodeURIComponent(q('#messageChat').value)+'/messages',"
        "{method:'POST',headers:{'Content-Type':'application/json','X-CSRF-Token':csrf},"
        "body:JSON.stringify({content:q('#messageContent').value,attachmentIds:"
        "q('#messageAttachments').value.split(',').map(x=>x.trim()).filter(Boolean)}),"
        "signal:generation.signal});"
        "if(!r.ok)throw new Error(await r.text());const reader=r.body.getReader(),decoder=new TextDecoder();"
        "let pending='';for(;;){const x=await reader.read();if(x.done)break;pending+=decoder.decode(x.value,{stream:true});"
        "let n;while((n=pending.indexOf('\\n'))>=0){const line=pending.slice(0,n);pending=pending.slice(n+1);"
        "if(line){const event=JSON.parse(line);if(event.type==='token')s.textContent+=event.content;"
        "if(event.type==='error')throw new Error(event.error);}}}await load();}"
        "catch(x){s.textContent+=x.name==='AbortError'?'\\nCancelled.':'\\nAction failed: '+x.message;}"
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
        "function toggleSourceFields(){const type=q('#downloadSourceType').value;"
        "q('#hfFields').hidden=type!=='huggingface';"
        "q('#githubFields').hidden=type!=='github';}"
        "addEventListener('DOMContentLoaded',()=>{const f=q('#login');"
        "if(f){f.addEventListener('submit',login);initLogin();"
        "q('#setupForm').addEventListener('submit',setupLocalAdmin);}"
        "if(q('#sidebar')){load();"
        "for(const btn of document.querySelectorAll('#sidebar [data-panel]')){"
        "btn.addEventListener('click',()=>{location.hash=btn.dataset.panel;});}"
        "q('#newChatBtn').addEventListener('click',()=>{location.hash='chat';"
        "q('#currentChatId').textContent='(none selected)';"
        "q('#chatMessages').replaceChildren();q('#messageChat').value='';});"
        "addEventListener('hashchange',route);route();"
        "q('#newProject').addEventListener('submit',e=>submit(e,'/api/v1/projects',"
        "()=>({id:q('#projectId').value,displayName:q('#projectName').value})));"
        "q('#newChat').addEventListener('submit',createChatHandler);"
        "q('#newAttachment').addEventListener('submit',e=>submit(e,'/api/v1/attachments',"
        "()=>({projectId:q('#attachmentProject').value,filename:q('#attachmentName').value,"
        "content:q('#attachmentContent').value})));"
        "q('#newMessage').addEventListener('submit',streamMessage);"
        "q('#cancelMessage').addEventListener('click',()=>{if(generation)generation.abort();});"
        "if(q('#downloadTier')){q('#downloadTier').addEventListener('change',refreshPresets);"
        "q('#downloadPreset').addEventListener('change',applyPreset);"
        "refreshPresets();q('#applyPreset').addEventListener('click',applyPreset);"
        "q('#newDownload').addEventListener('submit',queueDownload);"
        "q('#runDownload').addEventListener('submit',runDownloadJob);"
        "q('#downloadSourceType').addEventListener('change',toggleSourceFields);"
        "toggleSourceFields();"
        "q('#applyHfSource').addEventListener('click',applyHfSource);"
        "q('#applyGithubSource').addEventListener('click',applyGithubSource);}"
        "if(q('#newUser'))q('#newUser').addEventListener('submit',createUser);}});";
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

// Presents project, chat, model, attachment, and benchmark operations behind
// a persistent side panel (New Chat / Chat history / Projects / Admin), with
// a right-hand content area that swaps between panels client-side (see the
// hash router in application_script()) so switching sections never reloads
// the page.
std::string application_page(const UserRecord& user) {
    // The Admin nav entry is omitted entirely for non-administrators. This is
    // a presentation-only convenience -- every admin-only route (users.manage,
    // downloads.manage) still enforces its own permission check per request
    // regardless of what this page renders, exactly as before this panel
    // existed.
    const bool is_administrator = user.role == UserRole::administrator;
    return html_response(
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>MasterAI Workspace</title><style>" DARK_THEME_CSS
        "body{max-width:none;padding:0}"
        "#shell{display:flex;min-height:100vh}"
        "#sidebar{width:16rem;flex:none;padding:1.25rem 1rem;"
        "border-right:1px solid var(--panel-border);"
        "display:flex;flex-direction:column;gap:.5rem;overflow-y:auto}"
        "#sidebar h1{font-size:1.3rem}"
        "#sidebar h3{margin-top:1rem}"
        "#content{flex:1;padding:1.5rem;overflow-y:auto;max-width:1200px}"
        "#actionStatus{margin-bottom:1rem}"
        ".navButton{background:transparent;color:var(--text);border:1px solid "
        "transparent;text-align:left;margin-top:.15rem;padding:.5rem .6rem}"
        ".navButton:hover{background:var(--panel)}"
        ".navButton.active{background:var(--panel);border-color:var(--panel-border)}"
        "#chatList{display:flex;flex-direction:column;gap:.15rem;max-height:14rem;"
        "overflow-y:auto}"
        ".panel{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));"
        "gap:1.25rem}"
        "#chatMessages{max-height:24rem;overflow-y:auto;background:#0e0e11;"
        "border:1px solid var(--panel-border);border-radius:.5rem;padding:.75rem;"
        "margin-bottom:.75rem}"
        ".chatMsg{margin:0 0 .6rem;white-space:pre-wrap;overflow-wrap:anywhere}"
        ".chatMsg-user{color:var(--text)}.chatMsg-assistant{color:#8fe6c9}"
        ".chatMsg-system{color:var(--muted)}"
        "#hfFields,#githubFields{border:1px solid var(--panel-border);"
        "border-radius:.5rem;padding:.5rem .75rem;margin-top:.5rem}"
        "</style></head><body>"
        "<div id=\"shell\"><nav id=\"sidebar\">"
        "<h1>MasterAI</h1><p id=\"who\">" +
        html_escape(user.display_name) +
        "</p>"
        "<button type=\"button\" class=\"navButton\" id=\"newChatBtn\">"
        "+ New chat</button>"
        "<h3>Chats</h3><div id=\"chatList\"></div>"
        "<h3>Workspace</h3>"
        "<button type=\"button\" class=\"navButton\" data-panel=\"projects\">"
        "Projects</button>"
        "<button type=\"button\" class=\"navButton\" data-panel=\"models\">"
        "Models &amp; downloads</button>" +
        std::string(is_administrator
                        ? "<button type=\"button\" class=\"navButton\" "
                          "data-panel=\"admin\">Admin</button>"
                        : "") +
        "</nav><main id=\"content\">"
        "<p id=\"actionStatus\" role=\"status\"></p>"

        "<section id=\"panel-chat\" class=\"panel\"><div><h2>Chat</h2>"
        "<form id=\"newChat\"><label>Project<select id=\"chatProject\"></select></label>"
        "<label>Ready model<select id=\"chatModel\"></select></label>"
        "<button>Start chat</button></form>"
        "<p>Current chat: <span id=\"currentChatId\">(none selected)</span></p>"
        "<div id=\"chatMessages\"></div>"
        "<form id=\"newMessage\">"
        "<input type=\"hidden\" id=\"messageChat\">"
        "<label>Message<textarea id=\"messageContent\" required></textarea></label>"
        "<label>Attachment IDs (comma separated)<input id=\"messageAttachments\"></label>"
        "<button>Send and stream</button><button id=\"cancelMessage\" type=\"button\">"
        "Cancel generation</button></form></div>"
        "<div><h2>Attachments</h2><form id=\"newAttachment\">"
        "<label>Project<select id=\"attachmentProject\"></select></label>"
        "<label>Filename<input id=\"attachmentName\" required></label>"
        "<label>UTF-8 text<textarea id=\"attachmentContent\" required></textarea></label>"
        "<button>Store attachment</button></form></div></section>"

        "<section id=\"panel-projects\" class=\"panel\" hidden><div>"
        "<h2>Projects</h2>"
        "<form id=\"newProject\"><label>Project ID<input id=\"projectId\" required "
        "pattern=\"[A-Za-z0-9_.-]+\"></label><label>Name<input id=\"projectName\" "
        "required></label><button>Create project</button></form>"
        "<pre id=\"projects\">Loading...</pre></div></section>"

        "<section id=\"panel-models\" class=\"panel\" hidden><div>"
        "<h2>Models</h2><pre id=\"models\">Loading...</pre></div>"
        "<div><h2>Download a model</h2>"
        // Source selector: assembles a source URL/revision for the operator
        // instead of requiring one be hand-typed, without changing what's
        // actually submitted or validated -- the assembled fields still go
        // through the same required raw sourceUrl/revision/sha256 fields
        // and the same server-side revision-substring and post-download
        // SHA-256 checks as before.
        "<label>Download source<select id=\"downloadSourceType\">"
        "<option value=\"huggingface\">Hugging Face</option>"
        "<option value=\"github\">GitHub Releases</option>"
        "<option value=\"custom\">Custom URL</option>"
        "</select></label>"
        "<div id=\"hfFields\">"
        "<label>Org/repo (e.g. Qwen/Qwen2.5-Coder-7B-Instruct-GGUF)"
        "<input id=\"hfRepo\"></label>"
        "<label>Commit/revision<input id=\"hfRevision\"></label>"
        "<label>Filename<input id=\"hfFilename\"></label>"
        "<button type=\"button\" id=\"applyHfSource\">Fill source from these fields"
        "</button></div>"
        "<div id=\"githubFields\" hidden>"
        "<label>Owner/repo<input id=\"ghRepo\"></label>"
        "<label>Release tag<input id=\"ghTag\"></label>"
        "<label>Asset filename<input id=\"ghAsset\"></label>"
        "<button type=\"button\" id=\"applyGithubSource\">Fill source from these fields"
        "</button></div>"
        // Suggestion picker: fills the raw form below from a verified,
        // hard-coded preset so the operator never has to hand-type a
        // commit hash or a 64-character digest to get a working model.
        "<h3>Or pick a curated suggestion</h3>"
        "<label>Filter suggestions by available RAM<select id=\"downloadTier\">"
        "<option value=\"test\">Tiny test model (~1 MB) - any hardware</option>"
        "<option value=\"2\">2 GB</option>"
        "<option value=\"4\">4 GB</option>"
        "<option value=\"8\">8 GB</option><option value=\"16\">16 GB</option>"
        "<option value=\"24\">24 GB</option><option value=\"32\">32 GB</option>"
        "<option value=\"64\">64 GB or more</option></select></label>"
        "<label>Suggested model<select id=\"downloadPreset\"></select></label>"
        "<button type=\"button\" id=\"applyPreset\">Reset form to suggestion</button>"
        "<form id=\"newDownload\">"
        "<label>Category<select id=\"downloadCategory\">"
        "<option value=\"general-programming\">general-programming</option>"
        "<option value=\"code-completion\">code-completion</option>"
        "<option value=\"code-review\">code-review</option>"
        "<option value=\"debugging\">debugging</option>"
        "<option value=\"documentation\">documentation</option>"
        "<option value=\"embeddings-code-search\">embeddings-code-search</option>"
        "</select></label>"
        "<label>Model ID<input id=\"downloadModelId\" required "
        "pattern=\"[A-Za-z0-9_.-]+\"></label>"
        "<label>Filename<input id=\"downloadFilename\" required "
        "pattern=\"[A-Za-z0-9_.-]+\"></label>"
        "<label>Source URL (Hugging Face resolve URL or GitHub release URL)"
        "<input id=\"downloadSourceUrl\" type=\"url\" required></label>"
        "<label>Immutable revision (commit hash or release tag)"
        "<input id=\"downloadRevision\" required></label>"
        "<label>Expected SHA-256<input id=\"downloadSha256\" required "
        "pattern=\"[0-9a-fA-F]{64}\"></label>"
        "<label>Minimum RAM (MiB)<input id=\"downloadMinRam\" type=\"number\" "
        "min=\"1\" required></label>"
        "<label>Recommended RAM (MiB)<input id=\"downloadRecRam\" "
        "type=\"number\" min=\"1\" required></label>"
        "<label><input id=\"downloadLicense\" type=\"checkbox\" required> "
        "I have reviewed and accept this model's license</label>"
        "<button>Queue download</button></form>"
        "<form id=\"runDownload\"><label>Download job ID"
        "<input id=\"downloadRunId\" required></label>"
        "<button>Start / resume transfer</button></form>"
        "<progress id=\"downloadProgress\" hidden></progress>"
        "<p id=\"downloadProgressText\" hidden></p>"
        "<pre id=\"downloads\">Loading...</pre></div>"
        "<div><h2>Benchmarks</h2><pre id=\"benchmarks\">Loading...</pre></div>"
        "</section>" +

        std::string(is_administrator
                        ? "<section id=\"panel-admin\" class=\"panel\" hidden><div>"
                          "<h2>Administration</h2>"
                          "<form id=\"newUser\"><label>Username"
                          "<input id=\"newUserName\" required "
                          "pattern=\"[A-Za-z0-9_.-]+\"></label>"
                          "<label>Display name<input id=\"newUserDisplay\" "
                          "required></label>"
                          "<label>Role<select id=\"newUserRole\">"
                          "<option value=\"administrator\">administrator</option>"
                          "<option value=\"developer\">developer</option>"
                          "<option value=\"viewer\">viewer</option></select></label>"
                          "<label>Password (minimum 8 characters)"
                          "<input id=\"newUserPassword\" type=\"password\" "
                          "minlength=\"8\" required></label>"
                          "<button>Create local account</button></form>"
                          "<pre id=\"users\">Loading...</pre></div></section>"
                        : "") +
        "</main></div><script src=\"/assets/app.js\"></script></body></html>");
}

}  // namespace masterai::server_internal
