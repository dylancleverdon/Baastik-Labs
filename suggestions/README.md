# Tutti suggestion box

The suggestion box in the Tutti app posts to the Google Sheet **"suggestions sheet tutti"**
through the small Apps Script web app in `Code.gs`. One-time setup (about two minutes, in the
browser, by the sheet's owner):

1. Open the sheet, then **Extensions > Apps Script**.
2. Replace everything in `Code.gs` with the contents of this folder's `Code.gs`, and save.
3. **Deploy > New deployment**. Click the gear next to "Select type" and choose **Web app**.
   - Execute as: **Me**
   - Who has access: **Anyone**
4. Click **Deploy** and allow access when Google asks. Google warns that the app isn't
   verified: click **Advanced**, then **Go to ... (unsafe)**. That's expected for your own script.
5. Copy the **Web app URL** (it ends in `/exec`) into `suggestions/endpoint.txt` on the `tutti`
   branch (or send it to Claude to do it).

The next release puts the URL into the update manifest, and every Tutti picks it up within the
hour. Suggestions made before then wait on each computer and are sent once the URL is known.
Each suggestion has an id, so nothing is ever added twice.

The URL ships inside the public update manifest, so in principle anyone who goes looking could
add rows. The script caps each suggestion's length; if junk ever shows up, make a new deployment
(new URL), put it in `endpoint.txt`, and the old one stops working once you archive it.
