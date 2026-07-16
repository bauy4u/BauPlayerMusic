#ifndef GAME_SERVER_MUSIC_MAP_H
#define GAME_SERVER_MUSIC_MAP_H

#include <base/hash.h>
#include <base/system.h>

class IStorage;

struct SMusicMapResult
{
	bool m_HasWebMap = false;
	char m_aSha256[SHA256_MAXSTRSIZE] = {0};
};

bool WriteMusicMapWithAudio(IStorage *pStorage, const char *pCurrentMapName, const char *pOriginMapPath, const char *pTargetMapPath, const char *pSoundName, void *pAudioData, unsigned AudioDataSize, bool GenerateWebMap, SMusicMapResult *pResult);

#endif
