// MasterAI dependency-free loopback browser presentation.
//
// This unit contains only static HTML and JavaScript documents. Moving browser
// presentation out of the HTTP router keeps transport, policy, and UI concerns
// independently reviewable without introducing a web framework.
#include "server_internal.hpp"

namespace masterai::server_internal {
// Supplies the small fetch/streaming client used by both browser documents.
std::string application_script() {
    return
        "const q=s=>document.querySelector(s);let csrf='',generation=null;"
        "async function api(path,method='GET',body){const h={};"
        "if(body)h['Content-Type']='application/json';if(csrf)h['X-CSRF-Token']=csrf;"
        "const r=await fetch(path,{method,headers:h,body:body?JSON.stringify(body):undefined});"
        "const t=await r.text();if(!r.ok)throw new Error(t||r.status);"
        "return t?JSON.parse(t):{};}"
        "async function login(e){e.preventDefault();try{const d=await api('/api/v1/auth/login','POST',"
        "{username:q('#username').value,password:q('#password').value});"
        "sessionStorage.setItem('csrf',d.csrfToken);location.href='/app';}"
        "catch(x){q('#status').textContent='Login failed. Check your OS account mapping.';}}"
        "async function load(){csrf=sessionStorage.getItem('csrf')||'';try{"
        "const [me,p,c,m,b]=await Promise.all([api('/api/v1/users/me'),api('/api/v1/projects'),"
        "api('/api/v1/chats'),api('/api/v1/models'),api('/api/v1/benchmarks')]);"
        "q('#who').textContent=me.displayName+' ('+me.role+')';"
        "q('#projects').textContent=JSON.stringify(p.projects,null,2);"
        "q('#chats').textContent=JSON.stringify(c.chats,null,2);"
        "q('#models').textContent=JSON.stringify(m.models,null,2);"
        "q('#benchmarks').textContent=JSON.stringify(b.benchmarks,null,2);"
        "fill('#chatProject',p.projects,x=>x.id,x=>x.displayName);"
        "fill('#attachmentProject',p.projects,x=>x.id,x=>x.displayName);"
        "fill('#chatModel',m.models.filter(x=>x.state==='ready'),x=>x.id,x=>x.displayName);"
        "fill('#messageChat',c.chats,x=>x.id,x=>x.id+' / '+x.modelId);}"
        "catch(x){location.href='/';}}"
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
        "addEventListener('DOMContentLoaded',()=>{const f=q('#login');if(f)f.addEventListener('submit',login);"
        "if(q('#workspace')){load();q('#newProject').addEventListener('submit',e=>submit(e,'/api/v1/projects',"
        "()=>({id:q('#projectId').value,displayName:q('#projectName').value})));"
        "q('#newChat').addEventListener('submit',e=>submit(e,'/api/v1/chats',"
        "()=>({projectId:q('#chatProject').value,modelId:q('#chatModel').value})));"
        "q('#newAttachment').addEventListener('submit',e=>submit(e,'/api/v1/attachments',"
        "()=>({projectId:q('#attachmentProject').value,filename:q('#attachmentName').value,"
        "content:q('#attachmentContent').value})));"
        "q('#newMessage').addEventListener('submit',streamMessage);"
        "q('#cancelMessage').addEventListener('click',()=>{if(generation)generation.abort();});}});";
}

// Presents the native OS account sign-in form without embedding credentials.
std::string login_page() {
    return html_response(
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>MasterAI Login</title><style>body{font:16px system-ui;"
        "max-width:34rem;margin:8vh auto;padding:1rem}label{display:block;"
        "margin-top:1rem}input,button{font:inherit;padding:.65rem;width:100%;"
        "box-sizing:border-box}button{margin-top:1.25rem}</style></head>"
        "<body><h1>MasterAI</h1><p>Sign in with an operating-system account "
        "mapped by your administrator.</p><form id=\"login\"><label for=\"username\">"
        "OS user name</label><input id=\"username\" autocomplete=\"username\" required>"
        "<label for=\"password\">Password</label><input id=\"password\" type=\"password\" "
        "autocomplete=\"current-password\" required><button>Sign in</button></form>"
        "<p id=\"status\" role=\"status\"></p><script src=\"/assets/app.js\"></script>"
        "</body></html>");
}

// Presents project, chat, model, attachment, and benchmark operations.
std::string application_page(const UserRecord& user) {
    return html_response(
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>MasterAI Workspace</title><style>body{font:15px system-ui;"
        "max-width:1200px;margin:auto;padding:1rem}main{display:grid;"
        "grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:1rem}"
        "section{border:1px solid #bbb;border-radius:.5rem;padding:1rem}"
        "pre{white-space:pre-wrap;overflow-wrap:anywhere}label{display:block;"
        "margin-top:.6rem}input,select,textarea,button{font:inherit;width:100%;"
        "box-sizing:border-box;padding:.45rem}button{margin-top:.7rem}</style></head><body>"
        "<header><h1>Programming workspace</h1><p id=\"who\">" +
        html_escape(user.display_name) +
        "</p><p id=\"actionStatus\" role=\"status\"></p></header>"
        "<main id=\"workspace\"><section><h2>Projects</h2>"
        "<form id=\"newProject\"><label>Project ID<input id=\"projectId\" required "
        "pattern=\"[A-Za-z0-9_.-]+\"></label><label>Name<input id=\"projectName\" "
        "required></label><button>Create project</button></form>"
        "<pre id=\"projects\">Loading...</pre></section><section><h2>Chats</h2>"
        "<form id=\"newChat\"><label>Project<select id=\"chatProject\"></select></label>"
        "<label>Ready model<select id=\"chatModel\"></select></label>"
        "<button>New chat</button></form><form id=\"newMessage\">"
        "<label>Chat<select id=\"messageChat\"></select></label>"
        "<label>Message<textarea id=\"messageContent\" required></textarea></label>"
        "<label>Attachment IDs (comma separated)<input id=\"messageAttachments\"></label>"
        "<button>Send and stream</button><button id=\"cancelMessage\" type=\"button\">"
        "Cancel generation</button></form><pre id=\"chats\">Loading...</pre></section>"
        "<section><h2>Models</h2><pre id=\"models\">Loading...</pre></section>"
        "<section><h2>Attachments</h2><form id=\"newAttachment\">"
        "<label>Project<select id=\"attachmentProject\"></select></label>"
        "<label>Filename<input id=\"attachmentName\" required></label>"
        "<label>UTF-8 text<textarea id=\"attachmentContent\" required></textarea></label>"
        "<button>Store attachment</button></form></section>"
        "<section><h2>Benchmarks</h2><pre id=\"benchmarks\">Loading...</pre>"
        "</section></main><script src=\"/assets/app.js\"></script></body></html>");
}

}  // namespace masterai::server_internal
