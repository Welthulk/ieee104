// SPDX-FileCopyrightText: 2026 SOPTIM AG
// SPDX-License-Identifier: LicenseRef-Proprietary
// Author: Udo Schmitz (SOPTIM AG)
// All rights reserved by SOPTIM AG.
//
// test_handlers_webkit.js - checks the inline handlers of the GUI page in a real browser
// engine (WebKitGTK): every name called in onclick/onchange/oninput must not be a property
// of the element, its form or the document, because inline handlers look names up there
// first (a function command() is never called from a button, which has a property
// "command"). The page is loaded from the file, no GUI program needed.
//
//   gjs tests/gui/test_handlers_webkit.js examples/julia/gui/index.html
// Exit code 0 = passed, 1 = a name is shadowed, 2 = WebKit not usable (no display).
imports.gi.versions.Gtk = '3.0';
imports.gi.versions.WebKit2 = '4.0';
const System = imports.system;
let Gtk, WebKit2, GLib;
try {
    ({Gtk, WebKit2, GLib} = imports.gi);
    if (!Gtk.init_check(null)[0]) throw new Error('kein Display');
} catch (e) {
    print('WebKit nicht nutzbar: ' + e);
    System.exit(2);
}
const file = imports.gi.Gio.File.new_for_path(ARGV[0]);
const win = new Gtk.OffscreenWindow();
const view = new WebKit2.WebView();
win.add(view);
win.show_all();
const probe = `(() => {
  const bad = [], seen = new Set();
  for (const el of document.querySelectorAll('[onclick],[onchange],[oninput]')) {
    for (const attr of ['onclick', 'onchange', 'oninput']) {
      const code = el.getAttribute(attr) || '';
      for (const m of code.matchAll(/(^|[^.\\w$])([A-Za-z_$][\\w$]*)\\s*\\(/g)) {
        const n = m[2];
        seen.add(n);
        if (n in el || n in document || (el.form && n in el.form)) bad.push(el.tagName.toLowerCase() + '.' + attr + ': ' + n);
      }
    }
  }
  return JSON.stringify({bad, names: [...seen].length});
})()`;
let rc = 1;
view.connect('load-changed', (v, ev) => {
    if (ev !== WebKit2.LoadEvent.FINISHED) return;
    view.run_javascript(probe, null, (src, res) => {
        try {
            const r = JSON.parse(view.run_javascript_finish(res).get_js_value().to_string());
            print(`${r.names} Handler-Namen geprüft` + (r.bad.length ? '; überdeckt: ' + r.bad.join(', ') : ', keiner überdeckt'));
            rc = r.bad.length ? 1 : 0;
        } catch (e) {
            print('Fehler: ' + e);
        }
        Gtk.main_quit();
    });
});
view.load_uri(file.get_uri());
GLib.timeout_add(GLib.PRIORITY_DEFAULT, 30000, () => { print('Zeitüberschreitung'); Gtk.main_quit(); return false; });
Gtk.main();
System.exit(rc);
