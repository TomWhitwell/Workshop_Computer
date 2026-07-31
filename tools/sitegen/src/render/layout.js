import { applyContentSecurityPolicy, CSP_PLACEHOLDER } from './csp.js';

export function renderLayout({ title, content, relativeRoot = '.', legacyRedirectRoot = relativeRoot, repoUrl = 'https://github.com/TomWhitwell/Workshop_Computer', showProgramIdentity = false }) {
  const html = `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <meta http-equiv="Content-Security-Policy" content="${CSP_PLACEHOLDER}" />
  <title>${title ? String(title).replace(/</g, '&lt;') : 'Workshop Computer'}</title>
  <link rel="icon" type="image/png" href="${relativeRoot}/assets/favicon/favicon.png" />
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
        <button class="site-menu-toggle" type="button" aria-expanded="false" aria-controls="site-nav" aria-label="Menu"><span class="site-menu-toggle__icon" aria-hidden="true"><span></span><span></span><span></span></span></button>
        <nav class="site-nav" id="site-nav" aria-label="Music Thing Modular">
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
        <a href="https://www.musicthing.co.uk/workshopsystem/program-cards/install/" target="_blank" rel="noopener noreferrer">Installation <span aria-hidden="true">↗</span><span class="sr-only"> (opens in a new tab)</span></a>
        <a href="${repoUrl}" target="_blank" rel="noopener noreferrer">Make a card <span aria-hidden="true">↗</span><span class="sr-only"> (opens in a new tab)</span></a>
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
  <script src="${relativeRoot}/assets/js/site-menu.js"></script>
  <script type="module" src="${legacyRedirectRoot}/assets/js/legacy-redirects.js"></script>
  <script type="module" src="${relativeRoot}/assets/js/program-cards.js"></script>
  <script src="${relativeRoot}/assets/js/catalogue-filters.js"></script>
</body>
</html>`;
  return applyContentSecurityPolicy(html);
}

// CSS moved to tools/sitegen/assets/style.css and referenced via link tag in renderLayout
