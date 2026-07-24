export function renderLayout({ title, content, relativeRoot = '.', repoUrl = 'https://github.com/TomWhitwell/Workshop_Computer', showProgramIdentity = false, programCardCount = null }) {
  return `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>${title ? String(title).replace(/</g, '&lt;') : 'Workshop Computer'}</title>
  <link rel="stylesheet" href="${relativeRoot}/assets/github-markdown.css" />
  <link rel="stylesheet" href="${relativeRoot}/assets/style.css" />
  <link rel="stylesheet" href="${relativeRoot}/assets/program-cards.css" />
</head>
<body>
  <header class="site-header" id="page-top">
    <div class="container header-bar">
      <a class="site-wordmark" href="https://www.musicthing.co.uk/" aria-label="Music Thing Modular">
        <img src="https://www.musicthing.co.uk/images/MTM_Horiz.svg" alt="Music Thing Modular">
      </a>
      <div class="site-header-actions">
        <nav class="site-nav" aria-label="Music Thing Modular">
          <a href="https://www.musicthing.co.uk/#writing">Talking &amp; Writing</a>
          <a href="https://www.musicthing.co.uk/about/">About</a>
          <a href="https://www.musicthing.co.uk/buy">Buy</a>
        </nav>
      </div>
    </div>
  </header>
  <main class="container">
    ${showProgramIdentity ? `<header class="program-cards__title program-cards__title--site">
      <a class="program-cards__identity" href="${relativeRoot}/index.html">
        <img class="program-cards__identity-mark" src="${relativeRoot}/assets/program_cards/ProgramCardGreen.svg" alt="">
        <span class="program-cards__identity-name">Program Cards</span>
      </a>
      <nav class="program-cards__links" aria-label="Program card links">
        <a href="${relativeRoot}/archive/">All cards${Number.isFinite(Number(programCardCount)) ? ` (${Number(programCardCount)})` : ''}</a>
        <a href="https://www.musicthing.co.uk/workshopsystem/program-cards/install/">Installation</a>
        <a href="${repoUrl}">Make a card</a>
      </nav>
    </header>` : ''}
    ${content}
  </main>
  <footer class="site-footer">
    <div class="container">
      <h2>Music Thing Modular</h2>
      <div class="footer-grid">
        <p>Tom Whitwell<br><a href="mailto:tom@musicthing.co.uk">tom@musicthing.co.uk</a><br><a href="https://www.musicthing.co.uk/about/">About Music Thing Modular</a></p>
        <p><a href="${repoUrl}">GitHub</a><br><a href="https://www.instagram.com/musicthingmodular/">Instagram</a><br><a href="https://workshopsystem.substack.com/">Newsletter</a></p>
        <p>Open source electronic musical instruments. Designed in London, made in Brighton, built and used by musicians around the world.</p>
      </div>
    </div>
  </footer>
  <script type="module">
import { Picoboot } from '${relativeRoot}/assets/js/picoboot.js';
import { uf2ToFlashBuffer } from '${relativeRoot}/assets/js/uf2.js';

var connectBtn = document.getElementById('connectToggle');
var pb = null;
var RECONNECT_KEY = 'workshop-computer-picoboot-reconnect';

// Physical blank cards ship in five colors. Give the catalogue's blank card a
// fresh color on each page load while keeping the future label overlay intact.
var blankCardColors = ['#6c2a83', '#b08a2e', '#b52c30', '#173f82', '#27743a'];
document.querySelectorAll('[data-random-blank-card]').forEach(function(card) {
  var index = Math.floor(Math.random() * blankCardColors.length);
  card.style.color = blankCardColors[index];
});

// Reveal a firmware's SHA256 beneath the tiles when its download is clicked.
document.addEventListener('click', function(e) {
  var a = e.target.closest('a.program-card-action--download[data-sha256]');
  if (!a) return;
  var main = a.closest('.program-card-hero__main');
  var box = main && main.querySelector('[data-sha-display]');
  if (box) {
    var v = box.querySelector('[data-sha-value]');
    if (v) v.textContent = a.getAttribute('data-sha256');
    box.hidden = false;
  }
});

// "How to verify" modal (native <dialog>): open, close button, backdrop click.
document.addEventListener('click', function(e) {
  var open = e.target.closest('[data-verify-open]');
  if (open) {
    var root = open.closest('.program-cards');
    var m = root && root.querySelector('[data-verify-modal]');
    if (m && m.showModal) m.showModal();
    return;
  }
  if (e.target.closest('[data-verify-close]')) {
    var d = e.target.closest('dialog');
    if (d) d.close();
    return;
  }
  if (e.target.matches('.verify-modal')) e.target.close(); // click outside the body
});

// Play the demo video inline (swap the thumbnail for an autoplay embed) instead
// of navigating to YouTube. Falls back to the link when JS is unavailable.
document.addEventListener('click', function(e) {
  var a = e.target.closest('.program-card-demo a[data-youtube-id]');
  if (!a) return;
  e.preventDefault();
  var id = a.getAttribute('data-youtube-id');
  var wrap = document.createElement('div');
  wrap.className = 'video-embed';
  wrap.innerHTML = '<iframe src="https://www.youtube.com/embed/' + encodeURIComponent(id) + '?rel=0&autoplay=1" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" allowfullscreen title="YouTube video"></iframe>';
  a.replaceWith(wrap);
});

// Play videos shown on the landing page without leaving the card index. The
// rest of each tile remains a link to the program's detail page.
document.addEventListener('click', function(e) {
  var media = e.target.closest('.program-card-tile__media[data-youtube-id]');
  if (!media) return;
  e.preventDefault();
  var id = media.getAttribute('data-youtube-id');
  media.classList.add('program-card-tile__media--playing');
  media.removeAttribute('data-youtube-id');
  media.removeAttribute('aria-hidden');
  media.innerHTML = '<iframe src="https://www.youtube.com/embed/' + encodeURIComponent(id) + '?rel=0&autoplay=1" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" allowfullscreen title="YouTube video"></iframe>';
});

document.addEventListener('click', function(e) {
  var button = e.target.closest('[data-panel-position-button]');
  if (!button) return;
  var root = button.closest('[data-panel-views]');
  if (!root) return;
  var selected = button.getAttribute('data-panel-position-button');
  root.querySelectorAll('[data-panel-position-button]').forEach(function(candidate) {
    var active = candidate.getAttribute('data-panel-position-button') === selected;
    candidate.setAttribute('aria-pressed', String(active));
    if (candidate.getAttribute('role') === 'tab') candidate.setAttribute('aria-selected', String(active));
  });
  root.querySelectorAll('[data-panel-position-view]').forEach(function(view) {
    var active = view.getAttribute('data-panel-position-view') === selected;
    view.hidden = !active;
    view.setAttribute('aria-hidden', String(!active));
  });
  root.querySelectorAll('[data-panel-position-panel]').forEach(function(panel) {
    var active = panel.getAttribute('data-panel-position-panel') === selected;
    panel.hidden = !active;
    panel.setAttribute('aria-hidden', String(!active));
  });
  if (button.classList.contains('program-card-panel-choice') && window.history && window.URL) {
    var url = new URL(window.location.href);
    url.searchParams.set('panel', selected);
    window.history.replaceState(null, '', url.pathname + url.search + url.hash);
  }
});

document.addEventListener('keydown', function(e) {
  var button = e.target.closest('.program-card-panel-choice');
  if (!button || !['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(e.key)) return;
  var buttons = Array.from(button.parentElement.querySelectorAll('.program-card-panel-choice'));
  var index = buttons.indexOf(button);
  if (e.key === 'Home') index = 0;
  else if (e.key === 'End') index = buttons.length - 1;
  else index = (index + (e.key === 'ArrowRight' ? 1 : -1) + buttons.length) % buttons.length;
  e.preventDefault();
  buttons[index].focus();
  buttons[index].click();
});

document.addEventListener('DOMContentLoaded', function() {
  if (!window.URLSearchParams) return;
  var requested = new URLSearchParams(window.location.search).get('panel');
  if (!requested) return;
  document.querySelectorAll('.program-card-panel-choice').forEach(function(button) {
    if (button.getAttribute('data-panel-position-button') === requested) button.click();
  });
});

// On wide layouts, keep the panel visualization visible for the entire card:
// card-title aligned at the top, viewport-centered through the middle, and
// bottom-aligned with the Back to all programs action at the end.
var panelRailFrame = 0;
function updatePanelRails() {
  panelRailFrame = 0;
  document.querySelectorAll('.program-card-panel-rail').forEach(function(rail) {
    var article = rail.closest('.program-card-page');
    var title = article && article.querySelector('.program-card-hero h1');
    if (!article || !title || window.innerWidth <= 1450) {
      rail.classList.remove('is-viewport-tracked');
      rail.style.removeProperty('--program-card-panel-left');
      rail.style.removeProperty('--program-card-panel-top');
      return;
    }

    var actions = article.nextElementSibling && article.nextElementSibling.classList.contains('actions-duo')
      ? article.nextElementSibling
      : null;
    var articleRect = article.getBoundingClientRect();
    var titleTop = title.getBoundingClientRect().top + window.scrollY;
    var endBottom = (actions || article).getBoundingClientRect().bottom + window.scrollY;
    var panelHeight = rail.getBoundingClientRect().height;
    var centeredTop = Math.max(18, (window.innerHeight - panelHeight) / 2);
    var startingTop = titleTop + window.scrollY;
    var endingTop = endBottom - panelHeight - window.scrollY;
    var top = Math.min(endingTop, centeredTop, startingTop);

    rail.style.setProperty('--program-card-panel-left', (articleRect.left - 306) + 'px');
    rail.style.setProperty('--program-card-panel-top', Math.max(0, top) + 'px');
    rail.classList.add('is-viewport-tracked');
  });
}

function schedulePanelRails() {
  if (!panelRailFrame) panelRailFrame = requestAnimationFrame(updatePanelRails);
}

window.addEventListener('scroll', schedulePanelRails, { passive: true });
window.addEventListener('resize', schedulePanelRails);
schedulePanelRails();

function setConnected(on) {
  if (connectBtn) {
    connectBtn.setAttribute('aria-checked', String(on));
    connectBtn.classList.toggle('active', on);
    var label = connectBtn.querySelector('.c-label');
    if (label) label.textContent = on ? 'Connected' : 'Connect workshop computer';
  }
  document.querySelectorAll('a[data-uf2-url]').forEach(function(a) {
    var actionLabel = a.querySelector('.program-card-action__label') || a.querySelector('span');
    if (on) {
      if (!a.dataset.origHtml) a.dataset.origHtml = a.innerHTML;
      a.classList.add('program-card-action--program');
      if (actionLabel) actionLabel.textContent = 'Program';
    } else {
      a.classList.remove('program-card-action--program');
      if (a.dataset.origHtml) { a.innerHTML = a.dataset.origHtml; delete a.dataset.origHtml; }
    }
  });
}

function setFirmwareActionLabel(action, text) {
  var label = action.querySelector('.program-card-action__label') || action.querySelector('span');
  if (label) label.textContent = text;
  else action.textContent = text;
}

function bytesToHex(bytes) {
  return Array.from(bytes, function(byte) { return byte.toString(16).padStart(2, '0'); }).join('');
}

async function disconnectPicoboot() {
  var connected = pb;
  pb = null;
  if (connected) await connected.disconnect();
  setConnected(false);
}

async function identifyIndexCard() {
  if (!pb || !connectBtn || !document.querySelector('.program-cards--index')) return;
  var label = connectBtn.querySelector('.c-label');
  connectBtn.disabled = true;
  if (label) label.textContent = 'Identifying card…';
  try {
    var response = await fetch('${relativeRoot}/firmware-fingerprints.json', { cache: 'no-store' });
    if (!response.ok) throw new Error('Fingerprint catalogue HTTP ' + response.status);
    var catalogue = await response.json();
    var flash = await pb.flashRead(Number(catalogue.address), Number(catalogue.length));
    var digest = new Uint8Array(await crypto.subtle.digest('SHA-256', flash));
    var hash = bytesToHex(digest);
    var match = (catalogue.entries || []).find(function(entry) { return entry.hash === hash; });
    if (!match || !Array.isArray(match.cards) || match.cards.length !== 1) {
      var message = match ? 'Card match is ambiguous — reconnect' : 'Card not recognized — reconnect';
      await disconnectPicoboot();
      if (label) label.textContent = message;
      connectBtn.disabled = false;
      setTimeout(function() {
        if (label && !pb && label.textContent === message) label.textContent = 'Connect workshop computer';
      }, 3000);
      return;
    }
    var card = match.cards[0];
    if (label) label.textContent = 'Opening ' + (card.title || 'card') + '…';
    try {
      sessionStorage.setItem(RECONNECT_KEY, JSON.stringify({
        serialNumber: pb.device && pb.device.serialNumber || '',
        expires: Date.now() + 15000,
      }));
    } catch (_) {}
    window.location.href = '${relativeRoot}/programs/' + encodeURIComponent(card.slug) + '/';
  } catch (error) {
    console.error('Could not identify connected card:', error);
    await disconnectPicoboot();
    var failureMessage = 'Identification failed — reconnect';
    if (label) label.textContent = failureMessage;
    connectBtn.disabled = false;
    setTimeout(function() {
      if (label && !pb && label.textContent === failureMessage) label.textContent = 'Connect workshop computer';
    }, 3000);
  }
}

async function reconnectAuthorizedPicoboot() {
  if (!connectBtn || !('usb' in navigator)) return;
  var pending = null;
  try {
    pending = JSON.parse(sessionStorage.getItem(RECONNECT_KEY) || 'null');
    sessionStorage.removeItem(RECONNECT_KEY);
  } catch (_) {}
  if (!pending || Number(pending.expires) < Date.now()) return;
  var label = connectBtn.querySelector('.c-label');
  connectBtn.disabled = true;
  if (label) label.textContent = 'Reconnecting…';
  for (var attempt = 0; attempt < 15; attempt++) {
    try {
      var devices = await Picoboot.getDevices();
      var candidate = devices.find(function(device) {
        return !pending.serialNumber || device.device.serialNumber === pending.serialNumber;
      });
      if (candidate) {
        await candidate.connect();
        pb = candidate;
        connectBtn.disabled = false;
        setConnected(true);
        return;
      }
    } catch (error) {
      console.warn('Picoboot reconnect attempt failed:', error);
    }
    await new Promise(function(resolve) { setTimeout(resolve, 150); });
  }
  connectBtn.disabled = false;
  setConnected(false);
}

async function flash(url, el) {
  if (el.dataset.busy) return;
  el.dataset.busy = '1';
  try {
    setFirmwareActionLabel(el, 'Fetching…');
    var r = await fetch(url);
    if (!r.ok) throw new Error('HTTP ' + r.status);
    var parsed = uf2ToFlashBuffer(new Uint8Array(await r.arrayBuffer()));
    setFirmwareActionLabel(el, 'Flashing…');
    await pb.flashEraseAndWrite(parsed.address, parsed.data);
    setFirmwareActionLabel(el, 'Verifying…');
    var readback = await pb.flashRead(parsed.address, parsed.data.length);
    for (var i = 0; i < parsed.data.length; i++) {
      if (readback[i] !== parsed.data[i]) throw new Error('Verify failed at byte ' + i);
    }
    setFirmwareActionLabel(el, 'Starting card…');
    var rebooted = false;
    try {
      var connection = pb && pb.getConnection();
      if (!connection) throw new Error('Picoboot connection was lost before reboot');
      await connection.reboot(500);
      rebooted = true;
    } catch (rebootError) {
      // Some browsers report the expected USB disappearance as an error even
      // after the reboot command was accepted. Preserve a manual-reset fallback.
      console.warn('Automatic reboot after programming was not confirmed:', rebootError);
    }
    pb = null;
    setConnected(false);
    setFirmwareActionLabel(el, rebooted ? 'Card started' : 'Flashed; reset to use');
    setTimeout(function() {
      delete el.dataset.busy;
      setFirmwareActionLabel(el, 'Download');
    }, 3000);
  } catch(e) {
    setFirmwareActionLabel(el, 'Error');
    delete el.dataset.busy;
    setTimeout(function() { setFirmwareActionLabel(el, 'Program'); }, 3000);
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
        await identifyIndexCard();
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
    var a = e.target.closest('a[data-uf2-url]');
    if (!a) return;
    e.preventDefault();
    await flash(a.dataset.uf2Url, a);
  });
  reconnectAuthorizedPicoboot();
}
  </script>
  <script>(function(){
    // Search + filters. Index toggles curated shelves <-> flat results; the
    // archive page filters its one-line rows in place. Both support type-ahead
    // search and latest-update sort.
    var creatorSel=document.getElementById('filter-creator');
    var tagInputs=Array.from(document.querySelectorAll('input[name="filter-tag"]'));
    var tagSearch=document.getElementById('filter-tag-search');
    var tagGroup=document.querySelector('.tag-filter-group');
    var toggleAllTags=document.getElementById('toggle-all-tags');
    var clearTags=document.getElementById('clear-tags');
    var searchInput=document.getElementById('filter-search');
    var searchClear=document.getElementById('search-clear');
    var sortSel = document.getElementById('sort-mode');
    var countEl=document.getElementById('cards-count');
    var discoveryEl=document.getElementById('discovery');
    var resultsEl=document.getElementById('search-results');
    var resultsGrid=resultsEl?resultsEl.querySelector('.program-card-grid'):null;
    var noResults=document.getElementById('no-results');
    var archiveList=document.querySelector('.program-card-archive-list');
    // The collection to sort/filter directly in place (archive) vs behind a toggle (index).
    var sortContainer=resultsGrid||archiveList;
    var itemSelector=resultsGrid?'.program-card-tile':'.program-card-archive-row';

    function filterCollection(items, c, selectedTags, s){
      var shown=0;
      items.forEach(function(el){
        var cr=(el.getAttribute('data-creator')||'').toLowerCase();
        var tags=(el.getAttribute('data-tags')||'').toLowerCase().split(/\\s+/);
        var st=(el.getAttribute('data-search')||'');
        var ok=true;
        if(c && cr!==c) ok=false;
        if(selectedTags.length && !selectedTags.some(function(tag){ return tags.indexOf(tag) !== -1; })) ok=false;
        if(s && st.indexOf(s)===-1) ok=false;
        el.style.display=ok?'':'none';
        if(ok) shown++;
      });
      if(countEl) countEl.textContent = shown?('('+shown+')'):'';
      if(noResults) noResults.hidden = shown>0;
    }

    function applyFilters(){
      var c=creatorSel&&creatorSel.value?creatorSel.value.toLowerCase():'';
      var selectedTags=tagInputs.filter(function(input){ return input.checked; }).map(function(input){ return input.value.toLowerCase(); });
      var s=searchInput&&searchInput.value?searchInput.value.trim().toLowerCase():'';

      var active = !!(c||selectedTags.length||s);

      if(searchClear) searchClear.style.display = active ? 'flex' : 'none';
      if(clearTags) clearTags.hidden = selectedTags.length === 0;

      if(tagInputs.length && window.history && window.URL) {
        var url = new URL(window.location.href);
        url.searchParams.delete('tag');
        selectedTags.forEach(function(tag){ url.searchParams.append('tag', tag); });
        window.history.replaceState(null, '', url.pathname + url.search + url.hash);
      }

      if(resultsEl){
        // Index: reveal flat results only while filtering
        if(discoveryEl) discoveryEl.hidden = active;
        resultsEl.hidden = !active;
        if(!active) return;
        filterCollection(resultsGrid?resultsGrid.querySelectorAll('.program-card-tile'):[], c, selectedTags, s);
      } else if(archiveList){
        // Archive: filter rows in place
        filterCollection(archiveList.querySelectorAll('.program-card-archive-row'), c, selectedTags, s);
      }

    }

    function applyTagOptionSearch(){
      var query=tagSearch&&tagSearch.value?tagSearch.value.trim().toLowerCase():'';
      var showAll=tagGroup&&tagGroup.classList.contains('is-showing-all');
      document.querySelectorAll('[data-tag-option]').forEach(function(option){
        var input=option.querySelector('input[name="filter-tag"]');
        var curated=option.getAttribute('data-tag-source')==='curated';
        var matches=!query||(option.getAttribute('data-tag-label')||'').indexOf(query)!==-1;
        option.hidden=query?!matches:!(showAll||curated||(input&&input.checked));
      });
    }

    if(sortContainer) {
      sortContainer.querySelectorAll(itemSelector).forEach(function(c,i){ c.setAttribute('data-idx', i); });
    }
    function applySort() {
      if(!sortSel || !sortContainer) return;
      var mode = sortSel.value;
      var items = Array.from(sortContainer.children);
      function idx(el){ return Number(el.getAttribute('data-idx'))||0; }
      function dv(el){ var d=el.getAttribute('data-date')||''; return d==='n/a'?'':d; }
      function nv(el){ return Number(el.getAttribute('data-num'))||0; }
      function nm(el){ return el.getAttribute('data-name')||''; }
      items.sort(function(a,b){
        var da, db;
        switch(mode){
          case 'updated-desc': da=dv(a); db=dv(b); if(!da&&!db) return idx(a)-idx(b); if(!da) return 1; if(!db) return -1; return db.localeCompare(da);
          case 'updated-asc': da=dv(a); db=dv(b); if(!da&&!db) return idx(a)-idx(b); if(!da) return 1; if(!db) return -1; return da.localeCompare(db);
          case 'name-asc': return nm(a).localeCompare(nm(b));
          case 'name-desc': return nm(b).localeCompare(nm(a));
          case 'number-asc': return nv(a)-nv(b);
          case 'number-desc': return nv(b)-nv(a);
          default: return idx(a)-idx(b);
        }
      });
      items.forEach(function(c){ sortContainer.appendChild(c); });
    }

    function wire(sel, ev){if(!sel) return; sel.addEventListener(ev||'change',applyFilters);}
    wire(creatorSel); tagInputs.forEach(function(input){
      wire(input);
      input.addEventListener('change', applyTagOptionSearch);
    });
    if(tagSearch) tagSearch.addEventListener('input', applyTagOptionSearch);
    if(toggleAllTags) toggleAllTags.addEventListener('click', function(){
      var showing=tagGroup.classList.toggle('is-showing-all');
      toggleAllTags.setAttribute('aria-pressed', String(showing));
      applyTagOptionSearch();
    });
    if(clearTags) clearTags.addEventListener('click', function(){
      tagInputs.forEach(function(input){ input.checked = false; });
      applyFilters();
      applyTagOptionSearch();
    });
    wire(searchInput, 'input');
    if(searchInput) searchInput.addEventListener('search', applyFilters);
    if(searchClear) searchClear.addEventListener('click', function(){
      // Full reset back to the default curated view: clear search text and every filter.
      if(searchInput) searchInput.value = '';
      if(creatorSel) creatorSel.value = '';
      tagInputs.forEach(function(input){ input.checked = false; });
      if(tagSearch) tagSearch.value = '';
      if(tagGroup) tagGroup.classList.remove('is-showing-all');
      if(toggleAllTags) toggleAllTags.setAttribute('aria-pressed', 'false');
      applyFilters();
      applyTagOptionSearch();
      if(searchInput) searchInput.focus();
    });
    if(sortSel) sortSel.addEventListener('change', applySort);
    if(tagInputs.length && window.URLSearchParams) {
      var requestedTags = new URLSearchParams(window.location.search).getAll('tag').map(function(tag){ return tag.toLowerCase(); });
      tagInputs.forEach(function(input){ input.checked = requestedTags.indexOf(input.value.toLowerCase()) !== -1; });
      if(requestedTags.length) {
        var advanced = document.querySelector('.advanced-options');
        if(advanced) advanced.open = true;
      }
    }
    applyTagOptionSearch();
    if(creatorSel||tagInputs.length||searchInput) applyFilters();
  })();</script>
</body>
</html>`;
}

// CSS moved to tools/sitegen/assets/style.css and referenced via link tag in renderLayout
