// Search + filter bar shared by the index and the all-cards archive.
//
// Both pages get the same controls (search, advanced panel, connect button);
// catalogue-filters.js wires them by id and adapts to whichever collection the
// page renders - curated shelves on the index, one-line rows on the archive.

// linkId marks the link as the index's all-cards toggle: catalogue-filters.js
// intercepts it to reveal the full list in place. Without JS it stays an
// ordinary link to the archive page.
export function renderFilterBar({ creatorOptions, sortOptions, tagOptions, linkHref, linkText, linkId = '', label = 'Filter programs' }) {
  return `<section class="filter-bar" aria-label="${label}">
    <div class="search-bar-row">
      <div class="search-wrapper">
        <div class="search-control">
          <svg class="search-icon" viewBox="0 0 24 24" aria-hidden="true"><circle cx="10.5" cy="10.5" r="6.5"></circle><path d="m15.5 15.5 5 5"></path></svg>
          <input type="text" id="filter-search" placeholder="Search by name, creator, function or tag…" class="search-input" aria-label="Search cards" autofocus>
          <button id="search-clear" class="search-clear" aria-label="Clear search" type="button">✕</button>
        </div>
      </div>
    </div>
    <div class="search-tools-row">
      <details class="advanced-options">
        <summary>Advanced search</summary>
        <div class="filter-row">
          <div class="filter-group">
            <label for="filter-creator">Creator</label>
            <select id="filter-creator">${creatorOptions}</select>
          </div>
          <div class="filter-group">
            <label for="sort-mode">Sort</label>
            <select id="sort-mode">${sortOptions}</select>
          </div>
          <div class="filter-group">
            <label for="filter-flash-16mb">Flash</label>
            <label class="tag-filter-option program-card-tag" for="filter-flash-16mb"><input id="filter-flash-16mb" type="checkbox" name="filter-flash" value="16mb"> <span>16MB firmware</span></label>
          </div>
          <fieldset class="filter-group tag-filter-group">
            <legend class="tag-filter-heading"><span>Tags</span><span class="tag-filter-heading__actions"><button id="clear-tags" type="button" hidden>Clear</button></span></legend>
            <input id="filter-tag-search" class="tag-filter-search" type="search" placeholder="Search all tags…" aria-label="Search all tags" autocomplete="off">
            <div id="filter-tags" class="tag-filter-options">${tagOptions}<button id="toggle-all-tags" class="tag-filter-toggle-all" type="button" aria-pressed="false">Show all…</button></div>
          </fieldset>
        </div>
      </details>
      <a class="filter-link"${linkId ? ` id="${linkId}"` : ''} href="${linkHref}">${linkText}</a>
      <button id="connectToggle" class="connect-toggle connect-toggle--search" type="button" role="switch" aria-checked="false" aria-label="Connect to RP2040 via WebUSB" title="Reboot computer into programming mode before connecting">
        <span class="c-status" aria-hidden="true"></span><span class="c-label">Connect workshop computer</span>
      </button>
    </div>
</section>`;
}
