#include <Dialogs.h>
#include <Lists.h>
#include <vector>
#include "Prefs.h"
#include "HttpClient.h"
#include "CDEF/DarkListDef.h"  /* src/ui/CDEF */
#include "CDEF/DarkScrollbarDef.h"
#include "CDEF/DarkSliderDef.h"
#include "SpotifyClient.h"

const double mPollFrequencyMs = 30000;
const int mRestartTrackMs = 3000;  // later into a track, back restarts it

enum UIState
{
	Init,
	Login,
	Player
};

enum
{
	kMenuApple = 128,
	kMenuPlayer,
	kMenuDevices
};

enum
{
	kItemAbout      = 1,
	kItemFullScreen = 1,  /* Player menu */
	kItemQuit       = 3
};

enum UIElements
{
	kPlayerDialog = 128,
	kPlayerMenubar = 128,
	kPlayerLoginButton = 3,
	kPlayerPlaylistsLabel = 1,
	kPlayerNavList = 2,
	kPlayerTrackLabel = 3,
	kPlayerArtistLabel = 4,
	kPlayerTrackList = 5,
	kPlayerPlayButton = 6,
	kPlayerPrevTrack = 7,
	kPlayerNextTrack = 8,
	kPlayerShuffle = 9,
	kPlayerRepeat = 10,
	kPlayerVolumeIcon = 11,
	kPlayerVolumeControl = 12,
	kWindowWidth = 505,   /* DLOG 128; DITL 129 is laid out for this size */
	kWindowHeight = 296,
	kPlayerStartupDITL = 130,
	kPlayerLoginDITL = 128,
	kPlayerMainDITL = 129,
	kPlayerPlayIcon = 128,
	kPlayerPauseIcon = 136,
	kPlayerShuffleIcon = 131,
	kPlayerShuffleOnIcon = 137,
	kPlayerRepeatIcon = 132,
	kPlayerRepeatOnIcon = 138,
	kPlayerRepeatOneIcon = 139,
	kAboutDialog = 133,
	kPlaceholderImage = 134
};

struct Device
{
	string name;
	string id;
	bool active;
};

struct Playlist
{
	string name;
	string id;  // "" for Liked Songs
	string uri;
};


struct Track
{
	string name;
	string artist;
	string uri;
	string image;
	string albumId;
};

struct PlayerState
{
	bool isPlaying;
	bool shuffle;
	short repeat;    // index in kRepeatModes (MacPlayer.cpp): off, context, track
	int progressMs;  // into the track at the last RefreshPlayerState; 0 if unknown
};

bool _run = true;
UIState _uiState = Login;
HttpClient _httpClient;
Prefs _prefs;
SpotifyClient _spotifyClient(&_httpClient, &_prefs);
UnsignedWide _lastPollTime;
PlayerState _playerState;
bool _viewNowPlaying = false;


int main();
void InitToolBox();
void ShowMainWindow();
void MenuInit();
void EventInit();
void EventLoop();
void InitPlayer(DialogPtr dialog);
void DoEvent(EventRecord *eventPtr);
void HandleMouseDown(EventRecord *eventPtr);
void HandleInContent(EventRecord *eventPtr);
void HandleLoginContent(short item);
void ApplyPrefsCredentials();
void TryLogin();
void HandleUpdate(EventRecord *eventPtr);
void HandleActivate(EventRecord *eventPtr);
void HandleOSEvt(EventRecord *eventPtr);
void ModeInit(DialogPtr dialog);
void DisposePlayerLists();
void ModeLogin(DialogPtr dialog);
void ModePlayer(DialogPtr dialog);
void HandleMenuChoice(long menuChoice);
void ShowAboutBox();
void WaitCursor();
void HandlePlayerContent(EventRecord *eventPtr, short item);
void GetRecentTracks();
void GetPlaylists(DialogPtr dialog);
void GetPlaylistTracks(const string& uri, const string& playlistId);
void GetLikedSongs();
void InitCustomLDEF();
void PlayTrack();
void ViewNowPlaying();
bool RefreshPlayerState();
void LoadPendingImage();
void PollSoon(double delayMs);
void PopulateTrackList(JsonValue& root);
Track GetTrackObject(JsonValue& track);
void SetTrackImage(JsonValue& track, Track& trackObj);
void PollPlayerState(); 
void DrawTrackImage();
void TogglePlayButtonIcon();
void UpdateShuffleIcon();
void UpdateRepeatIcon();
void ExitApp();
void ActivateDevice(int index);
void GetDevices();
void NoDevicesError();
void MarkDevice();
void PlayFromTrackList();
void DrawTrackName();
void DrawPlaceholderImage();
void TrackChanging();
void SetFullScreen(bool on);
void ChooseFullScreen(bool on);
void RestoreMenuBar();
void LayoutPlayer();

vector<Device> _devices;
vector<Track> _tracks;
vector<Playlist> _playlists;
Track _currentTrack;
string _currentContext;
Rect _trackImageRect;     /* the cover's place (normal window: 2 px larger) */
short _coverWidth = 248, _coverHeight = 248;  /* the cover in the current layout */
bool _fullScreen = false;
int _currentVolume;

ListHandle _navList = 0, _trackList = 0;
string _userId;  /* Spotify user id, for telling own playlists from followed ones */
ListHandle CreateList(
	WindowPtr windowPtr,
	Rect rect,
	int columnsInList,
	int ldef,
	short cellWidth,
	short cellHeight);
void PopulateNavList(ListHandle list);

pascal OSErr Quit(AppleEvent* appleEvent, AppleEvent* reply, long refCon);
