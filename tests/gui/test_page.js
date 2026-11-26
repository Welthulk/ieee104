// SPDX-FileCopyrightText: 2026 SOPTIM AG
// SPDX-License-Identifier: LicenseRef-Proprietary
// Author: Udo Schmitz (SOPTIM AG)
// All rights reserved by SOPTIM AG.
//
// test_page.js - runs the script of the test GUI page (examples/julia/gui/index.html)
// in gjs with a small DOM stand-in against a running GUI, the way a browser would:
// station, value list, connect with STARTDT and interrogation, command with
// feedback, self-test, log filters, tabs. Unknown element ids, exceptions,
// HTTP answers other than 200 and texts in the error line of the page fail.
//
//   gjs tests/gui/test_page.js examples/julia/gui/index.html PORT
//
// Started by tests/gui/test_page.sh. Exit code 0 = passed, 1 = failed.
const ByteArray = imports.byteArray;
const System = imports.system;
const GLib = imports.gi.GLib;
const [htmlPath, port] = ARGV;
const html = ByteArray.toString(GLib.file_get_contents(htmlPath)[1]);
const script = html.slice(html.indexOf('<script>') + 8, html.lastIndexOf('</script>'));
const errors = [];
function err(where, e) { errors.push(where + ': ' + e + (e && e.stack ? '\n' + e.stack.split('\n').slice(0, 4).join('\n') : '')); }

// --- elements from the HTML: id, tag, initial value/checked, first option
const els = {};
const tagRe = /<(input|select|textarea|button|span|div|tbody|section|code|p|nav|header|label|table)\b([^>]*)>/g;
let m;
const sections = [], navButtons = [];
function makeEl(tag, attrs) {
  const id = (attrs.match(/\bid="([^"]+)"/) || [])[1];
  const el = {tag, id, value: (attrs.match(/\bvalue="([^"]*)"/) || [])[1] || '', checked: /\bchecked\b/.test(attrs),
    textContent: '', innerHTML: '', hidden: /\bhidden\b/.test(attrs), className: (attrs.match(/\bclass="([^"]*)"/) || [])[1] || '',
    options: [], disabled: false, style: {}, dataset: {}, title: '', scrollTop: 0, clientHeight: 100, scrollHeight: 100,
    parentElement: {title: ''},
    add(o) { this.options.push(o); if (this.options.length === 1 && this.value === '') this.value = o.value; },
    contains() { return false; }, focus() {}, select() {}, click() {}, remove() {}, appendChild() {}, closest() { return null; }};
  const dt = attrs.match(/\bdata-tab="([^"]+)"/);
  if (dt) el.dataset.tab = dt[1];
  return el;
}
while ((m = tagRe.exec(html)) !== null) {
  if (m.index > html.indexOf('<script>')) break;
  const el = makeEl(m[1], m[2]);
  if (m[1] === 'select') {   // first option value
    const rest = html.slice(tagRe.lastIndex, html.indexOf('</select>', tagRe.lastIndex));
    const o = rest.match(/<option value="([^"]*)"/);
    if (o) el.value = o[1];
  }
  if (m[1] === 'section' && el.className.includes('tab')) sections.push(el);
  if (m[1] === 'button' && html.lastIndexOf('<nav>', m.index) > html.lastIndexOf('</nav>', m.index)) navButtons.push(el);
  if (el.id) { if (els[el.id]) errors.push('doppelte id ' + el.id); els[el.id] = el; }
}
const missing = new Set();
globalThis.document = {
  getElementById(id) { if (!(id in els)) { missing.add(id); return null; } return els[id]; },
  querySelectorAll(q) { return q === 'section.tab' ? sections : q === 'nav button' ? navButtons : []; },
  createElement(t) { return makeEl(t, ''); }, body: {appendChild() {}}, activeElement: null};
globalThis.Option = function (text, value) { this.text = text; this.value = String(value); this.disabled = false; };
globalThis.URLSearchParams = function (o) { this.o = o; };
URLSearchParams.prototype.toString = function () {
  return Object.entries(this.o).map(([k, v]) => encodeURIComponent(k) + '=' + encodeURIComponent(String(v))).join('&');
};
globalThis.isSecureContext = true;   // in gjs, window is the global object itself
globalThis.navigator = {clipboard: {writeText: async t => { globalThis.clipboard = t; }}};
globalThis.alert = t => { globalThis.lastAlert = t; };
globalThis.confirm = () => true;
globalThis.URL = {createObjectURL: () => 'blob:x', revokeObjectURL() {}};
globalThis.Blob = function (parts) { this.parts = parts; };
let timers = [];
globalThis.setTimeout = (f, ms) => { timers.push(f); return timers.length; };
globalThis.setInterval = () => 0;
globalThis.fetch = async (url) => {
  const [ok, out, errout, status] = GLib.spawn_sync(null, ['curl', '-sS', '--max-time', '10', '-w', '\n%{http_code}',
    '-H', 'X-IEC104-GUI: 1', 'http://127.0.0.1:' + port + url], null, GLib.SpawnFlags.SEARCH_PATH, null);
  const text = ByteArray.toString(out);
  const i = text.lastIndexOf('\n');
  const body = text.slice(0, i), code = Number(text.slice(i + 1));
  if (code !== 200) errors.push('HTTP ' + code + ' für ' + url + ': ' + body.slice(0, 200));
  return {ok: code === 200, status: code, text: async () => body, json: async () => JSON.parse(body), blob: async () => new Blob([body])};
};
function runTimers() { const t = timers; timers = []; for (const f of t) { try { f(); } catch (e) { err('Timer', e); } } }

// Inline handlers (onclick="f()") look names up on the element and the document first: a
// function named like one of their properties is never called. "command" is such a
// property of buttons in current browsers (invoker commands); the list holds the
// names of HTMLElement, button, select, input and document that a handler could use.
const SHADOWING = new Set(('command commandForElement popover popoverTargetElement popoverTargetAction form ' +
  'formAction name value type labels click focus blur select remove before after append prepend replaceWith ' +
  'toggle show close open write writeln submit reset checkValidity reportValidity setCustomValidity add item ' +
  'namedItem stepUp stepDown showPicker title hidden lang dir style dataset children closest matches animate ' +
  'scroll scrollTo scrollBy scrollIntoView contains normalize evaluate find fetch').split(' '));
const handlerNames = new Set([...html.matchAll(/\bon(?:click|change|input)="([A-Za-z_$][\w$]*)\(/g)].map(x => x[1]));
for (const n of handlerNames) {
  if (SHADOWING.has(n)) errors.push(`Handler ${n}() kollidiert mit einer Eigenschaft von Element oder Dokument`);
  if (!new RegExp('(?:^|\\n)(?:async )?function ' + n + '\\(').test(script) && !/^(confirm|alert)$/.test(n))
    errors.push(`Handler ${n}() ist keine Funktion des Seitenskripts`);
}

let api;
try {
  api = new Function(script + '\n;return {refresh, act, connect, sendCommand, listAdd, listEdit, pickCommand, tab, drawLog, drawImage, renderCmds, logText, saveImage, copyText, copyReport, saveReport, sendPoint};')();
} catch (e) { err('Laden', e); }
const sleep = ms => new Promise(r => GLib.timeout_add(GLib.PRIORITY_DEFAULT, ms, () => { r(); return false; }));
async function tick(where) {
  try { await api.refresh(); } catch (e) { err(where, e); }
  runTimers();
  const t = els['err'].textContent;
  if (t && where !== 'Fehlerfall') errors.push(where + ': Fehlerzeile der Seite: ' + t);
}
const v = id => els[id];
async function main() {
  if (!api) return;
  await sleep(800);   // the page's own first refresh
  await tick('Start');
  await api.act('server/listen', {port: '0', station: '1'}); runTimers();
  await api.act('server/point', {type: 13, ioa: 100, value: 4.5});
  await api.act('server/point', {type: 1, ioa: 1001, value: 0});
  await tick('Station');
  const sport = (els['sstate'].textContent.match(/Port (\d+)/) || [])[1];
  print('Station: ' + els['sstate'].textContent);
  v('lst').value = '1'; v('lioa').value = '6001'; v('lty').value = '45'; v('ltx').value = 'Schalter Q1'; v('lfb').value = '1001';
  api.listAdd(); await sleep(300); await tick('Liste');
  v('port').value = sport; v('host').value = '127.0.0.1';
  api.connect(); await sleep(2500); await tick('Verbinden'); await sleep(300); await tick('Verbinden2');
  print('Leitstelle: ' + els['cstate'].textContent);
  v('cpick').value = String(1 * 16777216 + 6001); api.pickCommand();
  api.sendCommand(); await sleep(1500); await tick('Befehl');
  await api.act('server/point', {type: 1, ioa: 1001, value: 1}); await sleep(800); await tick('Rückmeldung');
  api.listEdit(1, 100); api.saveImage();
  await api.act('list/take'); await tick('Übernehmen');
  await api.act('selftest/start'); runTimers();
  for (let i = 0; i < 40; i++) { await sleep(1000); await tick('Selbsttest'); if (/bestanden/.test(els['tstate'].textContent)) break; }
  try { await api.copyReport(); await api.saveReport(); } catch (e) { err('Protokoll', e); }
  for (const [id, val] of [['fdir', 'in'], ['fty', '13'], ['fc', '20'], ['fioa', '100'], ['ftext', 'Generalabfrage']]) {
    v(id).value = val; try { api.drawLog(); } catch (e) { err('Filter ' + id, e); } v(id).value = '';
  }
  v('showobj').checked = true; v('showhex').checked = true; api.drawLog();
  try { api.logText(); } catch (e) { err('logText', e); }
  for (const t of ['station', 'selbst', 'pruef']) api.tab(t);
  runTimers();
  print('Selbsttest: ' + els['tstate'].textContent);
  print('Befehle: ' + (els['cmds'].innerHTML.match(/<tr>/g) || []).length + ' Zeilen; ' + els['cmds'].innerHTML.slice(0, 300));
  print('Prozessabbild: ' + (els['image'].innerHTML.match(/<tr/g) || []).length + ' Zeilen; ' + els['pcount'].textContent);
  print('Telegramme: ' + els['lcount'].textContent + '; Fehlerzeile: "' + els['err'].textContent + '"');
  // what a user must see
  const expect = (ok, what) => { if (!ok) errors.push('erwartet: ' + what); };
  expect(/^Port \d+, Station 1$/.test(els['sstate'].textContent), 'Station läuft');
  expect(els['cstate'].textContent === 'Datenübertragung läuft', 'Leitstelle mit Datenübertragung');
  expect(/^23 von 23 bestanden$|^22 von 22 bestanden$/.test(els['tstate'].textContent), 'Selbsttest bestanden');
  expect(/<td>Schalter Q1<\/td>.*ausgeführt.*ms, IOA 1001: ein/.test(els['cmds'].innerHTML), 'Befehl ausgeführt mit Rückmeldung');
  expect((els['image'].innerHTML.match(/<tr/g) || []).length >= 3, 'mindestens 3 Zeilen im Prozessabbild');
  expect(/Wert 4.5/.test(els['image'].innerHTML), 'Messwert im Prozessabbild');
  expect(/Leitstelle fordert alle Werte an/.test(els['log'].innerHTML), 'Rahmen mit Erklärung im Protokoll');
  expect(/Bestanden|bestanden/.test(globalThis.clipboard || ''), 'Protokoll in der Zwischenablage');
}
const loop = new GLib.MainLoop(null, false);
main().catch(e => err('main', e)).finally(() => {
  if (missing.size) errors.push('unbekannte ids: ' + [...missing].join(', '));
  print(errors.length ? 'FEHLER:\n' + errors.join('\n') : 'keine Fehler im Seitenskript');
  loop.quit();
});
loop.run();
System.exit(errors.length ? 1 : 0);
