/**
 * Tutti's suggestion box: receives suggestions from the Tutti app and adds them to the first
 * tab of this spreadsheet. Deploy it as a web app (see suggestions/README.md).
 *
 * The app posts JSON: {id, kind, text, from, version, os, live, at}. Each suggestion has an id,
 * so if the app sends one twice (after a network hiccup), it still appears only once.
 */
var HEADERS = ['When', 'From', 'Type', 'Suggestion', 'Tutti version', 'Computer', 'Live', 'ID'];

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
    sheet.appendRow([
      new Date(),
      String(data.from || ''),
      String(data.kind || ''),
      text,
      String(data.version || ''),
      String(data.os || ''),
      String(data.live || ''),
      String(data.id)
    ]);
    return reply({ ok: true });
  } finally {
    lock.releaseLock();
  }
}

function doGet() {
  return reply({ ok: true, service: 'Tutti suggestion box' });
}

function reply(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj)).setMimeType(ContentService.MimeType.JSON);
}
