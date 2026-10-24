#include <string.h>
#include <Folders.h>
#include <Dialogs.h>
#include <stddef.h>
#include "Prefs.h"

// tools/spotify_auth.py writes the login fields: they must stay 766 bytes
static_assert(offsetof(PrefsData, FullScreen) == 766, "login fields moved: update tools/spotify_auth.py");

Prefs::Prefs()
{
	Load();
}

/* Read "MacPlayer Preferences" (also written by tools/spotify_auth.py).
 * A missing file leaves every field empty; a shorter one, such as the
 * script's, leaves the fields after its end at 0. */
bool Prefs::Load()
{
	long theSize;
	FSSpec theSpec;
	short theFile;
	OSErr err;
	bool ok = false;

	memset(&Data, 0, sizeof(Data));

	if (GetPrefsSpec(&theSpec))
	{
		if (FSpOpenDF(&theSpec, fsRdPerm, &theFile) == noErr)
		{
			theSize = sizeof(PrefsData);
			err = FSRead(theFile, &theSize, &Data);
			ok = (err == noErr || err == eofErr);
			if (!ok)
				memset(&Data, 0, sizeof(Data));

			FSClose(theFile);
		}
	}

	// The file comes from another machine; never trust it to be terminated
	Data.AccessToken[sizeof(Data.AccessToken) - 1] = '\0';
	Data.RefreshToken[sizeof(Data.RefreshToken) - 1] = '\0';
	Data.ClientId[sizeof(Data.ClientId) - 1] = '\0';
	Data.ClientSecret[sizeof(Data.ClientSecret) - 1] = '\0';

	return ok;
}

bool Prefs::Save()
{
	long size = sizeof(PrefsData);
	FSSpec fsSpec;
	short file;
	OSErr err;

	if (GetPrefsSpec(&fsSpec)) {
		err = FSpOpenDF(&fsSpec, fsRdWrPerm, &file);
		if (err != noErr) {
			if ((err = FSpCreate(&fsSpec, 'MPLY', 'pref', 0)) == noErr)
				err = FSpOpenDF(&fsSpec, fsRdWrPerm, &file);
		}
		if (err == noErr) {
			if (SetEOF(file, 0L) == noErr) {
				if (FSWrite(file, &size, &Data) == noErr) {
					FSClose(file);
					return true;
				}

				FSClose(file);
			}
		}
	}

	return false;
}

bool Prefs::GetPrefsSpec(FSSpec *theSpec)
{
	Str255 prefsName = "\pMacPlayer Preferences";
	short vRefNum;
	long dirID;

	if (FindFolder(kOnSystemDisk, kPreferencesFolderType, kCreateFolder, &vRefNum, &dirID) == noErr) {
		FSMakeFSSpec(vRefNum, dirID, prefsName, theSpec);
		return true;
	}

	return false;
}