#include "Keys.h"

// Empty on purpose: tools/spotify_auth.py writes your Spotify app credentials
// to MacPlayer Preferences, and MacPlayer reads them from there at launch.
// Anything filled in here is compiled into the app.
string Keys::SpotifyClientId = "";
string Keys::SpotifyClientSecret = "";
