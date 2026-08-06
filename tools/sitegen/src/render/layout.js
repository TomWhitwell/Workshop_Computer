export function renderLayout({ title, content, relativeRoot = '.', repoUrl = 'https://github.com/TomWhitwell/Workshop_Computer' }) {
  return `<!doctype html>
<html lang="en" class="theme-dark" data-theme="dark">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>${title ? String(title).replace(/</g, '&lt;') : 'Workshop Computer'}</title>
  <script>(function(){try{var k='wc-theme';var d=document.documentElement;var prefersDark=window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches;var t=localStorage.getItem(k);if(t!=='dark'&&t!=='light'){t=prefersDark?'dark':'light';}d.classList.remove('theme-dark','theme-light');d.classList.add('theme-'+t);d.setAttribute('data-theme',t);}catch(e){}})();</script>
  <link rel="stylesheet" href="${relativeRoot}/assets/github-markdown.css" />
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700&display=swap" rel="stylesheet">
  <link rel="stylesheet" href="${relativeRoot}/assets/style.css" />
  <style>
    .filter-bar{margin:0 0 16px 0}
  .filter-row label{font-weight:600;color:var(--muted)}
  .filter-row select{background:transparent;border:1px solid var(--border);color:var(--text);padding:8px;border-radius:8px;width:100%}
  </style>
</head>
<body>
  <header class="site-header" id="page-top">
    <div class="container header-bar">
      <h1 class="site-title"><a href="${relativeRoot}/index.html">Workshop Computer Program Cards</a></h1>
      <div style="display:flex;align-items:center;gap:10px">
        <button id="connectToggle" class="connect-toggle" type="button" role="switch" aria-checked="false" aria-label="Connect to RP2040 via WebUSB" title="Reboot computer into programming mode before connecting">
          <span class="c-track"><span class="c-thumb"></span><span class="c-icons" aria-hidden="true">💾<span class="c-gap"></span>⚡</span></span>
        </button>
        <button id="themeToggle" class="theme-toggle" type="button" role="switch" aria-checked="true" aria-label="Toggle color scheme">
          <span class="track"><span class="thumb"></span><span class="icons" aria-hidden="true">☀️<span class="gap"></span>🌙</span></span>
        </button>
      </div>
    </div>
  </header>
  <main class="container">
    ${content}
  </main>
  <footer class="site-footer">
    <div class="container">
      <p>
        <a href="${repoUrl}" target="_blank" rel="noopener">View on GitHub</a>
        <span aria-hidden="true">•</span>
        <a href="https://www.musicthing.co.uk/workshopsystem/" target="_blank" rel="noopener">Music Thing Workshop System</a>
      </p>
    </div>
  </footer>
  <script type="module">
import { Picoboot } from '${relativeRoot}/assets/js/picoboot.js';
import { uf2ToFlashBuffer } from '${relativeRoot}/assets/js/uf2.js';

var connectBtn = document.getElementById('connectToggle');
var pb = null;

function setConnected(on) {
  if (connectBtn) {
    connectBtn.setAttribute('aria-checked', String(on));
    connectBtn.classList.toggle('active', on);
  }
  document.querySelectorAll('a.btn.download[data-uf2-url]').forEach(function(a) {
    if (on) {
      if (!a.dataset.origHtml) a.dataset.origHtml = a.innerHTML;
      a.innerHTML = '⚡ Program';
    } else {
      if (a.dataset.origHtml) { a.innerHTML = a.dataset.origHtml; delete a.dataset.origHtml; }
    }
  });
}

async function flash(url, el) {
  if (el.dataset.busy) return;
  el.dataset.busy = '1';
  try {
    el.innerHTML = '⏳ Fetching…';
    var r = await fetch(url);
    if (!r.ok) throw new Error('HTTP ' + r.status);
    var parsed = uf2ToFlashBuffer(new Uint8Array(await r.arrayBuffer()));
    el.innerHTML = '⏳ Flashing…';
    await pb.flashEraseAndWrite(parsed.address, parsed.data);
    el.innerHTML = '⏳ Verifying…';
    var readback = await pb.flashRead(parsed.address, parsed.data.length);
    for (var i = 0; i < parsed.data.length; i++) {
      if (readback[i] !== parsed.data[i]) throw new Error('Verify failed at byte ' + i);
    }
    el.innerHTML = '✅ Reset to use';
    // try { await pb.getConnection().reboot(500); } catch(_) {}
    setTimeout(function() { delete el.dataset.busy; el.innerHTML = '⚡ Program'; }, 3000);
  } catch(e) {
    el.innerHTML = '❌ Error';
    delete el.dataset.busy;
    setTimeout(function() { el.innerHTML = '⚡ Program'; }, 3000);
  }
}

if (!('usb' in navigator)) {
  if (connectBtn) { connectBtn.disabled = true; connectBtn.title = 'Use Chrome to program cards from this site'; }
} else {
  if (connectBtn) {
    connectBtn.addEventListener('click', async function() {
      var isOn = connectBtn.getAttribute('aria-checked') === 'true';
      if (isOn) {
        setConnected(false);
        return;
      }
      if (pb) {
        setConnected(true);
        return;
      }
      try {
        var dev = await Picoboot.requestDevice();
        await dev.connect();
        pb = dev;
        setConnected(true);
      } catch(e) {
        setConnected(false);
      }
    });
  }

  navigator.usb.addEventListener('disconnect', function(ev) {
    if (pb && pb.device === ev.device) {
      pb = null;
      setConnected(false);
    }
  });

  document.addEventListener('click', async function(e) {
    if (!pb) return;
    var a = e.target.closest('a.btn.download[data-uf2-url]');
    if (!a) return;
    e.preventDefault();
    await flash(a.dataset.uf2Url, a);
  });
}
  </script>
  <script>(function(){
    // Theme toggle
    var btn=document.getElementById('themeToggle');if(btn){var k='wc-theme';function cur(){return document.documentElement.getAttribute('data-theme')||'dark';}function set(t){try{localStorage.setItem(k,t);}catch(e){}var d=document.documentElement;d.classList.remove('theme-dark','theme-light');d.classList.add('theme-'+t);d.setAttribute('data-theme',t);update();}function update(){var t=cur();btn.setAttribute('aria-checked',String(t==='dark'));}btn.addEventListener('click',function(){set(cur()==='dark'?'light':'dark');});update();}

    // Filters (index only)
    var typeSel=document.getElementById('filter-type');
    var creatorSel=document.getElementById('filter-creator');
    var langSel=document.getElementById('filter-language');
    var searchInput=document.getElementById('filter-search');
    var searchClear=document.getElementById('search-clear');
    var sortCheckbox = document.getElementById('sort-latest');
    var countEl=document.getElementById('cards-count');
    
    function normTypeKey(v){
      return String(v||'').toLowerCase().replace(/\s+/g,' ').replace(/[^a-z0-9\s]+/g,' ').replace(/\s+/g,' ').trim();
    }
    
    function applyFilters(){
      var t=typeSel&&typeSel.value?normTypeKey(typeSel.value):'';
      var c=creatorSel&&creatorSel.value?creatorSel.value.toLowerCase():'';
      var l=langSel&&langSel.value?langSel.value.toLowerCase():'';
      var s=searchInput&&searchInput.value?searchInput.value.toLowerCase():'';
      
      if(searchClear) searchClear.style.display = s ? 'block' : 'none';

      var cards=document.querySelectorAll('.grid .card');
      var shown=0;
      cards.forEach(function(card){
        var ct=normTypeKey(card.getAttribute('data-type')||'');
        var cr=(card.getAttribute('data-creator')||'').toLowerCase();
        var lg=(card.getAttribute('data-language')||'').toLowerCase();
        var st=(card.getAttribute('data-search')||'');
        var ok=true;
        if(t && ct!==t) ok=false;
        if(c && cr!==c) ok=false;
        if(l && lg!==l) ok=false;
        if(s && st.indexOf(s)===-1) ok=false;
        card.style.display=ok?'':'none';
        if(ok) shown++;
      });
      if(countEl) countEl.textContent=String(shown);
    }

    // Sort Logic
    var grid = document.querySelector('.grid');
    if(grid) {
      grid.querySelectorAll('.card').forEach(function(c,i){ c.setAttribute('data-idx', i); });
    }
    function applySort() {
      if(!sortCheckbox || !grid) return;
      var latest = sortCheckbox.checked;
      var cards = Array.from(grid.children);
      cards.sort(function(a,b){
        if(!latest) {
          return Number(a.getAttribute('data-idx')) - Number(b.getAttribute('data-idx'));
        }
        var da = a.getAttribute('data-date') || '';
        var db = b.getAttribute('data-date') || '';
        // Date Descending
        if (!da && !db) return 0;
        if (!da) return 1;
        if (!db) return -1;
        return db.localeCompare(da);
      });
      cards.forEach(function(c){ grid.appendChild(c); });
    }

    function wire(sel, ev){if(!sel) return; sel.addEventListener(ev||'change',applyFilters);} 
    wire(typeSel); wire(creatorSel); wire(langSel); wire(searchInput, 'input');
    if(searchClear) searchClear.addEventListener('click', function(){
      if(searchInput) { searchInput.value = ''; applyFilters(); }
    });
    if(sortCheckbox) sortCheckbox.addEventListener('change', applySort);
    // Initialize count on load
    if(typeSel||creatorSel||langSel||searchInput) applyFilters();
  })();</script>
</body>
</html>`;
}

// CSS moved to tools/sitegen/assets/style.css and referenced via link tag in renderLayout
