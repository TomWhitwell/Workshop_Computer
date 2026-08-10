(function(){
  // Search + filters. Index toggles curated shelves <-> flat results; the
  // archive page filters its one-line rows in place. Both support type-ahead
  // search and creation-date sort.
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
  var resultsList=resultsEl?resultsEl.querySelector('.program-card-archive-list, .program-card-grid'):null;
  var noResults=document.getElementById('no-results');
  var archiveList=document.querySelector('.program-card-archive-list');
  // The collection to sort/filter directly in place (archive) vs behind a toggle (index).
  var sortContainer=resultsList||archiveList;
  var itemSelector=sortContainer&&sortContainer.classList.contains('program-card-grid')?'.program-card-tile':'.program-card-archive-row';

  // Cards still visible after the last filter pass, so Enter in the search box
  // can jump straight to the card when filtering leaves exactly one.
  var visibleItems=[];
  // Set by Enter on an empty search box: show the full list, not the shelves.
  var showAll=false;
  // Whether the full, unfiltered list is on screen right now.
  var listingAll=false;
  var allCardsLink=document.getElementById('all-cards-toggle');
  var allCardsLabel=allCardsLink?allCardsLink.textContent:'';

  // "#12" searches the card number alone, rather than the whole text blob.
  function cardNumberQuery(s){
    var m=/^#\s*(\d+)$/.exec(s);
    return m?parseInt(m[1], 10):null;
  }

  function filterCollection(items, c, selectedTags, s){
    var shown=0;
    var wantedNum=cardNumberQuery(s);
    visibleItems=[];
    items.forEach(function(el){
      var cr=(el.getAttribute('data-creator')||'').toLowerCase();
      var tags=(el.getAttribute('data-tags')||'').toLowerCase().split(/\s+/);
      var st=(el.getAttribute('data-search')||'');
      var ok=true;
      if(c && cr!==c) ok=false;
      if(selectedTags.length && !selectedTags.some(function(tag){ return tags.indexOf(tag) !== -1; })) ok=false;
      if(wantedNum!==null){ if(parseInt(el.getAttribute('data-num'), 10)!==wantedNum) ok=false; }
      else if(s && st.indexOf(s)===-1) ok=false;
      el.style.display=ok?'':'none';
      if(ok){ shown++; visibleItems.push(el); }
    });
    if(countEl) countEl.textContent = shown===items.length?('All '+shown+' cards'):(shown===1?'1 result':(shown+' results'));
    if(noResults) noResults.hidden = shown>0;
  }

  function applyFilters(){
    var c=creatorSel&&creatorSel.value?creatorSel.value.toLowerCase():'';
    var selectedTags=tagInputs.filter(function(input){ return input.checked; }).map(function(input){ return input.value.toLowerCase(); });
    var raw=searchInput&&searchInput.value?searchInput.value:'';
    var s=raw.trim().toLowerCase();

    // A whitespace-only search - or Enter on an empty box - still counts as a
    // search: it lists every card.
    var active = !!(c||selectedTags.length||raw||showAll);
    // The unfiltered listing, however it was reached (toggle link, Enter or
    // whitespace in the search box).
    listingAll = active && !c && !selectedTags.length && !s;

    if(allCardsLink) allCardsLink.textContent = listingAll ? 'Close card list' : allCardsLabel;
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
      if(!active){ visibleItems=[]; return; }
      filterCollection(resultsList?resultsList.querySelectorAll(itemSelector):[], c, selectedTags, s);
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
      var flair=option.getAttribute('data-tag-source')==='flair';
      var matches=!query||(option.getAttribute('data-tag-label')||'').indexOf(query)!==-1;
      option.hidden=query?!matches:!(showAll||flair||(input&&input.checked));
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
        case 'created-desc': da=dv(a); db=dv(b); if(!da&&!db) return idx(a)-idx(b); if(!da) return 1; if(!db) return -1; return db.localeCompare(da);
        case 'created-asc': da=dv(a); db=dv(b); if(!da&&!db) return idx(a)-idx(b); if(!da) return 1; if(!db) return -1; return da.localeCompare(db);
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
  // The label carries the state, so the button needs no pressed styling.
  function setTagToggleLabel(showing){
    if(!toggleAllTags) return;
    toggleAllTags.setAttribute('aria-pressed', String(showing));
    toggleAllTags.textContent = showing ? 'Show fewer…' : 'Show all…';
  }
  if(toggleAllTags) toggleAllTags.addEventListener('click', function(){
    setTagToggleLabel(tagGroup.classList.toggle('is-showing-all'));
    applyTagOptionSearch();
  });
  if(clearTags) clearTags.addEventListener('click', function(){
    tagInputs.forEach(function(input){ input.checked = false; });
    showAll = false;
    applyFilters();
    applyTagOptionSearch();
  });
  if(searchInput) searchInput.addEventListener('input', function(){ showAll = false; applyFilters(); });
  if(searchInput) searchInput.addEventListener('search', applyFilters);
  if(searchInput) searchInput.addEventListener('keydown', function(e){
    // Backspace/Delete/Escape on an already-empty box drops the "show all"
    // listing and returns to the curated shelves.
    if(e.key === 'Backspace' || e.key === 'Delete' || e.key === 'Escape'){
      if(searchInput.value.trim()) return;
      if(!showAll && !searchInput.value) return;
      e.preventDefault();
      searchInput.value = '';
      showAll = false;
      applyFilters();
      return;
    }
    if(e.key !== 'Enter') return;
    // Enter on an empty box lists every card, like a whitespace-only search.
    if(!searchInput.value.trim()){ e.preventDefault(); showAll = true; applyFilters(); return; }
    // Enter on a search that narrowed down to a single card opens that card.
    if(visibleItems.length !== 1) return;
    var card = visibleItems[0];
    var link = card.querySelector('.program-card-tile__link, .program-card-archive-row__link') || card.querySelector('a[href]');
    if(!link) return;
    e.preventDefault();
    window.location.href = link.href;
  });
  // Full reset back to the default curated view: clear search text and every filter.
  function resetAll(){
    showAll = false;
    if(searchInput) searchInput.value = '';
    if(creatorSel) creatorSel.value = '';
    tagInputs.forEach(function(input){ input.checked = false; });
    if(tagSearch) tagSearch.value = '';
    if(tagGroup) tagGroup.classList.remove('is-showing-all');
    setTagToggleLabel(false);
    applyFilters();
    applyTagOptionSearch();
  }
  if(searchClear) searchClear.addEventListener('click', function(){
    resetAll();
    if(searchInput) searchInput.focus();
  });
  // On the index the link opens the full list in place instead of navigating to
  // the archive page; elsewhere (and without JS) it stays a plain link.
  if(allCardsLink && resultsEl) allCardsLink.addEventListener('click', function(e){
    e.preventDefault();
    var wasListing = listingAll;
    resetAll();
    if(wasListing) return;
    showAll = true;
    applyFilters();
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
})();
