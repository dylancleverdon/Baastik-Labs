# Pickless suggestion box

The suggestion box in Pickless posts to the Google Sheet **"elimination diet app suggestion sheet"**
through the small Apps Script web app in `Code.gs`. One-time setup (about two minutes, in a browser
on your computer, by the sheet's owner):

1. Open the sheet, then **Extensions > Apps Script**.
2. Replace everything in `Code.gs` with the contents of this folder's `Code.gs`, and save.
3. **Deploy > New deployment**. Click the gear next to "Select type" and choose **Web app**.
   - Execute as: **Me**
   - Who has access: **Anyone**
4. Click **Deploy** and allow access when Google asks. Google warns that the app isn't verified:
   click **Advanced**, then **Go to ... (unsafe)**. That's expected for your own script.
5. Click the **Web app URL** (it ends in `/exec`). The page shows a QR code.
6. In Pickless: **More > Suggestion box > Scan QR code**, and point the phone at the page.

That's it: suggestions made before you connected are sent right away (each has an id, so nothing is
added twice). The script moves anything already in row 1 down and puts its headers there.

Optional: put the `/exec` URL into `suggestions/endpoint.txt` on the `pickless` branch (or ask
Claude to). The next release puts it in the update manifest, so a reinstalled Pickless connects by
itself.

## Updating the script (keeps the same URL)

1. Open the sheet, then **Extensions > Apps Script**, replace `Code.gs` with this folder's, and save.
2. **Deploy > Manage deployments**, click the pencil on the existing deployment, set **Version** to
   **New version**, and click **Deploy**.

The URL is a secret only in the sense that anyone who has it can add rows. The script caps each
suggestion's length; if junk ever shows up, make a new deployment (new URL), scan its QR code again,
and archive the old deployment.
