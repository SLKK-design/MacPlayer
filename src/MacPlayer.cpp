#include <Events.h>
#include <Resources.h>
#include <Dialogs.h>
#include <Quickdraw.h>
#include <Menus.h>
#include <ToolUtils.h>
#include <Devices.h>
#include <Timer.h>
#include <LowMem.h>
#include <string.h>
#include <stdio.h>
#include "gason.hpp"
#include "MacPlayer.h"
#include "DebugLog.h"
#include "growzone.h"
#include "CoverArt.h"
#include "Util.h"
#include "Keys.h"

DialogPtr _dialog;
double _pollDelayMs = mPollFrequencyMs;  // see PollSoon

// Album cover waiting to be loaded. Downloading and decoding one takes
// seconds on a real 68030, so it happens on an idle pass of the event loop,
// after the library and track info have been drawn (see LoadPendingImage).
static string _pendingImage, _pendingAlbumId;
static string _imageAlbumId;  // album whose cover ActiveCover holds

// ActiveCover dithered for the screen (CoverArt), and the album and size it
// was rendered for: redone only when either changes
static string _coverBits;
static string _coverBitsAlbumId;
static short _coverBitsWidth = 0, _coverBitsHeight = 0;

// Where the window was before full screen
static Point _windowPos;

// DITL 129's item rects, for the normal window (see LayoutPlayer)
static Rect _ditlBox[kPlayerVolumeControl + 1];

// The play/pause cicn TogglePlayButtonIcon put in the play button, so the
// next toggle can free it. Forgotten whenever the dialog items are rebuilt.
static CIconHandle _playIcon = nil;

using namespace gason;

// String value of a JSON field, or "" when it is missing or null.
// gason's toString() asserts on anything that isn't a string, which ends
// the app; Spotify leaves fields such as a device id null.
static string JsonStr(const JsonValue& value)
{
	return value.getTag() == JSON_STRING ? string(value.toString()) : string();
}

// A Spotify Connect device to play on is known (Spotify may not count it
// as active: commands name it, see SpotifyClient::PlayerUrl)
static bool HaveDevice()
{
	return !_spotifyClient.DeviceId.empty();
}

// When the user last chose a device in the Device menu (TickCount). For a
// moment after that Spotify still reports the old device as the one that
// plays; RefreshPlayerState doesn't follow it back.
static unsigned long _deviceChosenAt = 0;

int main()
{
	DebugLogInstallHandlers();
	DebugLogOnFatal(RestoreMenuBar);  // hidden in full screen
	InitToolBox();
	_spotifyClient.InitCache();  // file system calls, so after the Toolbox is up
	MenuInit();
	EventInit();
	InitCustomLDEF();

	ApplyPrefsCredentials();
	ShowMainWindow();
	EventLoop();
	RestoreMenuBar();  // its height is system-wide: the Finder needs it back
	_httpClient.SslFreeAll();  /* release MacTCP stream on clean exit */
}

void InitToolBox()
{
	/* Memory setup as in Claude-86's InitApp: the HttpClient constructor
	   already enlarged the stack (SetApplLimit) before main() */
	MaxApplZone();
	for (int i = 0; i < 10; i++)
		MoreMasters();
	GrowZoneInstall();  /* 32K emergency reserve, released under heap pressure */

	InitGraf(&qd.thePort);
	InitFonts();
	InitWindows();
	InitMenus();
	TEInit();
	InitDialogs(NULL);
	InitCursor();
}

void ShowMainWindow()
{
	_dialog = GetNewDialog(kPlayerDialog, 0, (WindowPtr)-1);
	MacSetPort(_dialog);

	if (_spotifyClient.AccessToken.size() > 0)
	{
		InitPlayer(_dialog);
	}
	else
	{
		TryLogin();
	}
}

// Credentials in MacPlayer Preferences (written by tools/spotify_auth.py)
// override the compiled-in defaults in Keys.cpp
void ApplyPrefsCredentials()
{
	if (_prefs.Data.ClientId[0] != '\0')
		Keys::SpotifyClientId = _prefs.Data.ClientId;
	if (_prefs.Data.ClientSecret[0] != '\0')
		Keys::SpotifyClientSecret = _prefs.Data.ClientSecret;
}

// Log in with the refresh token from MacPlayer Preferences, or show the
// how-to screen when there is none or Spotify rejects it (Login reports why)
void TryLogin()
{
	if (_prefs.Data.RefreshToken[0] == '\0')
	{
		ModeLogin(_dialog);
		return;
	}

	ModeInit(_dialog);  // "Getting ready..." while the token is refreshed
	WaitCursor();
	DebugLog("TryLogin", "refreshing access token");

	_spotifyClient.Login(
		_prefs.Data.RefreshToken,
		[](LoginResponse response)
		{
			DebugLog("TryLogin", response.Success ? "logged in" : "failed");
			if (response.Success)
			{
				InitPlayer(_dialog);
			}
			else
			{
				ModeLogin(_dialog);
				InitCursor();
			}
		});
}

void EventInit()
{
	AEInstallEventHandler(
		kCoreEventClass,
		kAEQuitApplication,
		(AEEventHandlerUPP)Quit,
		0L,
		false);

}

void MenuInit()
{
	Handle menuBar;
	MenuHandle menu;
	ControlHandle control;
	OSErr myErr;
	long feature;
	
	menuBar = GetNewMBar(kPlayerMenubar);
	SetMenuBar(menuBar);
	DisposeHandle(menuBar);

	AppendResMenu(GetMenuHandle(kPlayerMenubar), 'DRVR');

	MacDrawMenuBar();
}

void EventLoop()
{
	EventRecord event;

	while (_run)
	{
		GrowZoneIdle();

		if (WaitNextEvent(everyEvent, &event, 2, NULL))
		{
			DoEvent(&event);
		}
		else if (_uiState == Player)
		{
			// Idle: everything else has been drawn by now, so this is when a
			// waiting album cover loads; polling resumes after it
			if (!_pendingImage.empty())
				LoadPendingImage();
			else
				PollPlayerState();
		}
	}
}

void DoEvent(EventRecord *eventPtr)
{
	char theChar;

	switch (eventPtr->what)
	{
		case keyDown:
		case autoKey:
			theChar = eventPtr->message & charCodeMask;
			if ((eventPtr->modifiers & cmdKey) != 0)
				HandleMenuChoice(MenuKey(theChar));
			else if (_fullScreen && theChar == 0x1B)
				ChooseFullScreen(false);  // Esc
			else if (_uiState == Login && (theChar == '\r' || theChar == 0x03))
				HandleLoginContent(kPlayerLoginButton);  // Return / Enter = Try Again
			break;

		case mouseDown:
			HandleMouseDown(eventPtr);
			break;

		case updateEvt:
			HandleUpdate(eventPtr);
			break;

		case kHighLevelEvent:
			AEProcessAppleEvent(eventPtr);
			break;

		case osEvt:
			// Another application comes to the front: it needs the menu bar
			if (((eventPtr->message >> 24) & 0xFF) == suspendResumeMessage &&
			    (eventPtr->message & resumeFlag) == 0)
				SetFullScreen(false);
			break;
	}
}

void HandleMouseDown(EventRecord *eventPtr)
{
	WindowPtr window;
	short int part;
	long menuChoice;

	part = FindWindow(eventPtr->where, &window);

	switch (part)
	{
		case inMenuBar:
			menuChoice = MenuSelect(eventPtr->where);
			HandleMenuChoice(menuChoice);
			break;

		case inContent:
			if (window == FrontWindow())
				HandleInContent(eventPtr);
			break;

		case inDrag:
			if (window == FrontWindow())
				DragWindow(window, eventPtr->where, &qd.screenBits.bounds);
			break;

		case inGoAway:
			if (TrackGoAway(window, eventPtr->where))
			{
				ExitApp();
			}
			break;
	}
}

void HandleMenuChoice(long menuChoice)
{
	Str255 str;
	WindowRef w;
	short menuID = menuChoice >> 16;
	short menuItem = menuChoice & 0xFFFF;
	if (menuID == kMenuApple)
	{
		if (menuItem == kItemAbout)
			ShowAboutBox();
		else
		{
			GetMenuItemText(MacGetMenu(128), menuItem, str);
			OpenDeskAcc(str);
		}
	}
	else if (menuID == kMenuPlayer)
	{
		switch (menuItem)
		{
			case kItemFullScreen:
				ChooseFullScreen(!_fullScreen);
				break;
			case kItemQuit:
				ExitApp();
				break;
		}
	}
	else if (menuID == kMenuDevices)
	{
		if (menuItem <= _devices.size())
		{
			ActivateDevice(menuItem);
		}
		else if (_uiState == Player)
		{
			// Refresh menu item (nothing to refresh before login)
			GetDevices();
		}
	}

	HiliteMenu(0);
}

// Device menu: play on this device from now on. Playback moves over only
// if there is some, and stays playing or paused; on a fresh start Spotify
// has none to move, and the device is simply the one Play and tracks go to.
void ActivateDevice(int index)
{
	_spotifyClient.DeviceId = _devices[index - 1].id;
	_deviceChosenAt = TickCount();
	MarkDevice();
	if (_playerState.isPlaying || !_currentTrack.uri.empty())
		_spotifyClient.ActivateDevice(_spotifyClient.DeviceId,
			[](JsonValue& root) { PollSoon(1500); });  // show what it plays
}

// Whether the Device menu lists this device
static bool DeviceListed(const string& id)
{
	for (size_t i = 0; i < _devices.size(); i++)
		if (_devices[i].id == id)
			return true;
	return false;
}

// Check the Device menu item of the device MacPlayer plays on
void MarkDevice()
{
	MenuHandle deviceMenu = GetMenuHandle(kMenuDevices);
	for (int i = 1; i <= (int)_devices.size(); i++)
		SetItemMark(deviceMenu, i, _devices[i - 1].id == _spotifyClient.DeviceId
		                           ? (CharParameter)checkMark : (CharParameter)noMark);
}

void ShowAboutBox()
{
	DialogRef aboutDialog = GetNewDialog(kAboutDialog, 0, (WindowPtr)-1);

	MacSetPort(aboutDialog);
	UpdateDialog(aboutDialog, aboutDialog->visRgn);

	while (!Button())
		;
	while (Button())
		;

	FlushEvents(everyEvent, 0);
	DisposeWindow(aboutDialog);
	MacSetPort(_dialog);
}

void DisposePlayerLists()
{
	if (_navList)   { LDispose(_navList);   _navList   = 0; }
	if (_trackList) { LDispose(_trackList); _trackList = 0; }
}

void ExitApp()
{
	// The music keeps playing: MacPlayer is only the remote
	_run = false;
}

void HandleInContent(EventRecord *eventPtr)
{
	WindowPtr windowPtr;
	short item;

	if (DialogSelect(eventPtr, &_dialog, &item))
	{
		switch (_uiState)
		{
			case Login:
				HandleLoginContent(item);
				break;

			case Player:
				HandlePlayerContent(eventPtr, item);
				break;
		}
	}
}

void HandleLoginContent(short item)
{
	switch (item)
	{
		case kPlayerLoginButton:
			// Try Again: pick up a MacPlayer Preferences file copied in
			// since launch
			_prefs.Load();
			ApplyPrefsCredentials();

			if (_prefs.Data.RefreshToken[0] == '\0')
			{
				_spotifyClient.HandleError(
					"No login found in System Folder:Preferences:MacPlayer Preferences.\r\r"
					"Run tools/spotify_auth.py on another computer and copy the "
					"MacPlayer Preferences file it writes into the Preferences folder.");
				break;
			}

			TryLogin();
			break;
	}
}

void HandlePlayerContent(EventRecord *eventPtr, short item)
{
	Point pt;

	switch (item)
	{
		case kPlayerNavList:
		{
			// Nav List
			GetMouse(&pt);
			LClick(pt, 0, _navList);
			Cell cell = LLastClick(_navList);
			short cellIndex = cell.v;

			// Clicks on the scroll bar just scroll; LLastClick still
			// returns the previous row for them
			if (MacPtInRect(pt, &(**_navList).rView) &&
			    cellIndex >= 0 && cellIndex < (short)_playlists.size())
			{
				GetPlaylistTracks(_playlists[cellIndex].uri, _playlists[cellIndex].id);
			}
			break;
		}

		case kPlayerTrackList:
		{
			// Track List
			GetMouse(&pt);
			bool dblClick = LClick(pt, 0, _trackList);
			Cell cell = LLastClick(_trackList);
			short cellIndex = cell.v;

			// Only a double-click on an actual row plays it: an empty list,
			// the area below the last row or the scroll bar give no track
			if (dblClick && MacPtInRect(pt, &(**_trackList).rView) &&
			    cellIndex >= 0 && cellIndex < (short)_tracks.size())
			{
				if (HaveDevice())
				{
					_currentTrack = _tracks[cellIndex];
					PlayTrack();
				}
				else
				{
					NoDevicesError();
				}
			}

			break;
		}
			
		case kPlayerPlayButton:
		{
			if (HaveDevice())
			{
				// Requests run to completion: flip the button only once Spotify
				// has accepted the command
				bool wantPlaying = !_playerState.isPlaying;
				bool accepted = false;
				auto onDone = [&accepted](JsonValue& root) { accepted = true; };

				// A refusal (403 "Restriction violated") usually means the device
				// was already playing/paused, changed elsewhere since the last poll:
				// re-read the real state and only complain if it still differs.
				// With nothing played yet there is nothing to resume: start the
				// open playlist instead.
				auto onError = [wantPlaying](int statusCode, const string& body)
				{
					if (wantPlaying && _currentTrack.uri.empty())
					{
						PlayFromTrackList();
						return;
					}
					RefreshPlayerState();
					if (_playerState.isPlaying != wantPlaying)
						_spotifyClient.HandleError(
							string(wantPlaying ? "Play" : "Pause") + " failed: HTTP " +
							to_string(statusCode) + "\r" + SpotifyClient::ErrorMessage(body));
				};

				if (wantPlaying)
					_spotifyClient.Play(onDone, onError);
				else
					_spotifyClient.Pause(onDone, onError);

				if (accepted)
				{
					_playerState.isPlaying = wantPlaying;
					TogglePlayButtonIcon();
				}
			}
			else
			{
				NoDevicesError();
			}
			break;
		}

		case kPlayerPrevTrack:
		{
			if (HaveDevice())
			{
				TrackChanging();
				_spotifyClient.PreviousTrack(
					[=](JsonValue& root)
					{
						// Asked at once, Spotify can still report the old track:
						// poll again shortly. Clearing the uri makes that poll
						// redraw whatever it finds.
						_currentTrack.uri = "";
						PollSoon(1500);
					});
			}
			else
			{
				NoDevicesError();
			}
			break;
		}

		case kPlayerNextTrack:
		{
			if (HaveDevice())
			{
				TrackChanging();
				_spotifyClient.NextTrack(
					[=](JsonValue& root)
					{
						// Asked at once, Spotify can still report the old track:
						// poll again shortly. Clearing the uri makes that poll
						// redraw whatever it finds.
						_currentTrack.uri = "";
						PollSoon(1500);
					});
			}
			else
			{
				NoDevicesError();
			}
			break;
		}

		case kPlayerVolumeControl:
		{
			pt = eventPtr->where;
			GlobalToLocal(&pt);
			ControlHandle cHdl;
			if (FindControl(pt, _dialog, &cHdl) > 0)
			{
				if (TrackControl(cHdl, pt, nil) > 0)
				{
					_currentVolume = GetControlValue(cHdl);
					_spotifyClient.SetVolume(_currentVolume, [](JsonValue& root) {});
					// Let Spotify apply it before a poll reads the volume back
					PollSoon(mPollFrequencyMs);
				}
			}

			break;
		}
	}
}

void PlayTrack()
{
	// The play button and now-playing change once Spotify plays the track
	_spotifyClient.PlayTrack(
		_currentContext,
		_currentTrack.uri, 
		[=](JsonValue& root)
		{
			_playerState.isPlaying = true;
			TogglePlayButtonIcon();
			DrawTrackName();
			ViewNowPlaying();
		});
}

void ViewNowPlaying()
{
	_viewNowPlaying = true;
	HideDialogItem(_dialog, kPlayerTrackLabel);
	HideDialogItem(_dialog, kPlayerArtistLabel);
	HideDialogItem(_dialog, kPlayerTrackList);
	HideControl((**_trackList).vScroll);

	string image = _currentTrack.image;

	if (image == "")
	{
		// e.g. a podcast episode: no cover
		_pendingImage.clear();
		_imageAlbumId.clear();
		_spotifyClient.ActiveCover.clear();
		DrawTrackImage();
	}
	else if (_currentTrack.albumId == _imageAlbumId && !_spotifyClient.ActiveCover.empty())
	{
		// Same album as the cover already loaded: nothing to fetch
		_pendingImage.clear();
		DrawTrackImage();
	}
	else
	{
		// Load it on the next idle pass; show the placeholder meanwhile
		_pendingImage = image;
		_pendingAlbumId = _currentTrack.albumId;
		_imageAlbumId.clear();
		_spotifyClient.ActiveCover.clear();
		DrawPlaceholderImage();
	}
}

// Idle-time half of ViewNowPlaying: read the cover from the cache or
// download it, then draw it if it is still the one on screen
void LoadPendingImage()
{
	string image = _pendingImage;
	string albumId = _pendingAlbumId;
	_pendingImage.clear();

	// The user opened a playlist meanwhile: load it when now-playing returns
	if (!_viewNowPlaying)
		return;

	_spotifyClient.GetImage(image, albumId,
		[=]()
		{
			if (!_spotifyClient.ActiveCover.empty())
				_imageAlbumId = albumId;
			if (_uiState == Player && _viewNowPlaying && _currentTrack.albumId == albumId)
				DrawTrackImage();
		});
}

void DrawPlaceholderImage()
{
	MacSetPort(_dialog);  // an error dialog just before leaves no port set

	Rect r = _trackImageRect;
	PicHandle pic = GetPicture(kPlaceholderImage);
	if (pic == nil)
	{
		EraseRect(&r);  // at least no old cover
		return;
	}

	// At its own size, centred in the cover's place, which full screen makes
	// bigger: scaled up, its dither pattern turns into moiré. Shrunk only to
	// fit a smaller place.
	Rect frame = (**pic).picFrame;
	short w = r.right - r.left, h = r.bottom - r.top;
	short pw = frame.right - frame.left, ph = frame.bottom - frame.top;
	if (pw > w)
	{
		ph = ph * w / pw;
		pw = w;
	}
	if (ph > h)
	{
		pw = pw * h / ph;
		ph = h;
	}
	if (pw != w || ph != h)
		EraseRect(&r);
	short left = r.left + (w - pw) / 2, top = r.top + (h - ph) / 2;
	MacSetRect(&frame, left, top, left + pw, top + ph);

	DrawPicture(pic, &frame);
}

void TrackChanging()
{
	if (_viewNowPlaying)
	{
		DrawPlaceholderImage();
	}
}

void DrawTrackImage()
{
	MacSetPort(_dialog);

	if (_spotifyClient.ActiveCover.empty())
	{
		if (!_pendingImage.empty())
			DrawPlaceholderImage();  // cover still waiting to load
		else
			EraseRect(&_trackImageRect);
		return;
	}

	// Dithered once per cover and size (CoverArt), so every update event
	// after that is a single CopyBits
	if (_coverBits.empty() || _coverBitsAlbumId != _imageAlbumId ||
	    _coverBitsWidth != _coverWidth || _coverBitsHeight != _coverHeight)
	{
		long t0 = TickCount();
		_coverBits = RenderCover(_spotifyClient.ActiveCover, _coverWidth, _coverHeight);
		_coverBitsAlbumId = _imageAlbumId;
		_coverBitsWidth = _coverWidth;
		_coverBitsHeight = _coverHeight;

		char timing[48];
		snprintf(timing, sizeof(timing), "%dx%d px: render %ld ticks",
		         _coverWidth, _coverHeight, TickCount() - t0);
		DebugLog("Cover", timing);
	}
	DrawCover(_coverBits, _coverWidth, _coverHeight, _trackImageRect.left, _trackImageRect.top);
}

void DrawTrackName()
{
	// Bottom left, next to the controls row
	short bottom = _dialog->portRect.bottom;
	Rect trackRect;
	MacSetRect(&trackRect, 10, bottom - 34, 130, bottom - 6);
	EraseRect(&trackRect);

	short length;

	string name = Util::Utf8ToMacRoman(_currentTrack.name);
	const char* cName = name.c_str();
	length = strlen(cName);
	TruncText(120, (Ptr)cName, &length, truncEnd);

	unsigned char* pName = Util::CtoPStr((char*)cName);
	pName[0] = length;

	MoveTo(10, bottom - 25);
	DrawString(pName);

	string artist = Util::Utf8ToMacRoman(_currentTrack.artist);
	const char* cArtist = artist.c_str();
	length = strlen(cArtist);
	TruncText(120, (Ptr)cArtist, &length, truncEnd);

	unsigned char* pArtist = Util::CtoPStr((char*)cArtist);
	pArtist[0] = length;

	MoveTo(10, bottom - 13);
	DrawString(pArtist);
}

void GetRecentTracks()
{
	_spotifyClient.GetRecentTracks(
		[=](JsonValue& root)
		{
			PopulateTrackList(root);
		});
}

void GetPlaylistTracks(const string& uri, const string& playlistId)
{
	_spotifyClient.GetPlaylistTracks(
		playlistId,
		[=](JsonValue& root)
		{
			_currentContext = uri;
			PopulateTrackList(root);
		});
}

void PopulateTrackList(JsonValue& root)
{
	JsonValue items = root("items");
	JsonValue track;
	Cell cell;

	int rowNum = (**_trackList).dataBounds.bottom;

	_viewNowPlaying = false;

	ShowDialogItem(_dialog, kPlayerTrackLabel);
	ShowDialogItem(_dialog, kPlayerArtistLabel);
	ShowDialogItem(_dialog, kPlayerTrackList);
	ShowControl((**_trackList).vScroll);
	
	EraseRect(&(**_trackList).rView);
	EraseRect(&_trackImageRect);

	LSetDrawingMode(false, _trackList);

	JsonIterator it = gason::begin(items);

	_tracks.clear();
	LDelRow(0, 0, _trackList);

	while (it.isValid())
	{
		JsonValue father = it->value;
		ListRow row;
		Track trackObj;

		// Playlist items use "item" since Feb 2026; recently-played still "track"
		track = father("item");
		if (track.getTag() != JSON_OBJECT)
			track = father("track");
		if (track.getTag() != JSON_OBJECT)  // removed or unavailable entry
		{
			it++;
			continue;
		}

		trackObj = GetTrackObject(track);
		_tracks.push_back(trackObj);

		row.CellCount = 2;

		string macTrackName = Util::Utf8ToMacRoman(trackObj.name);
		size_t nameLen = macTrackName.size();
		if (nameLen > 255) nameLen = 255;
		row.Cells[0].Content[0] = (unsigned char)nameLen;
		memcpy(row.Cells[0].Content + 1, macTrackName.c_str(), nameLen);
		string macArtist = Util::Utf8ToMacRoman(trackObj.artist);
		size_t artistLen = macArtist.size();
		if (artistLen > 255) artistLen = 255;
		row.Cells[1].Content[0] = (unsigned char)artistLen;
		memcpy(row.Cells[1].Content + 1, macArtist.c_str(), artistLen);

		rowNum = LAddRow(1, rowNum, _trackList);
		SetPt(&cell, 0, rowNum);
		LSetCell(&row, sizeof(ListRow), cell, _trackList);

		rowNum = rowNum + 1;
		it++;
	}

	LSetDrawingMode(true, _trackList);
	InvalRect(&(**_trackList).rView);

	if ((**_trackList).vScroll != NULL)
		InvalRect(&(**(**_trackList).vScroll).contrlRect);

	UpdateDialog(_dialog, _dialog->visRgn);
}

Track GetTrackObject(JsonValue& track)
{
	Track trackObj;
	trackObj.name = JsonStr(track("name"));
	trackObj.uri = JsonStr(track("uri"));

	// Podcast episodes have no artists array
	JsonValue artists = track("artists");
	if (artists.getTag() == JSON_ARRAY && artists[0].getTag() == JSON_OBJECT)
		trackObj.artist = JsonStr(artists[0]("name"));

	SetTrackImage(track, trackObj);

	return trackObj;
}

void SetTrackImage(JsonValue& track, Track& trackObj)
{
	JsonValue album = track.child("album");

	if (album.getTag() == JSON_OBJECT)
	{
		JsonValue images = album.child("images");

		if (images.getTag() == JSON_ARRAY)
		{
			JsonValue image = images.at(1); // 300x300 — better quality for gray-pattern display

			if (image == JSON_OBJECT)
			{
				trackObj.albumId = JsonStr(album("id"));
				trackObj.image = JsonStr(image.child("url"));
			}
		}
	}
}

void WaitCursor()
{
	CursHandle theCursor = MacGetCursor(watchCursor);
	MacSetCursor(*theCursor);
}

void ModeInit(DialogPtr dialog)
{
	_uiState = Init;
	_playIcon = nil;  // dialog items are rebuilt below

	// Its items are laid out for the normal window
	SetFullScreen(false);
	DisableItem(GetMenuHandle(kMenuPlayer), kItemFullScreen);

	MacSetPort(dialog);
	EraseRgn(dialog->visRgn);
	ForeColor(whiteColor);

	ShortenDITL(dialog, CountDITL(dialog));
	Handle ditl = GetResource('DITL', kPlayerStartupDITL);
	AppendDITL(dialog, ditl, overlayDITL);
	ReleaseResource(ditl);

	UpdateDialog(dialog, dialog->visRgn);
}

void ModeLogin(DialogPtr dialog)
{
	_uiState = Login;
	_playIcon = nil;  // dialog items are rebuilt below

	// Its items are laid out for the normal window
	SetFullScreen(false);
	DisableItem(GetMenuHandle(kMenuPlayer), kItemFullScreen);

	MacSetPort(dialog);
	EraseRgn(dialog->visRgn);
	ForeColor(whiteColor);

	ShortenDITL(dialog, CountDITL(dialog));
	Handle ditl = GetResource('DITL', kPlayerLoginDITL);
	AppendDITL(dialog, ditl, overlayDITL);
	ReleaseResource(ditl);

	UpdateDialog(dialog, dialog->visRgn);
}

void ModePlayer(DialogPtr dialog)
{
	DialogItemType type;
	Handle itemH;
	Rect box;

	_uiState = Player;
	_playIcon = nil;  // dialog items are rebuilt below
	DisposePlayerLists();

	MacSetPort(dialog);
	EraseRgn(dialog->visRgn);

	ShortenDITL(dialog, CountDITL(dialog));
	Handle ditl = GetResource('DITL', kPlayerMainDITL);
	AppendDITL(dialog, ditl, overlayDITL);
	ReleaseResource(ditl);

	for (short item = 1; item <= kPlayerVolumeControl; item++)
	{
		GetDialogItem(dialog, item, &type, &itemH, &box);
		_ditlBox[item] = box;
	}

	HideDialogItem(dialog, kPlayerTrackLabel);
	HideDialogItem(dialog, kPlayerArtistLabel);

	// Init nav list
	GetDialogItem(dialog, kPlayerNavList, &type, &itemH, &box);
	_navList = CreateList(dialog, box, 1, 128, 0, 0);
	PopulateNavList(_navList);

	// Init track list
	GetDialogItem(dialog, kPlayerTrackList, &type, &itemH, &box);
	_trackList = CreateList(dialog, box, 1, 128, 0, 0);

	LayoutPlayer();
	EnableItem(GetMenuHandle(kMenuPlayer), kItemFullScreen);
	if (_prefs.Data.FullScreen)
		SetFullScreen(true);  // as it was left last time

	UpdateDialog(dialog, dialog->visRgn);

	// Draw the library now rather than on the next update event: the rest of
	// startup (devices, player state) runs first and would keep it blank
	LUpdate(dialog->visRgn, _navList);
	LUpdate(dialog->visRgn, _trackList);
}

ListHandle CreateList(
	WindowPtr windowPtr,
	Rect rect,
	int columnsInList,
	int ldef,
	short cellWidth,
	short cellHeight)
{
	bool doDraw = true;
	bool noGrow = false;
	bool includeScrollBar = true;
	int scrollBarWidth = 15;
	Rect myDataBounds;
	Point myCellSize;

	MacSetRect(&myDataBounds, 0, 0, columnsInList, 0);
	SetPt(&myCellSize, cellWidth, cellHeight);
	rect.right = rect.right - scrollBarWidth;

	return
		LNew(&rect, &myDataBounds, myCellSize, ldef, windowPtr,
			doDraw, noGrow, !includeScrollBar,
			includeScrollBar);
}

void PopulateNavList(ListHandle list)
{
	LSetDrawingMode(false, list);

	int rowNum = (**list).dataBounds.bottom;
	Cell cell;
	ListRow row;

	for (const auto& playlist : _playlists)
	{
		row.CellCount = 1;

		rowNum = LAddRow(1, rowNum, list);
		SetPt(&cell, 0, rowNum);

		string macName = Util::Utf8ToMacRoman(playlist.name);
		size_t len = macName.size();
		if (len > 255) len = 255;
		row.Cells[0].Content[0] = (unsigned char)len;
		memcpy(row.Cells[0].Content + 1, macName.c_str(), len);

		LSetCell(&row, sizeof(ListRow), cell, list);
		rowNum = rowNum + 1;
	}

	LSetDrawingMode(true, list);
}

void InitPlayer(DialogPtr dialog)
{
	ModeInit(dialog);
	WaitCursor();
	Microseconds(&_lastPollTime);

	_currentTrack = Track();
	_playerState.isPlaying = false;
	_pendingImage.clear();
	_imageAlbumId.clear();

	// Requests run to completion, so after each call we know whether it
	// worked. A failure has already shown Spotify's reason (e.g. Premium
	// required); go back to the login screen, whose Try Again starts over.
	bool gotUser = false;
	_userId.clear();
	_spotifyClient.GetCurrentUser(
		[&gotUser](JsonValue& root)
		{
			gotUser = true;
			_userId = JsonStr(root("id"));
		});

	if (gotUser)
		GetPlaylists(dialog);  // switches to the player screen on success

	if (_uiState != Player)
	{
		ModeLogin(dialog);
		InitCursor();
		return;
	}

	GetDevices();

	// Play button and now-playing come from the device on the first idle
	// pass, once the library is on screen; its cover loads after that
	PollSoon(0);
}

void GetDevices()
{
	_spotifyClient.GetDevices(
		[=](JsonValue& root)
		{
			// Rebuilt from scratch, like the menu: menu item N is _devices[N-1]
			_devices.clear();
			string chosen = _spotifyClient.DeviceId;
			_spotifyClient.DeviceId.clear();

			MenuHandle deviceMenu = GetMenuHandle(130);
			int itemCount = CountMItems(deviceMenu);

			// Clear existing menu items
			while (itemCount > 0) {
				DeleteMenuItem(deviceMenu, 1);
				itemCount--;
			}

			JsonValue devices = root("devices");

			JsonIterator it = gason::begin(devices);
			while (it.isValid())
			{
				JsonValue device = it->value;

				Device deviceObj;
				deviceObj.name = JsonStr(device("name"));
				deviceObj.id = JsonStr(device("id"));
				deviceObj.active = device("is_active").getTag() == JSON_TRUE;

				// A null id marks a device the Web API can't control
				if (device.getTag() != JSON_OBJECT || deviceObj.id.empty())
				{
					it++;
					continue;
				}

				_devices.push_back(deviceObj);

				AppendMenu(deviceMenu, "\p ");
				itemCount++;
				{
					Str255 devName;
					string macName = Util::Utf8ToMacRoman(deviceObj.name);
					size_t devLen = macName.size();
					if (devLen > 255) devLen = 255;
					devName[0] = (unsigned char)devLen;
					memcpy(devName + 1, macName.c_str(), devLen);
					SetMenuItemText(deviceMenu, itemCount, devName);
				}

				if (deviceObj.active)
					_spotifyClient.DeviceId = deviceObj.id;

				it++;
			}

			// Play on the active device. With none active (Spotify open but
			// idle, or nothing played yet): the one chosen before if it is
			// still there, else the only one there is. Otherwise the Device
			// menu asks.
			if (_spotifyClient.DeviceId.empty())
			{
				for (size_t i = 0; i < _devices.size(); i++)
					if (_devices[i].id == chosen)
						_spotifyClient.DeviceId = chosen;
				if (_spotifyClient.DeviceId.empty() && _devices.size() == 1)
					_spotifyClient.DeviceId = _devices[0].id;
			}
			MarkDevice();

			// Items show when the menu opens: the bar needs no redraw, which
			// would draw it over the cover in full screen
			AppendMenu(deviceMenu, "\p ");
			itemCount++;
			SetMenuItemText(deviceMenu, itemCount, "\pRefresh...");
		});
}

void NoDevicesError()
{
	if (_devices.empty())
		_spotifyClient.HandleError(
			"No Spotify Connect devices found.\r\r"
			"Open Spotify on a phone, computer or speaker that is logged in to "
			"your account, then choose Device > Refresh...");
	else
		_spotifyClient.HandleError("Choose the device to play on in the Device menu.");
}

// Play with nothing to resume (nothing has played yet): start the selected
// track of the open playlist, else its first one. With no playlist open,
// say how to start.
void PlayFromTrackList()
{
	if (_tracks.empty())
	{
		_spotifyClient.HandleError(
			"Nothing to resume yet.\r\r"
			"Choose a playlist on the left, then double-click a track to play it.");
		return;
	}

	Cell cell = { 0, 0 };
	short row = 0;
	if (_trackList != 0 && LGetSelect(true, &cell, _trackList) &&
	    cell.v >= 0 && cell.v < (short)_tracks.size())
		row = cell.v;

	_currentTrack = _tracks[row];
	PlayTrack();
}

void GetPlaylists(DialogPtr dialog)
{
	_spotifyClient.GetPlaylists(
		[=](JsonValue& root)
		{
			_playlists.clear();
			JsonValue playlists = root("items");

			JsonIterator it = gason::begin(playlists);
			while (it.isValid())
			{
				JsonValue playlistNode = it->value;

				Playlist playlist;
				playlist.name = JsonStr(playlistNode("name"));
				playlist.id = JsonStr(playlistNode("id"));
				playlist.uri = JsonStr(playlistNode("uri"));

				// Since Feb 2026 Spotify only serves the tracks of playlists the
				// user owns or collaborates on; leave out ones they only follow
				string ownerId = JsonStr(playlistNode("owner")("id"));
				bool collaborative = playlistNode("collaborative").getTag() == JSON_TRUE;
				bool usable = _userId.empty() || ownerId == _userId || collaborative;

				if (playlistNode.getTag() == JSON_OBJECT && !playlist.id.empty() && usable)
					_playlists.push_back(playlist);
				it++;
			}

			ModePlayer(dialog);
			InitCursor();
			// Reset poll timer now that startup blocking is done, so the
			// first poll fires 30 s from here rather than immediately.
			Microseconds(&_lastPollTime);
		});
}

void PollPlayerState()
{
	UnsignedWide now;
	Microseconds(&now);

	double diffMs = (Util::MicrosecondToDouble(&now) - Util::MicrosecondToDouble(&_lastPollTime)) / 1000;

	if (diffMs > _pollDelayMs)
	{
		_lastPollTime = now;
		_pollDelayMs = mPollFrequencyMs;
		RefreshPlayerState();
	}
}

// Poll again after delayMs instead of the usual interval, e.g. to give
// Spotify a moment to apply a command before reading the state back
void PollSoon(double delayMs)
{
	Microseconds(&_lastPollTime);
	_pollDelayMs = delayMs;
}

// Ask Spotify what is playing and bring the play button, volume and
// now-playing view in line with it
void RefreshPlayerState()
{
	_spotifyClient.GetPlayerState(
		[=](JsonValue& root)
		{
			bool isPlaying = root("is_playing").getTag() == JSON_TRUE;

			if (isPlaying != _playerState.isPlaying)
			{
				_playerState.isPlaying = isPlaying;
				TogglePlayButtonIcon();
			}

			JsonValue device = root("device");
			bool unlisted = false;
			if (device.getTag() == JSON_OBJECT)
			{
				// Follow the device that plays, also when it was started or
				// moved there from another Spotify app: MacPlayer's commands
				// name their device (SpotifyClient::PlayerUrl)
				string id = JsonStr(device("id"));
				if (!id.empty() && id != _spotifyClient.DeviceId &&
				    device("is_active").getTag() == JSON_TRUE &&
				    TickCount() - _deviceChosenAt > 600)
				{
					_spotifyClient.DeviceId = id;
					MarkDevice();
					unlisted = !DeviceListed(id);
				}

				JsonValue volumePercent = device("volume_percent");

				if (volumePercent.getTag() == JSON_NUMBER)
				{
					int volume = volumePercent.toInt();
					if (volume != _currentVolume)
					{
						DialogItemType type;
						Handle item;
						Rect box;
						GetDialogItem(_dialog, kPlayerVolumeControl, &type, &item, &box);

						ControlHandle volumeControl = (ControlHandle)item;
						SetControlValue(volumeControl, volume);
						_currentVolume = volume;
					}
				}
			}

			// While playing, or when the track is unknown (startup, right after
			// Next/Previous): show what the device has, even if paused
			if (isPlaying || _currentTrack.uri.empty())
			{
				JsonValue track = root("item");

				if (track.getTag() == JSON_OBJECT)
				{
					JsonValue uri = track("uri");

					if (uri.getTag() == JSON_STRING)
					{
						string trackUri = JsonStr(uri);

						if (trackUri != _currentTrack.uri)
						{
							_currentTrack = GetTrackObject(track);
							DrawTrackName();
							ViewNowPlaying();
						}
						else if (!_viewNowPlaying)
						{
							// Same track but now-playing view not shown yet — show it
							DrawTrackName();
							ViewNowPlaying();
						}
					}
				}
			}

			// Not in the Device menu (switched on after the list was read):
			// read the list again. Last, as root's strings live in the
			// response buffer the next request reuses.
			if (unlisted)
				GetDevices();
		});
}

void TogglePlayButtonIcon()
{
	DialogItemType itemType;
	Handle tempHandle;
	Rect itemRect;

	int iconId = _playerState.isPlaying ? kPlayerPauseIcon : kPlayerPlayIcon;
	CIconHandle icon = GetCIcon(iconId);

	GetDialogItem(_dialog, kPlayerPlayButton, &itemType, &tempHandle, &itemRect);
	SetDialogItem(_dialog, kPlayerPlayButton, itemType, (Handle)icon, &itemRect);

	// Free the icon this function put there last time. The one the Dialog
	// Manager loaded with the item is left alone, it owns that one.
	if (_playIcon != nil && (Handle)_playIcon == tempHandle)
		DisposeCIcon(_playIcon);
	_playIcon = icon;

	InvalRect(&itemRect);
	UpdateDialog(_dialog, _dialog->visRgn);
}

void HandleUpdate(EventRecord *eventPtr)
{
	if (_dialog == FrontWindow())
	{
		BeginUpdate(_dialog);

		if (_navList != 0)
		{
			LUpdate(_dialog->visRgn, _navList);
		}

		if (!_viewNowPlaying)
		{
			if (_trackList != 0)
			{
				LUpdate(_dialog->visRgn, _trackList);
			}
		}

		if (_uiState == Player)
			DrawTrackName();
		UpdateDialog(_dialog, _dialog->visRgn);

		// Draw AFTER UpdateDialog so dialog items don't overwrite it
		if (_uiState == Player && _viewNowPlaying)
		{
			DrawTrackImage();
		}

		EndUpdate(_dialog);
	}
}

// The menu bar's height while full screen hides it, else 0
static short _hiddenMenuBar = 0;

static RgnHandle MenuBarRgn(short height)
{
	Rect bar = qd.screenBits.bounds;
	bar.bottom = bar.top + height;
	RgnHandle rgn = NewRgn();
	RectRgn(rgn, &bar);
	return rgn;
}

// Take over the whole screen, as games do: with no menu bar height, clicks
// at the top go to the window, and with the bar's strip added to the
// desktop region, windows may cover it. Both are system-wide, so the bar
// comes back whenever MacPlayer leaves the front or quits (RestoreMenuBar).
static void HideMenuBarStrip()
{
	if (_hiddenMenuBar != 0)
		return;
	_hiddenMenuBar = GetMBarHeight();
	RgnHandle bar = MenuBarRgn(_hiddenMenuBar);
	UnionRgn(LMGetGrayRgn(), bar, LMGetGrayRgn());
	DisposeRgn(bar);
	LMSetMBarHeight(0);
}

void RestoreMenuBar()
{
	if (_hiddenMenuBar == 0)
		return;
	RgnHandle bar = MenuBarRgn(_hiddenMenuBar);
	DiffRgn(LMGetGrayRgn(), bar, LMGetGrayRgn());
	DisposeRgn(bar);
	LMSetMBarHeight(_hiddenMenuBar);
	_hiddenMenuBar = 0;
	MacDrawMenuBar();
}

// Full screen: no menu bar, and the window covers the whole screen, its
// title bar and frame just off the edges. The menus still answer their
// keys (cmd-F, cmd-Q). The player is laid out for the larger window.
void SetFullScreen(bool on)
{
	if (on == _fullScreen)
		return;
	_fullScreen = on;
	MacSetPort(_dialog);

	if (on)
	{
		Point pos = { 0, 0 };
		LocalToGlobal(&pos);
		_windowPos = pos;

		HideMenuBarStrip();
		Rect screen = qd.screenBits.bounds;
		MoveWindow(_dialog, screen.left, screen.top, false);
		SizeWindow(_dialog, screen.right - screen.left, screen.bottom - screen.top, true);
	}
	else
	{
		// Window back first: the strip it leaves is repainted as desktop,
		// then the menu bar is drawn over it
		SizeWindow(_dialog, kWindowWidth, kWindowHeight, true);
		MoveWindow(_dialog, _windowPos.h, _windowPos.v, false);
		RestoreMenuBar();
	}

	CheckItem(GetMenuHandle(kMenuPlayer), kItemFullScreen, on);

	if (_uiState == Player)
		LayoutPlayer();
	EraseRect(&_dialog->portRect);
	InvalRect(&_dialog->portRect);
}

// Full screen on or off by the user's choice (menu, Esc), remembered in
// MacPlayer Preferences for the next start. Leaving it because another
// application comes to the front is not a choice, so that keeps it.
void ChooseFullScreen(bool on)
{
	SetFullScreen(on);
	if ((_prefs.Data.FullScreen != 0) != on)
	{
		_prefs.Data.FullScreen = on ? 1 : 0;
		_prefs.Save();
	}
}

// Give an item a new rect, keeping it hidden if it is: HideDialogItem moves
// a hidden item 16384 pixels to the right
static void PlaceItem(short item, Rect box)
{
	DialogItemType type;
	Handle h;
	Rect old;
	GetDialogItem(_dialog, item, &type, &h, &old);
	if (old.left >= 8192)
		MacOffsetRect(&box, 16384, 0);
	SetDialogItem(_dialog, item, type, h, &box);
}

// An item's DITL rect with its edges moved
static Rect DitlBox(short item, short left, short top, short right, short bottom)
{
	Rect box = _ditlBox[item];
	box.left += left;
	box.top += top;
	box.right += right;
	box.bottom += bottom;
	return box;
}

// Put a list into box, an item rect that includes its scroll bar. There is
// no LMove: set rView, let LSize recount the visible cells and move the
// scroll bar along. Its cells take the new width, so the columns spread
// out. Not drawn here: in now-playing the track list is hidden behind the
// cover, and the window's update draws the rest.
static void PlaceList(ListHandle list, const Rect& box)
{
	if (list == 0)
		return;
	ControlHandle bar = (**list).vScroll;
	Rect view = (**list).rView;
	Rect barRect = bar ? (**bar).contrlRect : view;

	short width = box.right - box.left - 15;  // CreateList's scroll bar
	short height = box.bottom - box.top;
	LSetDrawingMode(false, list);
	MacSetRect(&(**list).rView, box.left, box.top, box.left + width, box.top + height);
	LSize(width, height, list);
	Point cell = (**list).cellSize;
	cell.h = width;
	LCellSize(cell, list);
	if (bar)
	{
		MoveControl(bar, barRect.left - view.right + box.left + width,
		                 barRect.top - view.top + box.top);
		SizeControl(bar, barRect.right - barRect.left,
		                 barRect.bottom - barRect.top - (view.bottom - view.top) + height);
	}
	LSetDrawingMode(true, list);
}

// Fit the player to the window's size, from DITL 129's rects for the normal
// window. The lists grow, the controls row stays at the bottom (centred,
// the volume at the right) and the cover takes the space above it. In full
// screen the playlists start at x 10 and are 50 px wider, and the cover
// fills everything right of them, cropped to that shape.
void LayoutPlayer()
{
	short w = _dialog->portRect.right - _dialog->portRect.left;
	short h = _dialog->portRect.bottom - _dialog->portRect.top;
	short dv = h - kWindowHeight;     // the controls row moves down
	short dr = w - kWindowWidth;      // right-aligned items move right
	short dc = dr / 2;                // centred items
	short wider = _fullScreen ? 50 : 0;
	short inset = _fullScreen ? 5 : 0;  // playlists at x 10, like the track name below
	short tracks = inset + wider;      // the track list starts this much further right

	// System 7 flashes its network activity arrows in the screen's top-left
	// corner, normally on the menu bar: in full screen, keep the label clear
	short arrows = _fullScreen ? 10 : 0;

	PlaceItem(kPlayerPlaylistsLabel, DitlBox(kPlayerPlaylistsLabel, arrows, 0, arrows, 0));
	Rect navBox = DitlBox(kPlayerNavList, inset, 0, tracks, dv);
	PlaceItem(kPlayerNavList, navBox);
	PlaceList(_navList, navBox);

	// The artist column starts halfway the track list
	Rect trackBox = DitlBox(kPlayerTrackList, tracks, 0, dr, dv);
	short artist = tracks + (dr - tracks) / 2;
	PlaceItem(kPlayerTrackLabel, DitlBox(kPlayerTrackLabel, tracks, 0, tracks, 0));
	PlaceItem(kPlayerArtistLabel, DitlBox(kPlayerArtistLabel, artist, 0, artist, 0));
	PlaceItem(kPlayerTrackList, trackBox);
	PlaceList(_trackList, trackBox);

	for (short item = kPlayerPlayButton; item <= kPlayerRepeat; item++)
		PlaceItem(item, DitlBox(item, dc, dv, dc, dv));
	PlaceItem(kPlayerVolumeIcon, DitlBox(kPlayerVolumeIcon, dr, dv, dr, dv));
	Rect volumeBox = DitlBox(kPlayerVolumeControl, dr, dv, dr, dv);
	PlaceItem(kPlayerVolumeControl, volumeBox);

	DialogItemType type;
	Handle volume;
	Rect box;
	GetDialogItem(_dialog, kPlayerVolumeControl, &type, &volume, &box);
	MoveControl((ControlHandle)volume, volumeBox.left, volumeBox.top);

	if (_fullScreen)
	{
		// Everything right of the playlists, down to the controls row
		_coverWidth = w - trackBox.left;
		_coverHeight = 250 + dv;
		MacSetRect(&_trackImageRect, trackBox.left, 0, w, _coverHeight);
	}
	else
	{
		// 248 px square, centred in the window, 2 px clear of the controls
		_coverWidth = _coverHeight = 248;
		short left = (w - 250) / 2;
		MacSetRect(&_trackImageRect, left, 0, left + 250, 250);
	}
}

pascal OSErr Quit(AppleEvent* appleEvent, AppleEvent* reply, long refCon)
{
	_run = false;
	return noErr;
}


void InitCustomLDEF()
{
	// 10-byte code resource stub trick
	Handle h = GetResource('LDEF', 128);
	HLock(h);
	*(ListDefProcPtr*)(*h + 6) = &DarkListDef;

	Handle h2 = GetResource('CDEF', 128);
	HLock(h2);
	*(ControlDefProcPtr*)(*h2 + 6) = &DarkSliderDef;

	Handle h3 = GetResource('CDEF', 1);
	HLock(h3);
	*(ControlDefProcPtr*)(*h3 + 6) = &DarkScrollbarDef;
}
