/* Music server implementation extracted from gamecontext.cpp. */
#include "gamecontext.h"
#include "music_config.h"
#include "music_map.h"

#include <base/system.h>
#include <engine/server.h>
#include <engine/storage.h>

void CGameContext::ConSong(IConsole::IResult *pResult, void *pUserData)  
{  
    CGameContext *pSelf = (CGameContext *)pUserData;  
      
    if(pResult->NumArguments() != 1)  
    {  
        pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Usage: song <song_name>");  
        return;  
    }  
      
    const char *pSongName = pResult->GetString(0);  
	if(pSelf->EmbedSongAudioIntoCurrentMap(pSongName))
    {  
        char aBuf[256];  
        str_format(aBuf, sizeof(aBuf), "Successfully added audio '%s' to map", pSongName);  
        pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);  
          
        // 触发地图重载  
        pSelf->Console()->ExecuteLine("hot_reload");   
    }  
    else  
    {  
        pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Failed to modify map with audio");  
    }  
}

bool CGameContext::EmbedSongAudioIntoCurrentMap(const char *pSongId)
{
	char aFilePath[IO_MAX_PATH_LENGTH];
	str_format(aFilePath, sizeof(aFilePath), MusicConfig::AUDIO_FILE, pSongId);

	void *pAudioData = nullptr;
	unsigned AudioDataSize = 0;
	if(!Storage()->ReadFile(aFilePath, IStorage::TYPE_ALL, &pAudioData, &AudioDataSize))
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Failed to load audio file: %s", aFilePath);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return false;
	}

	char aOriginMapPath[IO_MAX_PATH_LENGTH];
	str_format(aOriginMapPath, sizeof(aOriginMapPath), MusicConfig::ORIGIN_MAP_FILE, Server()->GetMapName());

	char aTargetMapPath[IO_MAX_PATH_LENGTH];
	str_format(aTargetMapPath, sizeof(aTargetMapPath), MusicConfig::TARGET_MAP_FILE, Server()->GetMapName());

	const bool Success = ModifyMapWithAudio(aOriginMapPath, aTargetMapPath, pSongId, pAudioData, AudioDataSize, true);
	free(pAudioData);
	return Success;
}

bool CGameContext::ModifyMapWithAudio(const char *pOriginMapPath, const char *pTargetMapPath, const char *pSoundName, void *pAudioData, unsigned AudioDataSize, bool bGenerateWebMap)
{
    SMusicMapResult Result;
    const bool Success = WriteMusicMapWithAudio(Storage(), Server()->GetMapName(), pOriginMapPath, pTargetMapPath, pSoundName, pAudioData, AudioDataSize, bGenerateWebMap, &Result);
    if(Success && Result.m_HasWebMap)
        RequestUploadToObjectStorage(Server()->GetMapName(), Result.m_aSha256);
    return Success;
}
