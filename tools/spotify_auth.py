#!/usr/bin/env python3
"""One-time Spotify authorization for MacPlayer.

Works like spotify-matrix's first run: it opens the Spotify login page in a
modern browser, catches the redirect on http://127.0.0.1:8888/callback, and
trades the code for a refresh token. It then writes a complete
"MacPlayer Preferences" file (Client ID, Client Secret and refresh token).
Copy that file into System Folder:Preferences on the Mac and MacPlayer logs
in by itself from then on.

Only run this again if MacPlayer can't log in any more (access removed in
your Spotify account, Client Secret reset, or MacPlayer's scopes changed).

You need a Spotify app for its Client ID and Client Secret. Create one at
https://developer.spotify.com/dashboard with this exact Redirect URI:

    http://127.0.0.1:8888/callback

The script prints the full steps when it asks for the Client ID.

Usage:
    python3 tools/spotify_auth.py
    python3 tools/spotify_auth.py --out "/path/to/MacPlayer Preferences"
    SPOTIFY_CLIENT_ID=... SPOTIFY_CLIENT_SECRET=... python3 tools/spotify_auth.py
"""

import argparse
import base64
import getpass
import html
import http.server
import json
import os
import secrets
import shutil
import ssl
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
import webbrowser

AUTHORIZE_URL = "https://accounts.spotify.com/authorize"
TOKEN_URL = "https://accounts.spotify.com/api/token"

# Must match SpotifyClient::_permissions in src/api/SpotifyClient.h
SCOPES = " ".join([
    "user-read-playback-state",
    "user-modify-playback-state",
    "user-read-recently-played",
    "user-library-read",
    "playlist-read-private",
    "playlist-read-collaborative",
])

# Must match the login fields at the start of struct PrefsData in src/Prefs.h:
# fixed-size, NUL-padded C strings, no padding between them (766 bytes).
# MacPlayer's own settings (full screen) follow them in the struct; a file
# without them loads them at their defaults. The access token is left empty
# so MacPlayer fetches a fresh one from the refresh token on first launch.
PREFS_FIELDS = (
    ("access_token", 255),
    ("refresh_token", 255),
    ("client_id", 128),
    ("client_secret", 128),
)
PREFS_FILE_NAME = "MacPlayer Preferences"

SETUP_GUIDE = """\
First time? Create a Spotify app to get a Client ID and Client Secret:

  1. Go to https://developer.spotify.com/dashboard and log in with your
     Spotify account (accept the developer terms if asked).
  2. Click "Create app" and fill in:
       App name:          MacPlayer
       App description:   Spotify remote for a 68k Mac
       Redirect URIs:     {redirect_uri}   (type it exactly, then click Add)
       APIs used:         Web API
     Tick the terms checkbox and click Save.
  3. On the app's page, open Settings. Copy the Client ID, then click
     "View client secret" and copy the Client Secret.

The Redirect URI must match exactly: 127.0.0.1 (not localhost), port {port},
no trailing slash. If the browser later says "INVALID_CLIENT: Invalid
redirect URI", correct it in the app's Settings in the dashboard.

Your own Spotify account can use the app right away. Other accounts need
to be added under User Management in the dashboard first.
"""

# Shown in the browser and the terminal once the file is written
DONE_MESSAGE = (
    'Copy "MacPlayer Preferences" into System Folder:Preferences on your '
    "Macintosh, then open MacPlayer.\n"
    "The Preferences file holds your Client Secret and refresh token, "
    "so keep it private."
)


class AuthError(Exception):
    """A login failure, reported in both the browser and the terminal."""


def wait_for_callback(port, on_callback):
    """Serve http://127.0.0.1:<port>/callback until Spotify redirects to it.

    on_callback(params) finishes the login and returns the text for the
    browser. The page waits for it, so it shows the real outcome rather than
    claiming success before the token exchange. Returns None on success or
    the AuthError that stopped the login."""
    outcome = []

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            url = urllib.parse.urlparse(self.path)
            if url.path != "/callback":
                self.send_error(404)
                return
            try:
                message = on_callback(dict(urllib.parse.parse_qsl(url.query)))
                outcome.append(None)
            except Exception as e:  # always answer the browser and stop serving
                error = e if isinstance(e, AuthError) else AuthError(f"Unexpected error: {e!r}")
                message = f"MacPlayer login failed: {error}"
                outcome.append(error)
            paragraphs = "".join(f"<p>{html.escape(line)}</p>" for line in message.splitlines())
            body = f"<html><body>{paragraphs}</body></html>".encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format, *args):
            pass

    try:
        server = http.server.HTTPServer(("127.0.0.1", port), Handler)
    except OSError as e:
        sys.exit(f"Can't listen on 127.0.0.1:{port}: {e}")

    with server:
        while not outcome:
            server.handle_request()
    return outcome[0]


def ssl_context():
    """Default TLS context, plus macOS's own CA bundle when Python has none
    (python.org builds ship without one until 'Install Certificates.command' runs)."""
    context = ssl.create_default_context()
    if not context.cert_store_stats()["x509_ca"] and os.path.exists("/etc/ssl/cert.pem"):
        context.load_verify_locations("/etc/ssl/cert.pem")
    return context


def request_tokens(client_id, client_secret, code, redirect_uri):
    data = urllib.parse.urlencode({
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": redirect_uri,
    }).encode()
    basic = base64.b64encode(f"{client_id}:{client_secret}".encode()).decode()
    request = urllib.request.Request(TOKEN_URL, data=data, headers={
        "Authorization": "Basic " + basic,
        "Content-Type": "application/x-www-form-urlencoded",
    })

    try:
        with urllib.request.urlopen(request, timeout=30, context=ssl_context()) as response:
            return json.load(response)
    except urllib.error.HTTPError as e:
        detail = e.read().decode(errors="replace")
        raise AuthError(f"Token exchange failed (HTTP {e.code}): {detail}")
    except urllib.error.URLError as e:
        hint = ""
        if "CERTIFICATE_VERIFY_FAILED" in str(e.reason):
            hint = ("\nOn python.org builds for macOS, run "
                    "'Install Certificates.command' from the Python folder.")
        raise AuthError(f"Can't reach {TOKEN_URL}: {e.reason}{hint}")


def build_prefs(values):
    data = b""
    for name, size in PREFS_FIELDS:
        raw = values.get(name, "").encode("ascii")
        if len(raw) >= size:
            raise AuthError(f"The {name} is {len(raw)} characters; MacPlayer stores at most {size - 1}.")
        data += raw.ljust(size, b"\0")
    return data


def set_finder_info(path):
    """Mark the file as type 'pref', creator 'MPLY' so the Mac shows it as
    MacPlayer's preferences. Basilisk II's shared folder reads its own
    attribute; macOS and other tools read com.apple.FinderInfo."""
    if sys.platform != "darwin" or not shutil.which("xattr"):
        return
    basilisk = b"prefMPLY" + b"\x00\x00" + b"\xff\xff\xff\xff" + b"\x00\x00"
    for name, value in (("com.apple.FinderInfo", b"prefMPLY" + bytes(24)),
                        ("org.BasiliskII.FinderInfo", basilisk)):
        subprocess.run(["xattr", "-wx", name, value.hex(), path], check=False)


def main():
    parser = argparse.ArgumentParser(
        description="Log in to Spotify once and write MacPlayer's preferences file.")
    parser.add_argument("--client-id", default=os.environ.get("SPOTIFY_CLIENT_ID"))
    parser.add_argument("--client-secret", default=os.environ.get("SPOTIFY_CLIENT_SECRET"),
                        help="prompted for if omitted, so it stays out of shell history")
    parser.add_argument("--out", default=PREFS_FILE_NAME,
                        help=f'where to write the file (default: "{PREFS_FILE_NAME}" in the current folder)')
    parser.add_argument("--port", type=int, default=8888,
                        help="callback port; the dashboard Redirect URI must match (default 8888)")
    parser.add_argument("--no-browser", action="store_true",
                        help="only print the login URL (e.g. when running over SSH)")
    args = parser.parse_args()

    redirect_uri = f"http://127.0.0.1:{args.port}/callback"

    if not args.client_id:
        print(SETUP_GUIDE.format(redirect_uri=redirect_uri, port=args.port))
    client_id = args.client_id or input("Spotify Client ID: ").strip()
    if not client_id:
        sys.exit("A Client ID is required (see the steps above).")
    client_secret = args.client_secret or getpass.getpass("Spotify Client Secret: ").strip()
    if not client_secret:
        sys.exit("A Client Secret is required (see the steps above).")
    state = secrets.token_urlsafe(16)
    auth_url = AUTHORIZE_URL + "?" + urllib.parse.urlencode({
        "client_id": client_id,
        "response_type": "code",
        "redirect_uri": redirect_uri,
        "scope": SCOPES,
        "state": state,
    })

    print(f"Redirect URI (must be listed in the Spotify dashboard): {redirect_uri}")
    print("\nOpen this URL and log in to Spotify:\n")
    print(auth_url + "\n", flush=True)
    if not args.no_browser:
        webbrowser.open(auth_url)

    def finish_login(params):
        if "error" in params:
            raise AuthError(f"Spotify returned an error: {params['error']}")
        if params.get("state") != state:
            raise AuthError("State mismatch in callback. Run the script again.")
        if "code" not in params:
            raise AuthError("Callback had no authorization code.")

        # Also proves the Client ID/Secret pair: Spotify rejects a wrong one here
        tokens = request_tokens(client_id, client_secret, params["code"], redirect_uri)
        refresh_token = tokens.get("refresh_token")
        if not refresh_token:
            raise AuthError(f"No refresh token in response: {tokens}")

        prefs = build_prefs({
            "refresh_token": refresh_token,
            "client_id": client_id,
            "client_secret": client_secret,
        })
        try:
            with open(args.out, "wb") as f:
                f.write(prefs)
        except OSError as e:
            raise AuthError(f"Can't write {args.out}: {e}")
        set_finder_info(args.out)
        return DONE_MESSAGE

    error = wait_for_callback(args.port, finish_login)
    if error:
        sys.exit(str(error))

    print(f'Wrote "{os.path.abspath(args.out)}".\n')
    print(DONE_MESSAGE)
    print("If MacPlayer is already open, quit and reopen it.")


if __name__ == "__main__":
    main()
