// Tests for the pure utility helpers shared by build and browser preview.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { arrayOrEmpty, normalizeYamlKey, slugify } from '../src/utils/strings.js';
import { normalizeTags, normalizeDraft, normalizeContact, resolveAudioSample } from '../src/discover/infoFields.js';
import { extractIframeSrc, classifyAudioUrl, resolveAudioSamples } from '../src/utils/audio.js';
import { parseInstagram, instagramEmbedHtml } from '../src/utils/instagram.js';
import { classifyDemoVideo, videoEmbedHtml } from '../src/utils/video.js';

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

  const ig = classifyDemoVideo('https://www.instagram.com/reel/DMKkotPsItQ/');
  assert.equal(ig.provider, 'instagram');
  assert.equal(ig.kind, 'reel');
  assert.match(videoEmbedHtml(ig.url), /instagram-media/);
  assert.match(videoEmbedHtml(ig.url), /data-instgrm-permalink="https:\/\/www\.instagram\.com\/reel\/DMKkotPsItQ\/"/);

  assert.equal(classifyDemoVideo('https://example.com/video'), null);
  assert.equal(videoEmbedHtml('https://example.com/video'), '');
});
