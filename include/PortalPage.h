#pragma once

#if defined(ARDUINO)
#include <Arduino.h>
#define CLOCK_PORTAL_PAGE_STORAGE PROGMEM
#else
#define CLOCK_PORTAL_PAGE_STORAGE
#endif

namespace portalpage {

constexpr char kHtml[] CLOCK_PORTAL_PAGE_STORAGE = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width">
<title>Kids Clock</title><style>
body{font:18px system-ui,sans-serif;max-width:32rem;margin:3rem auto;padding:1rem;
background:#101418;color:#f4f5f6;text-align:center}
#s{margin-top:1.5rem}</style></head><body>
<h1>Setting the clock</h1>
<p>This page automatically sends only this device's current time and time-zone offset.</p>
<p id="s" role="status" aria-live="polite">Reading this device's time...</p>
<script>
const maxAttempts=3;
const retryDelayMs=750;
const status=document.querySelector('#s');
function retry(attempt){
  status.textContent='Connection interrupted. Retrying...';
  setTimeout(()=>setTime(attempt+1),retryDelayMs);
}
async function setTime(attempt=1){
  status.textContent=attempt===1?'Setting the clock...':'Retrying...';
  const body='epoch='+Math.floor(Date.now()/1000)+
    '&offset='+(-new Date().getTimezoneOffset());
  try{
    const response=await fetch('/set-time',{method:'POST',headers:
      {'Content-Type':'application/x-www-form-urlencoded'},body});
    if(response.ok){
      status.textContent='Done. You can close this page.';
    }else if(response.status>=500&&attempt<maxAttempts){
      retry(attempt);
    }else{
      status.textContent='Could not set the time. Rejoin the clock network to retry.';
    }
  }catch(error){
    if(attempt<maxAttempts){
      retry(attempt);
    }else{
      status.textContent='Connection lost. Check the clock display; rejoin the network if needed.';
    }
  }
}
document.addEventListener('DOMContentLoaded',()=>setTime(),{once:true});
</script></body></html>)HTML";

}  // namespace portalpage

#undef CLOCK_PORTAL_PAGE_STORAGE
