/**
 * Tutti's suggestion box: receives suggestions from the Tutti app and adds them to the first
 * tab of this spreadsheet. Deploy it as a web app (see suggestions/README.md).
 *
 * The app posts JSON: {id, kind, text, from, version, os, live, at, log?}. Each suggestion has an
 * id, so if the app sends one twice (after a network hiccup), it still appears only once.
 * "Send logs" adds the recent logs: they're saved as a text file in a "Tutti logs" folder next to
 * this spreadsheet, linked from the Log column.
 */
var HEADERS = ['When', 'From', 'Type', 'Suggestion', 'Tutti version', 'Computer', 'Live', 'ID', 'Log'];

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
    if (sheet.getLastRow() === 0) {
      sheet.appendRow(HEADERS);
    } else if (sheet.getRange(1, 9).getValue() === '') {
      sheet.getRange(1, 9).setValue('Log');
    }
    var last = sheet.getLastRow();
    if (last > 1) {
      var ids = sheet.getRange(2, 8, last - 1, 1).getValues();
      for (var i = 0; i < ids.length; i++) {
        if (ids[i][0] === data.id) {
          return reply({ ok: true, duplicate: true });
        }
      }
    }
    var logUrl = '';
    if (data.log) {
      var name = 'Tutti log ' + String(data.from || 'someone') + ' ' + String(data.at || '') + ' ' + data.id + '.txt';
      logUrl = logsFolder().createFile(name, String(data.log).slice(0, 8000000), MimeType.PLAIN_TEXT).getUrl();
    }
    sheet.appendRow([
      new Date(),
      String(data.from || ''),
      String(data.kind || ''),
      text,
      String(data.version || ''),
      String(data.os || ''),
      String(data.live || ''),
      String(data.id),
      logUrl
    ]);
    return reply(logUrl ? { ok: true, log: logUrl } : { ok: true });
  } finally {
    lock.releaseLock();
  }
}

// "Tutti logs", in the same Drive folder as this spreadsheet (made the first time it's needed).
function logsFolder() {
  var parents = DriveApp.getFileById(SpreadsheetApp.getActiveSpreadsheet().getId()).getParents();
  var parent = parents.hasNext() ? parents.next() : DriveApp.getRootFolder();
  var found = parent.getFoldersByName('Tutti logs');
  return found.hasNext() ? found.next() : parent.createFolder('Tutti logs');
}

function doGet() {
  return reply({ ok: true, service: 'Tutti suggestion box' });
}

function reply(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj)).setMimeType(ContentService.MimeType.JSON);
}
