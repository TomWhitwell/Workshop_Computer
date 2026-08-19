// Tests for the pure utility helpers shared by build and browser preview.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { arrayOrEmpty, normalizeYamlKey, slugify } from '../src/utils/strings.js';
import { normalizeTags, normalizeDraft, normalizeContact, resolveAudioSample } from '../src/discover/infoFields.js';
import { extractIframeSrc, classifyAudioUrl, resolveAudioSamples } from '../src/utils/audio.js';
import { parseInstagram, instagramEmbedHtml } from '../src/utils/instagram.js';
import { classifyDemoVideo, videoEmbedHtml } from '../src/utils/video.js';
import { parseYoutubeId, parseYoutubeStartSeconds, youtubeEmbedHtml } from '../src/utils/youtube.js';
import {
  inferFlashFromFilename, normalizeFlash, resolveDownloadFlash, assignDownloadFlash,
  flashAttrFromDownloads, deriveMemoryFromDownloads,
} from '../src/utils/flash.js';

test('normalizeYamlKey strips spaces and hyphens, lowercases', () => {
  assert.equal(normalizeYamlKey('demo-link'), 'demolink');
  assert.equal(normalizeYamlKey('Short Description'), 'shortdescription');
});

test('slugify normalizes names for URLs', () => {
  assert.equal(slugify("Chord Blimey!"), 'chord-blimey');
});

test('arrayOrEmpty rejects legacy object-shaped list fields', () => {
  const list = [{ id: 'AudioIn1' }];
  assert.equal(arrayOrEmpty(list), list);
  assert.deepEqual(arrayOrEmpty({ audio_in_1: 'Input' }), []);
  assert.deepEqual(arrayOrEmpty(null), []);
});

test('normalizeTags dedupes and slugs from list or delimited string', () => {
  assert.deepEqual(normalizeTags(['MIDI Host', 'midi-host', ' Utility ']), ['midi-host', 'utility']);
  assert.deepEqual(normalizeTags('midi; utility, midi'), ['midi', 'utility']);
  assert.deepEqual(normalizeTags(undefined), []);
});

test('normalizeDraft handles booleans, strings, and defaults false', () => {
  assert.equal(normalizeDraft(true), true);
  assert.equal(normalizeDraft('Yes'), true);
  assert.equal(normalizeDraft('false'), false);
  assert.equal(normalizeDraft(undefined), false);
});

test('normalizeContact drops empty blocks and normalizes platform keys', () => {
  assert.equal(normalizeContact({}), null);
  assert.equal(normalizeContact('nope'), null);
  const c = normalizeContact({ email: ' a@b.co ', social: { 'Blue Sky': 'https://bsky.app/x' } });
  assert.deepEqual(c, { email: 'a@b.co', social: { bluesky: 'https://bsky.app/x' } });
});

test('resolveAudioSample passes URLs through and resolves repo paths', () => {
  const makeRawUrl = rel => `https://raw.test/${rel}`;
  assert.equal(resolveAudioSample('https://x.com/a.mp3', 'releases/05_x', makeRawUrl), 'https://x.com/a.mp3');
  assert.equal(resolveAudioSample('demo.mp3', 'releases/05_x', makeRawUrl), 'https://raw.test/releases/05_x/demo.mp3');
  assert.equal(resolveAudioSample('', 'releases/05_x', makeRawUrl), '');
});

test('extractIframeSrc pulls src and height from pasted embed code', () => {
  const html = '<iframe style="border: 0; width: 100%; height: 120px;" src="https://bandcamp.com/EmbeddedPlayer/track=1" seamless>';
  assert.deepEqual(extractIframeSrc(html), { src: 'https://bandcamp.com/EmbeddedPlayer/track=1', height: 120 });
  assert.equal(extractIframeSrc('<p>no iframe</p>'), null);
});

test('classifyAudioUrl distinguishes soundcloud, bandcamp embed, file, link', () => {
  assert.equal(classifyAudioUrl('https://soundcloud.com/artist/track').kind, 'soundcloud');
  assert.equal(classifyAudioUrl('https://w.soundcloud.com/player/?url=x').embedUrl, 'https://w.soundcloud.com/player/?url=x');
  assert.equal(classifyAudioUrl('https://artist.bandcamp.com/EmbeddedPlayer/track=1').kind, 'bandcamp');
  assert.equal(classifyAudioUrl('https://artist.bandcamp.com/track/song').kind, 'link');
  assert.equal(classifyAudioUrl('https://x.com/demo.mp3?v=2').kind, 'file');
  assert.equal(classifyAudioUrl('https://example.com/page').kind, 'link');
});

test('resolveAudioSamples handles strings, objects, and pasted iframes', () => {
  const items = resolveAudioSamples(
    [{ url: 'https://x.com/a.mp3', title: 'Demo' }, 'local.wav', ''],
    rel => `https://raw.test/${rel}`
  );
  assert.equal(items.length, 2);
  assert.deepEqual(items[0], { kind: 'file', url: 'https://x.com/a.mp3', host: 'x.com', title: 'Demo' });
  assert.equal(items[1].url, 'https://raw.test/local.wav');
});

test('parseInstagram handles reel, post, and tv URLs', () => {
  assert.deepEqual(
    parseInstagram('https://www.instagram.com/reel/DMKkotPsItQ/?utm_source=ig_web_copy_link'),
    { kind: 'reel', shortcode: 'DMKkotPsItQ' },
  );
  assert.deepEqual(
    parseInstagram('https://www.instagram.com/reels/DZJV1E0Pc8l'),
    { kind: 'reel', shortcode: 'DZJV1E0Pc8l' },
  );
  assert.deepEqual(
    parseInstagram('https://instagram.com/p/AbCdEfGhIjK/'),
    { kind: 'p', shortcode: 'AbCdEfGhIjK' },
  );
  assert.deepEqual(
    parseInstagram('https://www.instagram.com/tv/XyZ12345/embed'),
    { kind: 'tv', shortcode: 'XyZ12345' },
  );
  assert.equal(parseInstagram('https://www.instagram.com/musicthingmodular/'), null);
  assert.equal(parseInstagram('https://youtu.be/dQw4w9WgXcQ'), null);
});

test('instagramEmbedHtml builds an official Instagram blockquote embed', () => {
  const html = instagramEmbedHtml('https://www.instagram.com/reel/DMKkotPsItQ/');
  assert.match(html, /class="instagram-embed"/);
  assert.match(html, /class="instagram-media"/);
  assert.match(html, /data-instgrm-permalink="https:\/\/www\.instagram\.com\/reel\/DMKkotPsItQ\/"/);
});

test('classifyDemoVideo and videoEmbedHtml cover YouTube and Instagram', () => {
  const yt = classifyDemoVideo('https://youtu.be/dQw4w9WgXcQ');
  assert.equal(yt.provider, 'youtube');
  assert.equal(yt.id, 'dQw4w9WgXcQ');
  assert.match(videoEmbedHtml(yt.url), /youtube\.com\/embed\/dQw4w9WgXcQ/);

  const ytOffset = classifyDemoVideo('https://youtu.be/ABbWmZOtmig?t=1772');
  assert.equal(ytOffset.start, 1772);
  assert.match(videoEmbedHtml(ytOffset.url), /start=1772/);

  const ig = classifyDemoVideo('https://www.instagram.com/reel/DMKkotPsItQ/');
  assert.equal(ig.provider, 'instagram');
  assert.equal(ig.kind, 'reel');
  assert.match(videoEmbedHtml(ig.url), /instagram-media/);
  assert.match(videoEmbedHtml(ig.url), /data-instgrm-permalink="https:\/\/www\.instagram\.com\/reel\/DMKkotPsItQ\/"/);

  assert.equal(classifyDemoVideo('https://example.com/video'), null);
  assert.equal(videoEmbedHtml('https://example.com/video'), '');
});

test('parseYoutubeId handles watch, youtu.be, shorts, and embed URLs', () => {
  assert.equal(parseYoutubeId('https://www.youtube.com/watch?v=dQw4w9WgXcQ'), 'dQw4w9WgXcQ');
  assert.equal(parseYoutubeId('https://youtu.be/ABbWmZOtmig?t=1772'), 'ABbWmZOtmig');
  assert.equal(parseYoutubeId('https://www.youtube.com/shorts/abcdef12345'), 'abcdef12345');
  assert.equal(parseYoutubeId('https://www.youtube.com/embed/abcdef12345'), 'abcdef12345');
  assert.equal(parseYoutubeId('https://example.com/watch?v=nope'), null);
});

test('parseYoutubeStartSeconds preserves t= and start= offsets', () => {
  assert.equal(parseYoutubeStartSeconds('https://youtu.be/ABbWmZOtmig?si=x&t=1772'), 1772);
  assert.equal(parseYoutubeStartSeconds('https://www.youtube.com/watch?v=ABbWmZOtmig&t=1331s'), 1331);
  assert.equal(parseYoutubeStartSeconds('https://www.youtube.com/watch?v=ABbWmZOtmig&t=1h2m3s'), 3723);
  assert.equal(parseYoutubeStartSeconds('https://www.youtube.com/embed/ABbWmZOtmig?start=90'), 90);
  assert.equal(parseYoutubeStartSeconds('https://www.youtube.com/watch?v=ABbWmZOtmig#t=45s'), 45);
  assert.equal(parseYoutubeStartSeconds('https://www.youtube.com/watch?v=ABbWmZOtmig'), null);
});

test('parseYoutubeStartSeconds tolerates &amp; from sanitized README hrefs', () => {
  // sanitize-html encodes & in attributes; t= after another param becomes &amp;t=
  assert.equal(
    parseYoutubeStartSeconds('https://youtu.be/ABbWmZOtmig?si=bKNxzY5MFJ0kZ6UB&amp;t=1772'),
    1772,
  );
  assert.equal(
    parseYoutubeStartSeconds('https://www.youtube.com/watch?v=VFnUbPqJ7lY&amp;t=65s'),
    65,
  );
  assert.equal(
    parseYoutubeStartSeconds('https://youtu.be/D0H_VsJ15go?t=4819&amp;si=7J1yqLwJx2xuIX9x'),
    4819,
  );
});

test('youtubeEmbedHtml includes start= when the source URL has a time offset', () => {
  const html = youtubeEmbedHtml('https://youtu.be/ABbWmZOtmig?t=1772');
  assert.match(html, /youtube\.com\/embed\/ABbWmZOtmig\?rel=0&start=1772/);
  assert.match(
    youtubeEmbedHtml('https://youtu.be/ABbWmZOtmig?si=x&amp;t=1772'),
    /start=1772/,
  );
  assert.doesNotMatch(youtubeEmbedHtml('https://youtu.be/ABbWmZOtmig'), /start=/);
});

test('flash size inference reads 2mb/16mb tokens and ignores unmarked names', () => {
  assert.equal(inferFlashFromFilename('goldfish.2.0.16mb.uf2'), '16mb');
  assert.equal(inferFlashFromFilename('backyard_rain_16M_2_0_0.uf2'), '16mb');
  assert.equal(inferFlashFromFilename('433_sense_of_space_16mb_mayakovsky_cc0.uf2'), '16mb');
  assert.equal(inferFlashFromFilename('punk_confusion_2mb.uf2'), '2mb');
  assert.equal(inferFlashFromFilename('backyard_rain_2M_2_0_0.uf2'), '2mb');
  assert.equal(inferFlashFromFilename('goldfish.1.1.uf2'), null);
  assert.equal(inferFlashFromFilename('goldfish.2.0.2mb.uf2'), '2mb');
});

test('authored flash overrides filename inference; omit defaults to 2MB', () => {
  assert.equal(normalizeFlash('16MB'), '16mb');
  assert.equal(normalizeFlash('2m'), '2mb');
  assert.equal(normalizeFlash('32mb'), null);
  assert.equal(resolveDownloadFlash({ name: 'card_16mb.uf2' }), '16mb');
  assert.equal(resolveDownloadFlash({ name: 'card_16mb.uf2' }, '2mb'), '2mb');
  assert.equal(resolveDownloadFlash({ name: 'card.uf2' }), '2mb');
  assert.equal(assignDownloadFlash({ name: 'card.uf2' }).flash, undefined);
  assert.equal(assignDownloadFlash({ name: 'card.uf2' }, '16mb').flash, '16mb');
  assert.equal(assignDownloadFlash({ name: 'card_16mb.uf2' }, '2mb').flash, '2mb');
  assert.equal(flashAttrFromDownloads([{ name: 'a.uf2' }, { name: 'b.uf2', flash: '16mb' }]), '2mb 16mb');
  assert.equal(flashAttrFromDownloads([{ flash: '16mb' }]), '16mb');
  assert.equal(flashAttrFromDownloads([]), '2mb');
  assert.deepEqual(deriveMemoryFromDownloads([{ flash: '16mb' }]), { size: '16mb', requirement: 'only' });
  assert.equal(deriveMemoryFromDownloads([{ name: 'a.uf2' }, { flash: '16mb' }]), undefined);
});
