import { Picoboot } from './picoboot.js';
import { uf2ToFlashBuffer } from './uf2.js';

var siteRootUrl = new URL('../../', import.meta.url);
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

function loadInstagramEmbedScript() {
  if (window.instgrm && window.instgrm.Embeds) {
    window.instgrm.Embeds.process();
    return;
  }
  if (document.querySelector('script[data-instagram-embed]')) return;
  var s = document.createElement('script');
  s.src = 'https://www.instagram.com/embed.js';
  s.async = true;
  s.setAttribute('data-instagram-embed', '1');
  document.body.appendChild(s);
}

function demoEmbedIframe(provider, id, kind, autoplay, start) {
  if (provider === 'instagram') {
    var igKind = kind || 'reel';
    var permalink = 'https://www.instagram.com/' + encodeURIComponent(igKind) + '/' + encodeURIComponent(id) + '/';
    return '<blockquote class="instagram-media" data-instgrm-permalink="' + permalink + '" data-instgrm-version="14"><a href="' + permalink + '" target="_blank" rel="noopener noreferrer">View this ' + (igKind === 'reel' ? 'reel' : 'post') + ' on Instagram</a></blockquote>';
  }
  var qs = autoplay ? '?rel=0&autoplay=1' : '?rel=0';
  if (start) qs += '&start=' + encodeURIComponent(start);
  return '<iframe src="https://www.youtube.com/embed/' + encodeURIComponent(id) + qs + '" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" allowfullscreen title="YouTube video"></iframe>';
}

// Play the demo video inline (swap the thumbnail for an autoplay embed) instead
// of navigating away. Falls back to the link when JS is unavailable.
document.addEventListener('click', function(e) {
  var a = e.target.closest('.program-card-demo a[data-video-provider][data-video-id]');
  if (!a) return;
  e.preventDefault();
  var provider = a.getAttribute('data-video-provider') || 'youtube';
  var id = a.getAttribute('data-video-id');
  var kind = a.getAttribute('data-video-kind');
  var start = a.getAttribute('data-video-start');
  var wrap = document.createElement('div');
  wrap.className = provider === 'instagram' ? 'instagram-embed' : 'video-embed';
  wrap.innerHTML = demoEmbedIframe(provider, id, kind, provider === 'youtube', start);
  a.replaceWith(wrap);
  if (provider === 'instagram') loadInstagramEmbedScript();
});

// Play videos shown on the landing page without leaving the card index. The
// rest of each tile remains a link to the program's detail page.
document.addEventListener('click', function(e) {
  var media = e.target.closest('.program-card-tile__media[data-video-provider][data-video-id]');
  if (!media) return;
  e.preventDefault();
  var provider = media.getAttribute('data-video-provider') || 'youtube';
  var id = media.getAttribute('data-video-id');
  var kind = media.getAttribute('data-video-kind');
  var start = media.getAttribute('data-video-start');
  media.classList.add('program-card-tile__media--playing');
  media.removeAttribute('data-video-provider');
  media.removeAttribute('data-video-id');
  media.removeAttribute('data-video-kind');
  media.removeAttribute('data-video-start');
  media.removeAttribute('aria-hidden');
  media.innerHTML = demoEmbedIframe(provider, id, kind, provider === 'youtube', start);
  if (provider === 'instagram') loadInstagramEmbedScript();
});

// README Instagram embeds use Instagram's official embed.js for correct sizing.
if (document.querySelector('.instagram-media')) loadInstagramEmbedScript();

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
    var response = await fetch(new URL('firmware-fingerprints.json', siteRootUrl), { cache: 'no-store' });
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
    window.location.href = new URL('programs/' + encodeURIComponent(card.slug) + '/', siteRootUrl).href;
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
    var uf2Bytes = new Uint8Array(await r.arrayBuffer());
    var expectedHash = (el.dataset.sha256 || '').trim().toLowerCase();
    if (expectedHash) {
      setFirmwareActionLabel(el, 'Checking…');
      var digest = new Uint8Array(await crypto.subtle.digest('SHA-256', uf2Bytes));
      if (bytesToHex(digest) !== expectedHash) throw new Error('Firmware SHA256 mismatch');
    }
    var parsed = uf2ToFlashBuffer(uf2Bytes);
    setFirmwareActionLabel(el, 'Flashing…');
    await pb.flashEraseAndWrite(parsed.address, parsed.data, function(done, total) {
      setFirmwareActionLabel(el, 'Flashing… ' + Math.round(100 * done / total) + '%');
    });
    setFirmwareActionLabel(el, 'Verifying…');
    var readback = await pb.flashRead(parsed.address, parsed.data.length, function(done, total) {
      setFirmwareActionLabel(el, 'Verifying… ' + Math.round(100 * done / total) + '%');
    });
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
    console.error('Firmware programming failed:', e);
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
