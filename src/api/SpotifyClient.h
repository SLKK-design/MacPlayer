#include <string>
#include <map>
#include <functional>
#include "gason.hpp"
#include "Prefs.h"
#include "HttpClient.h"
#include "HttpResponse.h"
#include "CoverArt.h"

#ifndef __SPOTIFY_CLIENT__
#define __SPOTIFY_CLIENT__

using namespace std;
using namespace std::placeholders;
using namespace gason;

// Called instead of the error dialog when a request gets a non-2xx answer
typedef function<void(int statusCode, const string& body)> SpotifyErrorHandler;

struct LoginResponse
{
	bool Success;
	string ErrorMsg;
};

class SpotifyClient
{
	public:
		string AccessToken, RefreshToken;
		CoverGray ActiveCover;  // brightness of the current album's cover, empty if none
		string DeviceId;  // the Spotify Connect device MacPlayer plays on, "" if none yet
		SpotifyClient(HttpClient* httpClient, Prefs* prefs);
		void Login(const string& refreshToken, function<void(LoginResponse)> onComplete);
		void GetRecentTracks(function<void(JsonValue&)> onComplete);
		void PlayTrack(const string& contextUri, const string& trackUri, function<void(JsonValue&)> onComplete);
		void NextTrack(function<void(JsonValue&)> onComplete);
		void PreviousTrack(function<void(JsonValue&)> onComplete);
		void GetDevices(function<void(JsonValue&)> onComplete);
		void GetCurrentUser(function<void(JsonValue&)> onComplete);
		void GetPlaylists(function<void(JsonValue&)> onComplete);
		void GetPlaylistTracks(const string& playlistId, function<void(JsonValue&)> onComplete);
		void ActivateDevice(const string& deviceId, function<void(JsonValue&)> onComplete,
		                    SpotifyErrorHandler onError = nullptr);
		void GetImage(const string& image, const string& albumId, function<void()> onComplete);
		void GetPlayerState(function<void(JsonValue&)> onComplete);
		void Play(function<void(JsonValue&)> onComplete, SpotifyErrorHandler onError = nullptr);
		void Pause(function<void(JsonValue&)> onComplete, SpotifyErrorHandler onError = nullptr);
		void SetVolume(int volumePercent, function<void(JsonValue&)> onComplete);
		void HandleError(const string& errorMsg);
		static string ErrorMessage(const string& body);
		void InitCache();

	private:
		class SpotifyRequest
		{
			public:
				string Uri;
				string Content;
				function<void(JsonValue&)> OnComplete;
				function<void(int)> DoRequest;
				SpotifyErrorHandler OnError;  // nullptr: show the error dialog
				bool Refreshed;
				bool WokeDevice;  // already retried after WakeDevice
				bool Debug;
				bool ExpectJson;  // GETs; commands' bodies are never parsed
		};

		const string _permissions =
			"user-read-playback-state "
			"user-modify-playback-state "
			"user-read-recently-played "
			"playlist-read-private "
			"playlist-read-collaborative";

		HttpClient* _httpClient;
		Prefs* _prefs;
		JsonValue _root;
		short _cacheVRefNum;
		long _cacheDirId;
		map<int, SpotifyRequest> _requests;
		int _requestId = 0;

		bool WakeDevice();
		string PlayerUrl(const string& command, const string& query = "");
		void Get(string uri, bool debug, function<void(JsonValue&)> onComplete);
		void Post(string uri, bool debug, string content, function<void(JsonValue&)> onComplete,
		          SpotifyErrorHandler onError = nullptr);
		void Put(string uri, bool debug, string content, function<void(JsonValue&)> onComplete,
		         SpotifyErrorHandler onError = nullptr);
		void Request(
			string uri,
			string content,
			bool debug,
			function<void(JsonValue&)> onComplete,
			function<void(int)> doRequest,
			bool expectJson,
			SpotifyErrorHandler onError);

		void DoGet(int requestId);
		void DoPost(int requestId);
		void DoPut(int requestId);
		void HandleHttpResponse(HttpResponse& response, int requestId);
		void RefreshAccessToken(int requestId);
		bool RequestAccessToken();
		bool SaveImage(const FSSpec* imageSpec, const CoverGray& cover);
};

#endif
