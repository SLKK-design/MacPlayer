#include <vector>
#include <Folders.h>
#include <Script.h>
#include <Scrap.h>
#include <stdio.h>
#include "SpotifyClient.h"
#include "stb_image.h"
#include "CoverArt.h"
#include "Keys.h"
#include "Uri.h"
#include "base64.h"
#include "Util.h"
#include "DebugLog.h"

// Spotify explains a failed call in the body, e.g. a free account trying
// to control playback gets 403 with "reason":"PREMIUM_REQUIRED":
// { "error": { "status": 403, "message": "...", "reason": "..." } }
// Returns its message, or else the start of the body with its LF line
// breaks made CR, which is what a Mac breaks lines on.
string SpotifyClient::ErrorMessage(const string& body)
{
	string copy = body;  // the parser works in place
	JsonAllocator allocator;
	JsonValue root;
	if (!copy.empty() && jsonParse(&copy[0], root, allocator) == JSON_PARSE_OK &&
	    root.getTag() == JSON_OBJECT)
	{
		JsonValue error = root("error");
		if (error.getTag() == JSON_OBJECT && error("message").getTag() == JSON_STRING)
		{
			string message = error("message").toString();
			JsonValue reason = error("reason");
			if (reason.getTag() == JSON_STRING && string(reason.toString()) == "NO_ACTIVE_DEVICE")
				message += "\r\rStart Spotify on the device you play on, then choose it in "
				           "the Device menu (Refresh... if it isn't listed).";
			return message;
		}
	}

	string text = body.substr(0, 300);
	for (size_t i = 0; i < text.size(); i++)
		if (text[i] == '\n')
			text[i] = '\r';
	return text;
}

static string HttpErrorText(const string& uri, int statusCode, const string& body)
{
	string text = uri + ": HTTP " + to_string(statusCode) + " response.";
	if (!body.empty())
		text += "\r" + SpotifyClient::ErrorMessage(body);
	return text;
}

SpotifyClient::SpotifyClient(HttpClient* httpClient, Prefs* prefs)
{
	_httpClient = httpClient;
	_prefs = prefs;

	AccessToken = string(_prefs->Data.AccessToken);
	RefreshToken = string(_prefs->Data.RefreshToken);
	_cacheVRefNum = 0;
	_cacheDirId = 0;  // InitCache() runs from main(), after the Toolbox is up

	// Most requests go to the Web API; keep its TLS ticket and DNS entry
	// cached across host switches and restarts (see HttpClient::SetSessionHost).
	// Covers come from i.scdn.co: keeping its ticket too saves a ~5 s
	// handshake on the SE/30 for the first cover after a start.
	_httpClient->SetSessionHost("api.spotify.com");
	_httpClient->SetSecondSessionHost("i.scdn.co");
}

void SpotifyClient::GetRecentTracks(function<void(JsonValue&)> onComplete)
{
	Get(
		"https://api.spotify.com/v1/me/player/recently-played?limit=10", 
		false,
		onComplete);
}

/* A player command's URL. Naming MacPlayer's device makes the command reach
 * it even when Spotify doesn't count it as active, e.g. on a fresh start
 * before anything has played on it. */
string SpotifyClient::PlayerUrl(const string& command, const string& query)
{
	string params = query;
	if (!DeviceId.empty())
		params += (params.empty() ? "" : "&") + string("device_id=") + Uri::Encode(DeviceId);
	string url = "https://api.spotify.com/v1/me/player/" + command;
	return params.empty() ? url : url + "?" + params;
}

void SpotifyClient::PlayTrack(const string& contextUri, const string& trackUri, function<void(JsonValue&)> onComplete)
{
	Put(
		PlayerUrl("play"),
		false,
		"{ \"context_uri\": \"" + contextUri + "\", \"offset\": { \"uri\": \"" + trackUri + "\" } }",
		onComplete);
}

void SpotifyClient::NextTrack(function<void(JsonValue&)> onComplete)
{
	Post(
		PlayerUrl("next"),
		false,
		"",
		onComplete);
}

void SpotifyClient::PreviousTrack(function<void(JsonValue&)> onComplete)
{
	Post(
		PlayerUrl("previous"),
		false,
		"",
		onComplete);
}

void SpotifyClient::GetDevices(function<void(JsonValue&)> onComplete)
{
	Get(
		"https://api.spotify.com/v1/me/player/devices",
		false,
		onComplete);
}

void SpotifyClient::GetCurrentUser(function<void(JsonValue&)> onComplete)
{
	Get(
		"https://api.spotify.com/v1/me",
		false,
		onComplete);
}

void SpotifyClient::GetPlaylists(function<void(JsonValue&)> onComplete)
{
	Get(
		"https://api.spotify.com/v1/me/playlists",
		false,
		onComplete);
}

void SpotifyClient::GetPlaylistTracks(const string& playlistId, function<void(JsonValue&)> onComplete)
{
	// /tracks was removed in Feb 2026; /items only serves playlists the user
	// owns or collaborates on, and names each entry "item" (the old "track"
	// key is deprecated), so ask for both
	Get(
		"https://api.spotify.com/v1/playlists/" + playlistId + "/items?"
		"limit=20&"
		"fields=items(item(name,uri,album(id,images(url)),artists(name)),"
		"track(name,uri,album(id,images(url)),artists(name)))",
		false,
		onComplete);
}

/* Transfer playback to deviceId. "play": false keeps it as it was, playing
 * or paused ("play": true would start a paused track). */
void SpotifyClient::ActivateDevice(const string& deviceId, function<void(JsonValue&)> onComplete,
                                   SpotifyErrorHandler onError)
{
	Put(
		"https://api.spotify.com/v1/me/player",
		false,
		"{ \"device_ids\": [\"" + deviceId + "\"], \"play\": false }",
		onComplete,
		onError);
}

/* Spotify drops a device's "active" mark once it has sat paused or idle for
 * a while, and player commands then answer 404 NO_ACTIVE_DEVICE. Transfer
 * playback back to MacPlayer's device, without starting it: the command
 * that failed decides that. */
bool SpotifyClient::WakeDevice()
{
	bool woke = false;
	ActivateDevice(DeviceId,
		[&woke](JsonValue& root) { woke = true; },
		[](int statusCode, const string& body) {});  // the command's own error is shown
	DebugLog("WakeDevice", woke ? "playback transferred" : "failed");

	// Spotify applies a transfer a moment after confirming it
	if (woke)
	{
		unsigned long until;
		Delay(60, &until);
	}
	return woke;
}

/* Album covers are cached in "MacPlayer Cache" as their brightness at half
 * size (see CoverArt.h): 'MPC3', width and height (2 bytes each), then
 * width * height bytes. Older files ('MPC2'/'MPC1' 1-bit bitmaps of one
 * size, 270 KB RGB) fail the check and are fetched again. */
static const unsigned long kCoverMagic = 0x4D504333UL;  // 'MPC3'
enum { kCoverHeader = 8, kCoverMaxSide = 512 };

static void CoverFileSpec(short vRefNum, long dirId, const string& albumId, FSSpec* spec)
{
	// Album ID is used as filename (image IDs are too long for HFS)
	Str255 filename;
	size_t idLen = albumId.size();
	if (idLen > 31) idLen = 31;
	filename[0] = (unsigned char)idLen;
	memcpy(filename + 1, albumId.c_str(), idLen);
	FSMakeFSSpec(vRefNum, dirId, filename, spec);
}

void SpotifyClient::GetImage(const string& image, const string& albumId, function<void()> onComplete)
{
	FSSpec imageSpec;
	short file;
	memset(&imageSpec, 0, sizeof(imageSpec));

	// Until this track's art is loaded, show none rather than the last cover
	ActiveCover.clear();

	if (_cacheDirId != 0)
	{
		CoverFileSpec(_cacheVRefNum, _cacheDirId, albumId, &imageSpec);
		if (FSpOpenDF(&imageSpec, fsRdPerm, &file) == noErr)
		{
			long size = 0;
			GetEOF(file, &size);
			struct { unsigned long magic; short width, height; } header = { 0, 0, 0 };
			long count = kCoverHeader;
			bool valid = FSRead(file, &count, &header) == noErr && count == kCoverHeader &&
			             header.magic == kCoverMagic &&
			             header.width > 0 && header.width <= kCoverMaxSide &&
			             header.height > 0 && header.height <= kCoverMaxSide &&
			             size == kCoverHeader + (long)header.width * header.height;
			if (valid)
			{
				CoverGray cover;
				cover.width = header.width;
				cover.height = header.height;
				cover.pixels.resize((size_t)header.width * header.height);
				count = (long)cover.pixels.size();
				valid = FSRead(file, &count, &cover.pixels[0]) == noErr &&
				        count == (long)cover.pixels.size();
				if (valid)
					ActiveCover = cover;
			}
			FSClose(file);

			if (valid)
			{
				onComplete();
				return;
			}

			// Damaged, truncated or old-format cache file: fetch it again
			FSpDelete(&imageSpec);
		}
	}

	// Cache miss — download JPEG from Spotify CDN, decode, cache.
	// Certificates stay verified: offered only RSA suites, i.scdn.co answers
	// with its DigiCert G2 chain, which is in RootCert.cpp.
	_httpClient->SetAuthorization("");   // CDN needs no auth
	_httpClient->SwitchHost();           // api.spotify.com → i.scdn.co

	bool cacheable = _cacheDirId != 0;
	_httpClient->Get(image, [this, imageSpec, cacheable, onComplete](HttpResponse& response) {
		// Restore API connection for subsequent calls
		_httpClient->SetAuthorization("Bearer " + AccessToken);
		_httpClient->SwitchHost(); // i.scdn.co → api.spotify.com

		if (!response.Success || response.StatusCode != 200 || response.Content.empty()) {
			DebugLog("GetImage", ("download failed: " + to_string(response.StatusCode)).c_str());
			onComplete();
			return;
		}

		// One channel: for a JPEG that is its stored brightness (Y), exactly
		// what the gray cover shows, without the color conversion
		long t0 = TickCount();
		int w, h, channels;
		unsigned char* pixels = stbi_load_from_memory(
			(const stbi_uc*)response.Content.data(),
			(int)response.Content.size(),
			&w, &h, &channels, 1);

		if (!pixels) {
			DebugLog("GetImage", "decode failed");
			onComplete();
			return;
		}

		ActiveCover = ShrinkCover(pixels, w, h);
		stbi_image_free(pixels);

		long t1 = TickCount();
		if (cacheable && !ActiveCover.empty())
			SaveImage(&imageSpec, ActiveCover);

		char timing[96];
		snprintf(timing, sizeof(timing), "%dx%d: decode %ld, save %ld ticks",
		         w, h, t1 - t0, TickCount() - t1);
		DebugLog("Cover", timing);

		onComplete();
	});
}

void SpotifyClient::GetPlayerState(function<void(JsonValue&)> onComplete)
{
	Get(
		"https://api.spotify.com/v1/me/player",
		false,
		onComplete);
}

void SpotifyClient::Play(function<void(JsonValue&)> onComplete, SpotifyErrorHandler onError)
{
	Put(
		PlayerUrl("play"),
		false,
		"",
		onComplete,
		onError);
}

void SpotifyClient::Pause(function<void(JsonValue&)> onComplete, SpotifyErrorHandler onError)
{
	Put(
		PlayerUrl("pause"),
		false,
		"",
		onComplete,
		onError);
}

void SpotifyClient::SetVolume(int volumePercent, function<void(JsonValue&)> onComplete)
{
	Put(
		PlayerUrl("volume", "volume_percent=" + to_string(volumePercent)),
		false,
		"",
		onComplete);
}

void SpotifyClient::Get(string uri, bool debug, function<void(JsonValue&)> onComplete)
{
	Request(uri, "", debug, onComplete, bind(&SpotifyClient::DoGet, this, _1), true, nullptr);
}

// Player commands: Spotify documents 204 but also answers 200 with a body
// that isn't JSON (seen for /next), so their bodies are never parsed
void SpotifyClient::Post(string uri, bool debug, string content, function<void(JsonValue&)> onComplete,
                         SpotifyErrorHandler onError)
{
	Request(uri, content, debug, onComplete, bind(&SpotifyClient::DoPost, this, _1), false, onError);
}

void SpotifyClient::Put(string uri, bool debug, string content, function<void(JsonValue&)> onComplete,
                        SpotifyErrorHandler onError)
{
	Request(uri, content, debug, onComplete, bind(&SpotifyClient::DoPut, this, _1), false, onError);
}

void SpotifyClient::Request(
	string uri, 
	string content, 
	bool debug,
	function<void(JsonValue&)> onComplete,
	function<void(int)> doRequest, 
	bool expectJson,
	SpotifyErrorHandler onError)
{
	SpotifyRequest request;

	request.Uri = uri;
	request.Content = content;
	request.OnComplete = onComplete;
	request.DoRequest = doRequest;
	request.OnError = onError;
	request.Refreshed = false;
	request.WokeDevice = false;
	request.Debug = debug;
	request.ExpectJson = expectJson;

	_requestId++;
	_requests.insert(pair<int, SpotifyRequest>(_requestId, request));
	_httpClient->SetAuthorization("Bearer " + AccessToken);

	doRequest(_requestId);
}

void SpotifyClient::DoGet(int requestId)
{
	SpotifyRequest request = _requests[requestId];
	_httpClient->Get(request.Uri, [this, requestId](HttpResponse& response) {
		HandleHttpResponse(response, requestId);
	});
}

void SpotifyClient::DoPost(int requestId)
{
	SpotifyRequest request = _requests[requestId];
	_httpClient->Post(request.Uri, request.Content, [this, requestId](HttpResponse& response) {
		HandleHttpResponse(response, requestId);
	});
}

void SpotifyClient::DoPut(int requestId)
{
	SpotifyRequest request = _requests[requestId];
	Uri uri(request.Uri);
	_httpClient->Put(uri, request.Content, [this, requestId](HttpResponse& response) {
		HandleHttpResponse(response, requestId);
	});
}

void SpotifyClient::HandleHttpResponse(HttpResponse& response, int requestId)
{
	DebugLog("HandleHttpResp", to_string(response.StatusCode).c_str());
	SpotifyRequest request = _requests[requestId];

	if (response.Success)
	{
		if (request.Debug)
		{
			Util::Debug(response.Content + "\r\r");
		}

		if (response.StatusCode >= 200 &&
			response.StatusCode <= 299)
		{
			// 204, an empty body, or a command (no body needed): nothing to parse
			if (request.ExpectJson && response.StatusCode != 204 && !response.Content.empty())
			{
				JsonAllocator allocator;
				JsonParseStatus status = jsonParse((char*)response.Content.c_str(), _root, allocator);

				if (status == JSON_PARSE_OK)
				{
					request.OnComplete(_root);
				}
				else
				{
					HandleError(request.Uri + ": Error parsing response.");
				}
			}
			else
			{
				JsonValue nullValue = JsonValue();
				request.OnComplete(nullValue);
			}
		}
		else if (response.StatusCode == 401)
		{
			if (!request.Refreshed)
			{
				_requests[requestId].Refreshed = true;
				RefreshAccessToken(requestId);
				return;
			}
			else
			{
				HandleError(request.Uri + ": Authentication error.");
			}
		}
		else if (response.StatusCode == 404 && !request.ExpectJson && !request.WokeDevice &&
		         !DeviceId.empty() && request.Uri.find("/me/player/") != string::npos &&
		         response.Content.find("NO_ACTIVE_DEVICE") != string::npos)
		{
			// A player command, and the device has gone idle: wake it and
			// send the command once more (its result ends this request)
			_requests[requestId].WokeDevice = true;
			if (WakeDevice())
			{
				_httpClient->SetAuthorization("Bearer " + AccessToken);
				request.DoRequest(requestId);
				return;
			}
			if (request.OnError)
				request.OnError(response.StatusCode, response.Content);
			else
				HandleError(HttpErrorText(request.Uri, response.StatusCode, response.Content));
		}
		else if (request.OnError)
		{
			request.OnError(response.StatusCode, response.Content);
		}
		else
		{
			HandleError(HttpErrorText(request.Uri, response.StatusCode, response.Content));
		}
	}
	else
	{
		HandleError(request.Uri + ": " + response.ErrorMsg);
	}

	_requests.erase(requestId);
}

/* Copy a token into a fixed-size prefs field, always NUL-terminated. */
static void CopyToken(char* dest, size_t destSize, const string& token)
{
	memset(dest, 0, destSize);
	strncpy(dest, token.c_str(), destSize - 1);
}

/* Log in with a refresh token obtained once on a modern computer by
 * tools/spotify_auth.py, which delivers it in MacPlayer Preferences.
 * The Mac never sees the Spotify login page; it only trades the refresh
 * token for an access token, exactly as it does when one expires. */
void SpotifyClient::Login(const string& refreshToken, function<void(LoginResponse)> onComplete)
{
	LoginResponse loginResponse;
	loginResponse.Success = false;

	if (refreshToken.empty())
	{
		HandleError("Login: No refresh token.\r\r"
		            "Run tools/spotify_auth.py on another computer and copy the "
		            "MacPlayer Preferences file it writes into the Preferences folder.");
		onComplete(loginResponse);
		return;
	}

	RefreshToken = refreshToken;
	AccessToken = "";
	CopyToken(_prefs->Data.RefreshToken, sizeof(_prefs->Data.RefreshToken), RefreshToken);
	CopyToken(_prefs->Data.AccessToken, sizeof(_prefs->Data.AccessToken), AccessToken);
	_prefs->Save();

	if (RequestAccessToken())
	{
		/* Pre-connect to api.spotify.com so the first real API call finds an
		 * Idle connection.  The TLS handshake inside WarmUp runs on a 512 KB
		 * cooperative thread (RunSslHandshakeOnThread) so no stack overflow. */
		_httpClient->WarmUp("api.spotify.com");
		loginResponse.Success = true;
	}

	onComplete(loginResponse);
}

void SpotifyClient::RefreshAccessToken(int requestId)
{
	if (RequestAccessToken())
	{
		SpotifyRequest request = _requests[requestId];
		request.DoRequest(requestId);
	}
	else
	{
		_requests.erase(requestId);
	}
}

/* Trade RefreshToken for a new access token at accounts.spotify.com.
 * Saves the new access token (and the new refresh token, if Spotify
 * rotated it) to prefs, and always leaves _httpClient ready for
 * api.spotify.com.  Errors are reported via HandleError. */
bool SpotifyClient::RequestAccessToken()
{
	const string authHeader = Keys::SpotifyClientId + ":" + Keys::SpotifyClientSecret;
	string encoded = base64_encode(
		reinterpret_cast<const unsigned char*>(authHeader.c_str()), authHeader.length());

	/* PostForm runs to completion before returning, so handle the response
	 * here rather than inside the callback, keeping the caller's follow-up
	 * requests out of HttpClient's completion path. */
	HttpResponse response;
	_httpClient->SetAuthorization("Basic " + encoded);
	_httpClient->SwitchHost();  // api.spotify.com → accounts.spotify.com
	_httpClient->PostForm(
		"https://accounts.spotify.com/api/token",
		"grant_type=refresh_token&refresh_token=" + Uri::Encode(RefreshToken),
		[&](HttpResponse& r) { response = r; });
	_httpClient->SwitchHost();  // accounts.spotify.com → api.spotify.com

	if (!response.Success)
	{
		HandleError("Token refresh: " + response.ErrorMsg);
		return false;
	}

	if (response.StatusCode < 200 || response.StatusCode > 299)
	{
		/* 400 invalid_grant = token revoked or issued for another Client ID */
		HandleError("Token refresh failed (HTTP " + to_string(response.StatusCode) + ")\r" +
		            response.Content +
		            "\r\rRun tools/spotify_auth.py again, copy the new MacPlayer "
		            "Preferences file into the Preferences folder, then quit "
		            "and reopen MacPlayer.");
		return false;
	}

	JsonAllocator allocator;
	JsonValue root;
	if (jsonParse((char*)response.Content.c_str(), root, allocator) != JSON_PARSE_OK)
	{
		HandleError("Token refresh: Error parsing response.");
		return false;
	}

	/* toString(&ok) instead of toString(): gason asserts on a missing key */
	bool ok = false;
	char* accessToken = root("access_token").toString(&ok);
	if (!ok || !accessToken || !*accessToken)
	{
		HandleError("Token refresh: No access token in response.");
		return false;
	}

	AccessToken = accessToken;
	CopyToken(_prefs->Data.AccessToken, sizeof(_prefs->Data.AccessToken), AccessToken);

	char* refreshToken = root("refresh_token").toString(&ok);
	if (ok && refreshToken && *refreshToken)
	{
		RefreshToken = refreshToken;
		CopyToken(_prefs->Data.RefreshToken, sizeof(_prefs->Data.RefreshToken), RefreshToken);
	}

	_prefs->Save();
	_httpClient->SetAuthorization("Bearer " + AccessToken);
	return true;
}

/* Filter proc for the error-log dialog: enables Cmd-A (select all)
   and Cmd-C (copy) inside the EditText field.                        */
static pascal Boolean ErrorLogFilter(DialogPtr dlg, EventRecord* evt, short* item)
{
	if (evt->what == keyDown || evt->what == autoKey) {
		char key = (char)(evt->message & charCodeMask);
		/* Return, Enter and Esc mean OK, not a line break in the text */
		if (key == '\r' || key == 0x03 || key == 0x1B) {
			*item = 1;
			return true;
		}
		if (evt->modifiers & cmdKey) {
			TEHandle teH = ((DialogRecord*)dlg)->textH;
			if (key == 'a' || key == 'A') {
				if (teH) TESetSelect(0, 32767, teH);
				return true;
			}
			if (key == 'c' || key == 'C') {
				if (teH) { ZeroScrap(); TECopy(teH); TEToScrap(); }
				return true;
			}
		}
	}
	return false;
}

void SpotifyClient::HandleError(const string& errorMsg)
{
	DebugLog("HandleError", errorMsg.c_str());
	DebugLogFlush();

	/* Always copy the full text to the clipboard so the user can paste it */
	ZeroScrap();
	PutScrap((long)errorMsg.size(), 'TEXT', errorMsg.c_str());

	/* Build display text: clipboard notice + message */
	string display = "(Full text copied to clipboard - Cmd-V to paste)\r\r" + errorMsg;

	DialogPtr dlg = GetNewDialog(135, nil, (WindowPtr)-1);
	if (dlg) {
		DrawDialog(dlg);
		/* Focus EditText item (item 2) so textH is initialised */
		SelectDialogItemText(dlg, 2, 0, 0);
		TEHandle teH = ((DialogRecord*)dlg)->textH;
		if (teH) {
			long len = (long)display.size();
			if (len > 30000L) len = 30000L;
			TESetText(display.c_str(), len, teH);
			TESetSelect(0, 0, teH);
			TEUpdate(&(*teH)->viewRect, teH);
		}
		short itemHit = 0;
		ModalFilterUPP filterUPP = NewModalFilterUPP(ErrorLogFilter);
		do {
			ModalDialog(filterUPP, &itemHit);
		} while (itemHit != 1);
		DisposeModalFilterUPP(filterUPP);
		DisposeDialog(dlg);
	} else {
		/* Fallback: show first 255 chars in StopAlert */
		Str255 pMsg;
		size_t len = display.size();
		if (len > 255) len = 255;
		pMsg[0] = (unsigned char)len;
		memcpy(pMsg + 1, display.c_str(), len);
		ParamText(pMsg, nil, nil, nil);
		StopAlert(131, nil);
	}

	InitCursor();
}

/* Album-art cache folder in Preferences. Without it (locked or full disk)
 * MacPlayer still works, it just downloads the art every time, so this
 * only logs. _cacheDirId stays 0 when there is no cache. */
void SpotifyClient::InitCache()
{
	FSSpec dirSpec;
	OSErr err;
	long dirID;

	Str255 dirName = "\pMacPlayer Cache";
	_cacheDirId = 0;

	if (FindFolder(kOnSystemDisk, kPreferencesFolderType, kCreateFolder, &_cacheVRefNum, &dirID) != noErr)
	{
		DebugLog("InitCache", "no Preferences folder, album art not cached");
		return;
	}

	FSMakeFSSpec(_cacheVRefNum, dirID, dirName, &dirSpec);
	Util::GetDirectoryId(_cacheVRefNum, dirID, dirName, &_cacheDirId);

	if (_cacheDirId == 0)
	{
		err = FSpDirCreate(&dirSpec, smSystemScript, &_cacheDirId);
		if (err != noErr)
		{
			_cacheDirId = 0;
			DebugLog("InitCache", ("can't create MacPlayer Cache, error " + to_string(err)).c_str());
		}
	}
}

bool SpotifyClient::SaveImage(const FSSpec* imageSpec, const CoverGray& cover)
{
	OSErr err;
	short file;

	err = FSpCreate(imageSpec, 'MPLY', 'IMGD', 0);
	if (err == noErr || err == dupFNErr)  // dupFNErr: replace a damaged copy
		err = FSpOpenDF(imageSpec, fsRdWrPerm, &file);

	if (err == noErr) 
	{
		struct { unsigned long magic; short width, height; } header =
			{ kCoverMagic, cover.width, cover.height };
		long headerCount = kCoverHeader;
		long coverCount = (long)cover.pixels.size();
		bool ok = SetEOF(file, 0L) == noErr &&
		          FSWrite(file, &headerCount, &header) == noErr && headerCount == kCoverHeader &&
		          FSWrite(file, &coverCount, cover.pixels.data()) == noErr &&
		          coverCount == (long)cover.pixels.size();
		FSClose(file);
		if (ok)
			return true;
	}

	// Never leave a partial file behind: it would look cached
	FSpDelete(imageSpec);
	return false;
}
