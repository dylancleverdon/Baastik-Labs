/**
 * Pickless suggestion box: receives suggestions from the Pickless app and adds them to the first
 * tab of "elimination diet app suggestion sheet". Deploy it as a web app (see suggestions/README.md).
 *
 * The app posts JSON: {id, kind, text, from, version, phone, android, at, log?}. Each suggestion has
 * an id, so if the app sends one twice (after a network hiccup), it still appears only once.
 * "Send logs" adds the app's recent logs: they're saved as a text file in a "Pickless logs" folder
 * next to this spreadsheet, linked from the Log column.
 *
 * Opening the web app's link in a browser shows a QR code: scan it from Pickless
 * (More > Suggestion box > Scan QR code) to connect the app to this sheet.
 */
var HEADERS = ['When', 'From', 'Type', 'Suggestion', 'Pickless version', 'Phone', 'Android', 'ID', 'Log'];
var ID_COLUMN = 8;

function doPost(e) {
  var lock = LockService.getScriptLock();
  lock.waitLock(20000);
  try {
    var data = JSON.parse(e.postData.contents);
    var text = String(data.text || '').slice(0, 5000);
    if (!data.id || !text) {
      return reply({ ok: false, error: 'missing id or text' });
    }
    var sheet = SpreadsheetApp.getActiveSpreadsheet().getSheets()[0];
    ensureHeaders(sheet);
    var last = sheet.getLastRow();
    if (last > 1) {
      var ids = sheet.getRange(2, ID_COLUMN, last - 1, 1).getValues();
      for (var i = 0; i < ids.length; i++) {
        if (ids[i][0] === data.id) {
          return reply({ ok: true, duplicate: true });
        }
      }
    }
    var logUrl = '';
    if (data.log) {
      var name = 'Pickless log ' + String(data.at || '') + ' ' + data.id + '.txt';
      logUrl = logsFolder().createFile(name, String(data.log).slice(0, 8000000), MimeType.PLAIN_TEXT).getUrl();
    }
    sheet.appendRow([
      new Date(),
      String(data.from || ''),
      String(data.kind || ''),
      text,
      String(data.version || ''),
      String(data.phone || ''),
      String(data.android || ''),
      String(data.id),
      logUrl
    ]);
    return reply(logUrl ? { ok: true, log: logUrl } : { ok: true });
  } finally {
    lock.releaseLock();
  }
}

// Row 1 holds the headers. Anything already there (like a note typed in A1) moves down one row.
function ensureHeaders(sheet) {
  if (sheet.getLastRow() === 0) {
    sheet.appendRow(HEADERS);
  } else if (String(sheet.getRange(1, 1).getValue()) !== HEADERS[0]) {
    sheet.insertRowBefore(1);
    sheet.getRange(1, 1, 1, HEADERS.length).setValues([HEADERS]);
  }
  sheet.getRange(1, 1, 1, HEADERS.length).setFontWeight('bold');
  sheet.setFrozenRows(1);
}

// "Pickless logs", in the same Drive folder as this spreadsheet (made the first time it's needed).
function logsFolder() {
  var parents = DriveApp.getFileById(SpreadsheetApp.getActiveSpreadsheet().getId()).getParents();
  var parent = parents.hasNext() ? parents.next() : DriveApp.getRootFolder();
  var found = parent.getFoldersByName('Pickless logs');
  return found.hasNext() ? found.next() : parent.createFolder('Pickless logs');
}

function doGet(e) {
  if (e && e.parameter && e.parameter.format === 'json') {
    return reply({ ok: true, service: 'Pickless suggestion box' });
  }
  var url = ScriptApp.getService().getUrl();
  var data = encodeURIComponent(url);
  var html =
    '<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">' +
    '<style>body{font-family:system-ui,sans-serif;background:#FBF8F2;color:#1C211E;text-align:center;padding:24px}' +
    'img{width:280px;height:280px;background:#fff;padding:12px;border-radius:16px}code{word-break:break-all;font-size:12px}</style></head><body>' +
    '<h2>Connect Pickless to this sheet</h2>' +
    '<p>In Pickless: More &rsaquo; Suggestion box &rsaquo; Scan QR code, then point the camera here.</p>' +
    '<img alt="QR code" src="https://api.qrserver.com/v1/create-qr-code/?size=280x280&margin=0&data=' + data + '" ' +
    'onerror="this.onerror=null;this.src=\'https://quickchart.io/qr?size=280&margin=1&text=' + data + '\'">' +
    '<p>Or paste this link into Pickless:</p><p><code>' + url + '</code></p></body></html>';
  return HtmlService.createHtmlOutput(html).setTitle('Pickless suggestion box');
}

function reply(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj)).setMimeType(ContentService.MimeType.JSON);
}
