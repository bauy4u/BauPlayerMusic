#include <base/logger.h>
#include <base/system.h>

#include <engine/shared/datafile.h>
#include <engine/storage.h>

#include <game/gamecore.h>
#include <game/mapitems.h>

#include <iterator>
#include <memory>

static const char *TOOL_NAME = "music_map_patcher";

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

static int PatchMap(IStorage *pStorage, const char *pOriginMapPath, const char *pOutputMapPath, const char *pSoundName, const char *pAudioPath)
{
	void *pAudioData = nullptr;
	unsigned AudioDataSize = 0;
	if(!pStorage->ReadFile(pAudioPath, IStorage::TYPE_ABSOLUTE, &pAudioData, &AudioDataSize))
	{
		log_error(TOOL_NAME, "Failed to read audio file '%s'", pAudioPath);
		return -1;
	}

	CDataFileReader Reader;
	if(!Reader.Open(pStorage, pOriginMapPath, IStorage::TYPE_ABSOLUTE))
	{
		log_error(TOOL_NAME, "Failed to open origin map '%s'", pOriginMapPath);
		free(pAudioData);
		return -1;
	}

	int SoundStart = 0;
	int SoundNum = 0;
	Reader.GetType(MAPITEMTYPE_SOUND, &SoundStart, &SoundNum);

	int GroupStart = 0;
	int GroupNum = 0;
	Reader.GetType(MAPITEMTYPE_GROUP, &GroupStart, &GroupNum);
	if(GroupNum <= 0)
	{
		log_error(TOOL_NAME, "Origin map has no groups");
		Reader.Close();
		free(pAudioData);
		return -1;
	}

	const int LastGroupIndex = GroupNum - 1;
	CMapItemGroup *pLastGroup = (CMapItemGroup *)Reader.GetItem(GroupStart + LastGroupIndex);
	if(!pLastGroup)
	{
		log_error(TOOL_NAME, "Failed to read last group");
		Reader.Close();
		free(pAudioData);
		return -1;
	}
	const int LayerIndex = pLastGroup->m_StartLayer + pLastGroup->m_NumLayers;

	char aTempPath[IO_MAX_PATH_LENGTH];
	str_format(aTempPath, sizeof(aTempPath), "%s.tmp", pOutputMapPath);
	if(fs_makedir_rec_for(aTempPath) != 0)
	{
		log_error(TOOL_NAME, "Failed to create output folder for '%s'", aTempPath);
		Reader.Close();
		free(pAudioData);
		return -1;
	}

	CDataFileWriter Writer;
	if(!Writer.Open(pStorage, aTempPath, IStorage::TYPE_ABSOLUTE))
	{
		log_error(TOOL_NAME, "Failed to create temporary output map '%s'", aTempPath);
		Reader.Close();
		free(pAudioData);
		return -1;
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
	free(pAudioData);

	char aBackupPath[IO_MAX_PATH_LENGTH];
	str_format(aBackupPath, sizeof(aBackupPath), "%s.musicbak", pOutputMapPath);
	fs_remove(aBackupPath);

	const bool HadOutput = fs_is_file(pOutputMapPath);
	if(HadOutput && fs_rename(pOutputMapPath, aBackupPath) != 0)
	{
		log_error(TOOL_NAME, "Failed to back up existing output map '%s'", pOutputMapPath);
		fs_remove(aTempPath);
		return -1;
	}

	if(fs_rename(aTempPath, pOutputMapPath) != 0)
	{
		log_error(TOOL_NAME, "Failed to move '%s' to '%s'", aTempPath, pOutputMapPath);
		fs_remove(aTempPath);
		if(HadOutput && fs_rename(aBackupPath, pOutputMapPath) != 0)
			log_error(TOOL_NAME, "Failed to restore backup map '%s'", aBackupPath);
		return -1;
	}
	if(HadOutput)
		fs_remove(aBackupPath);

	log_info(TOOL_NAME, "Wrote music map '%s'", pOutputMapPath);
	return 0;
}

int main(int argc, const char **argv)
{
	CCmdlineFix CmdlineFix(&argc, &argv);
	log_set_global_logger_default();

	if(argc != 5)
	{
		log_error(TOOL_NAME, "Usage: %s <origin_map> <output_map> <sound_name> <audio_file>", TOOL_NAME);
		return -1;
	}

	std::unique_ptr<IStorage> pStorage = std::unique_ptr<IStorage>(CreateStorage(IStorage::EInitializationType::BASIC, argc, argv));
	if(!pStorage)
	{
		log_error(TOOL_NAME, "Error creating basic storage");
		return -1;
	}

	return PatchMap(pStorage.get(), argv[1], argv[2], argv[3], argv[4]);
}
