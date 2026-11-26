// SPDX-FileCopyrightText: 2026 SOPTIM AG
// SPDX-License-Identifier: LicenseRef-Proprietary
// Author: Udo Schmitz (SOPTIM AG)
// All rights reserved by SOPTIM AG.
//
// gui_screenshot.js - renders the page of a running test GUI with WebKitGTK in an
// offscreen window and writes the whole page as PNG. Console messages of the
// page (JavaScript errors) go to stdout. Started by tools/gui_screenshot.sh.
//
//   gjs tools/gui_screenshot.js URL OUT.png [WIDTH] [WAIT_MS]
imports.gi.versions.Gtk = '3.0';
imports.gi.versions.WebKit2 = '4.0';
const {Gtk, WebKit2, GLib} = imports.gi;
const System = imports.system;

const [url, out, width = '1500', wait = '4000'] = ARGV;
Gtk.init(null);
const win = new Gtk.OffscreenWindow();
const view = new WebKit2.WebView();
view.get_settings().set_enable_write_console_messages_to_stdout(true);
win.set_default_size(Number(width), 1200);
win.add(view);
win.show_all();
let rc = 1;
view.connect('load-changed', (v, event) => {
    if (event !== WebKit2.LoadEvent.FINISHED) return;
    GLib.timeout_add(GLib.PRIORITY_DEFAULT, Number(wait), () => {
        view.get_snapshot(WebKit2.SnapshotRegion.FULL_DOCUMENT, WebKit2.SnapshotOptions.NONE, null,
            (src, res) => {
                try {
                    const surface = view.get_snapshot_finish(res);
                    surface.writeToPNG(out);
                    print('Bild: ' + out);
                    rc = 0;
                } catch (e) {
                    print('Fehler: ' + e);
                }
                Gtk.main_quit();
            });
        return false;
    });
});
view.load_uri(url);
GLib.timeout_add(GLib.PRIORITY_DEFAULT, 60000, () => { print('Zeitüberschreitung'); Gtk.main_quit(); return false; });
Gtk.main();
System.exit(rc);
