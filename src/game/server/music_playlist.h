#ifndef GAME_SERVER_MUSIC_PLAYLIST_H
#define GAME_SERVER_MUSIC_PLAYLIST_H

class IStorage;
class CMusicState;

namespace MusicPlaylistStorage
{

struct CLoadResult
{
	bool m_FileExists = false;
	bool m_Loaded = false;
	int m_LoadedSongs = 0;
	int m_SkippedLines = 0;
};

CLoadResult Load(IStorage *pStorage, const char *pFilePath, CMusicState *pMusic);
bool Save(IStorage *pStorage, const char *pFilePath, const char *pStateDir, const CMusicState &Music);

}

#endif
