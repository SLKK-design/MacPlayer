#ifndef _PREFS_
#define _PREFS_

struct PrefsData
{
	/* Login: what tools/spotify_auth.py writes (766 bytes) */
	char AccessToken[255];
	char RefreshToken[255];
	char ClientId[128];
	char ClientSecret[128];

	/* MacPlayer's own settings. A file from the script ends before them,
	   so they load as 0, their defaults. */
	char FullScreen;  /* the player was last left in full screen */
};

class Prefs
{
	public:
		Prefs();
		PrefsData Data;
		bool Load();
		bool Save();
	private:
		bool GetPrefsSpec(FSSpec *theSpec);
};

#endif // _PREFS_ 