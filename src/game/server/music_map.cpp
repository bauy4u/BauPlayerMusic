#include "music_map.h"
#include "music_config.h"

#include <engine/shared/datafile.h>
#include <engine/storage.h>
#include <game/gamecore.h>
#include <game/mapitems.h>

#include <iterator>

static bool CopyFileInStorage(IStorage *pStorage, const char *pSourcePath, const char *pTargetPath)
{
	IOHANDLE SrcFile = pStorage->OpenFile(pSourcePath, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!SrcFile)
		return false;

	IOHANDLE DstFile = pStorage->OpenFile(pTargetPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!DstFile)
	{
		io_close(SrcFile);
		return false;
	}

	char aBuffer[4096];
	int BytesRead;
	while((BytesRead = io_read(SrcFile, aBuffer, sizeof(aBuffer))) > 0)
		io_write(DstFile, aBuffer, BytesRead);

	io_close(DstFile);
	io_close(SrcFile);
	return true;
}

static void AddGlobalSoundLayer(CDataFileWriter &Writer, int SoundIndex, int LayerIndex, const char *pSoundName)
{
	CSoundSource Source;
	Source.m_Position.x = f2fx(0.0f);
	Source.m_Position.y = f2fx(0.0f);
	Source.m_Loop = 1;
	Source.m_Pan = 1;
	Source.m_TimeDelay = 0;
	Source.m_Falloff = 0;
	Source.m_PosEnv = -1;
	Source.m_PosEnvOffset = 0;
	Source.m_SoundEnv = -1;
	Source.m_SoundEnvOffset = 0;
	Source.m_Shape.m_Type = CSoundShape::SHAPE_RECTANGLE;
	Source.m_Shape.m_Rectangle.m_Width = f2fx(100000.0f);
	Source.m_Shape.m_Rectangle.m_Height = f2fx(100000.0f);

	CMapItemLayerSounds LayerItem;
	LayerItem.m_Version = 2;
	LayerItem.m_Layer.m_Version = 0;
	LayerItem.m_Layer.m_Flags = 0;
	LayerItem.m_Layer.m_Type = LAYERTYPE_SOUNDS;
	LayerItem.m_Sound = SoundIndex;
	LayerItem.m_NumSources = 1;
	LayerItem.m_Data = Writer.AddDataSwapped(sizeof(CSoundSource), &Source);

	char aLayerName[64];
	str_format(aLayerName, sizeof(aLayerName), "%s", pSoundName);
	if(str_length(aLayerName) > 11)
		aLayerName[11] = '\0';
	StrToInts(LayerItem.m_aName, std::size(LayerItem.m_aName), aLayerName);

	Writer.AddItem(MAPITEMTYPE_LAYER, LayerIndex, sizeof(LayerItem), &LayerItem);
}

bool WriteMusicMapWithAudio(IStorage *pStorage, const char *pCurrentMapName, const char *pOriginMapPath, const char *pTargetMapPath, const char *pSoundName, void *pAudioData, unsigned AudioDataSize, bool GenerateWebMap, SMusicMapResult *pResult)
{
	if(pResult)
		*pResult = SMusicMapResult();

	dbg_msg("music_map", "embedding sound '%s' into map '%s'", pSoundName, pCurrentMapName);

	CDataFileReader Reader;
	if(!Reader.Open(pStorage, pOriginMapPath, IStorage::TYPE_ALL))
	{
		dbg_msg("music_map", "failed to open origin map: %s", pOriginMapPath);
		return false;
	}

	int SoundStart, SoundNum;
	Reader.GetType(MAPITEMTYPE_SOUND, &SoundStart, &SoundNum);

	int GroupStart, GroupNum;
	Reader.GetType(MAPITEMTYPE_GROUP, &GroupStart, &GroupNum);
	if(GroupNum <= 0)
	{
		dbg_msg("music_map", "origin map has no groups");
		Reader.Close();
		return false;
	}

	const int LastGroupIndex = GroupNum - 1;
	CMapItemGroup *pLastGroup = (CMapItemGroup *)Reader.GetItem(GroupStart + LastGroupIndex);
	if(!pLastGroup)
	{
		dbg_msg("music_map", "failed to read last group");
		Reader.Close();
		return false;
	}
	const int LayerIndex = pLastGroup->m_StartLayer + pLastGroup->m_NumLayers;

	char aTempPath[IO_MAX_PATH_LENGTH];
	str_format(aTempPath, sizeof(aTempPath), "%s.tmp", pTargetMapPath);

	CDataFileWriter Writer;
	if(!Writer.Open(pStorage, aTempPath, IStorage::TYPE_SAVE))
	{
		dbg_msg("music_map", "failed to create temporary map: %s", aTempPath);
		Reader.Close();
		return false;
	}

	for(int Index = 0; Index < Reader.NumItems(); ++Index)
	{
		int Type, Id;
		CUuid Uuid;
		const void *pItem = Reader.GetItem(Index, &Type, &Id, &Uuid);
		if(Type == ITEMTYPE_EX || Type == MAPITEMTYPE_GROUP)
			continue;

		Writer.AddItem(Type, Id, Reader.GetItemSize(Index), pItem, &Uuid);
	}

	for(int Index = 0; Index < Reader.NumData(); ++Index)
	{
		const void *pData = Reader.GetData(Index);
		const int DataSize = Reader.GetDataSize(Index);
		if(pData && DataSize > 0)
			Writer.AddData(DataSize, pData);
	}

	CMapItemSound SoundItem;
	SoundItem.m_Version = 1;
	SoundItem.m_External = 0;
	SoundItem.m_SoundName = Writer.AddDataString(pSoundName);
	SoundItem.m_SoundData = Writer.AddData(AudioDataSize, pAudioData);
	SoundItem.m_SoundDataSize = AudioDataSize;
	Writer.AddItem(MAPITEMTYPE_SOUND, SoundNum, sizeof(SoundItem), &SoundItem);

	AddGlobalSoundLayer(Writer, SoundNum, LayerIndex, pSoundName);

	for(int GroupIndex = 0; GroupIndex < GroupNum; ++GroupIndex)
	{
		CMapItemGroup *pOriginalGroup = (CMapItemGroup *)Reader.GetItem(GroupStart + GroupIndex);
		if(!pOriginalGroup)
			continue;

		CMapItemGroup NewGroup = *pOriginalGroup;
		if(GroupIndex == LastGroupIndex)
			NewGroup.m_NumLayers++;
		Writer.AddItem(MAPITEMTYPE_GROUP, GroupIndex, sizeof(NewGroup), &NewGroup);
	}

	Reader.Close();
	Writer.Finish();

	if(GenerateWebMap)
	{
		void *pMapData = nullptr;
		unsigned MapDataSize = 0;
		if(pStorage->ReadFile(aTempPath, IStorage::TYPE_SAVE, &pMapData, &MapDataSize))
		{
			const SHA256_DIGEST MapSha256 = sha256(pMapData, MapDataSize);
			char aSha256[SHA256_MAXSTRSIZE];
			sha256_str(MapSha256, aSha256, sizeof(aSha256));

			char aWebMapPath[IO_MAX_PATH_LENGTH];
			str_format(aWebMapPath, sizeof(aWebMapPath), MusicConfig::WEBMAP_FILE, pCurrentMapName, aSha256);
			pStorage->CreateFolder(MusicConfig::WEBMAP_DIR, IStorage::TYPE_SAVE);
			if(CopyFileInStorage(pStorage, aTempPath, aWebMapPath) && pResult)
			{
				pResult->m_HasWebMap = true;
				str_copy(pResult->m_aSha256, aSha256, sizeof(pResult->m_aSha256));
			}

			free(pMapData);
		}
	}

	if(!pStorage->RemoveFile(pTargetMapPath, IStorage::TYPE_SAVE))
		dbg_msg("music_map", "target map did not exist or could not be removed: %s", pTargetMapPath);

	if(!pStorage->RenameFile(aTempPath, pTargetMapPath, IStorage::TYPE_SAVE))
	{
		dbg_msg("music_map", "failed to replace target map: %s", pTargetMapPath);
		pStorage->RemoveFile(aTempPath, IStorage::TYPE_SAVE);
		return false;
	}

	return true;
}
