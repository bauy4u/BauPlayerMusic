#include "music_playlist.h"

#include "music.h"

#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <string>
#include <vector>

namespace
{

std::string EscapePlaylistField(const std::string &Field)
{
	std::string Result;
	Result.reserve(Field.size());
	for(char c : Field)
	{
		if(c == '\\' || c == '|')
		{
			Result.push_back('\\');
			Result.push_back(c);
		}
		else if(c == '\n')
		{
			Result.append("\\n");
		}
		else if(c == '\r')
		{
			Result.append("\\r");
		}
		else
		{
			Result.push_back(c);
		}
	}
	return Result;
}

std::vector<std::string> SplitPlaylistLine(const char *pLine)
{
	std::vector<std::string> vFields;
	std::string Field;
	bool Escape = false;
	for(const char *p = pLine; *p; ++p)
	{
		const char c = *p;
		if(Escape)
		{
			if(c == 'n')
				Field.push_back('\n');
			else if(c == 'r')
				Field.push_back('\r');
			else
				Field.push_back(c);
			Escape = false;
		}
		else if(c == '\\')
		{
			Escape = true;
		}
		else if(c == '|')
		{
			vFields.push_back(Field);
			Field.clear();
		}
		else
		{
			Field.push_back(c);
		}
	}
	if(Escape)
		Field.push_back('\\');
	vFields.push_back(Field);
	return vFields;
}

}

namespace MusicPlaylistStorage
{

CLoadResult Load(IStorage *pStorage, const char *pFilePath, CMusicState *pMusic)
{
	CLoadResult Result;
	CMusicQueueSnapshot Snapshot;
	pMusic->ImportQueueSnapshot(Snapshot);

	if(!pStorage->FileExists(pFilePath, IStorage::TYPE_SAVE))
		return Result;

	Result.m_FileExists = true;
	IOHANDLE File = pStorage->OpenFile(pFilePath, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return Result;

	CLineReader LineReader;
	LineReader.OpenFile(File);

	const char *pLine;
	while((pLine = LineReader.Get()) != nullptr)
	{
		char aBuffer[1024];
		str_copy(aBuffer, pLine, sizeof(aBuffer));

		if(str_comp_num(aBuffer, "STATE|", 6) == 0)
		{
			char *pStateData = aBuffer + 6;

			char *pIndex = pStateData;
			char *pStartTime = (char *)str_find(pIndex, "|");
			if(!pStartTime)
				continue;
			*pStartTime = '\0';
			pStartTime++;

			char *pDuration = (char *)str_find(pStartTime, "|");
			if(!pDuration)
				continue;
			*pDuration = '\0';
			pDuration++;

			char *pIsPlaying = (char *)str_find(pDuration, "|");
			if(!pIsPlaying)
				continue;
			*pIsPlaying = '\0';
			pIsPlaying++;

			char *pNextPreload = (char *)str_find(pIsPlaying, "|");
			if(!pNextPreload)
				continue;
			*pNextPreload = '\0';
			pNextPreload++;

			Snapshot.m_CurrentSongIndex = str_toint(pIndex);
			Snapshot.m_CurrentSongStartTime = str_toint64_base(pStartTime, 10);
			Snapshot.m_CurrentSongDuration = str_tofloat(pDuration);
			Snapshot.m_IsPlayingFromQueue = str_comp(pIsPlaying, "true") == 0;
			Snapshot.m_NextPreloadIndex = str_toint(pNextPreload);
			continue;
		}

		std::vector<std::string> vFields = SplitPlaylistLine(aBuffer);
		if(vFields.size() < 6)
		{
			Result.m_SkippedLines++;
			continue;
		}

		float Duration = 0.0f;
		if(!str_tofloat(vFields[3].c_str(), &Duration) || vFields[0].empty() || vFields[2].empty())
		{
			Result.m_SkippedLines++;
			continue;
		}

		SongInfo Song;
		Song.title = vFields[0];
		Song.artist = vFields[1];
		Song.page_url = vFields[2];
		Song.duration = Duration;
		Song.isPreloaded = vFields[4] == "true";
		Song.isReady = vFields[5] == "true";
		if(vFields.size() >= 9)
		{
			Song.requesterName = vFields[6];
			Song.requesterSource = vFields[7];
			Song.requesterId = vFields[8];
		}
		else
		{
			Song.requesterName = "未知";
			Song.requesterSource = "legacy";
			Song.requesterId = "legacy";
		}
		if(Song.isPreloaded && !Song.isReady)
			Song.isPreloaded = false;

		Snapshot.m_vSongs.push_back(Song);
		Result.m_LoadedSongs++;
	}

	pMusic->ImportQueueSnapshot(std::move(Snapshot));
	Result.m_Loaded = true;
	return Result;
}

bool Save(IStorage *pStorage, const char *pFilePath, const char *pStateDir, const CMusicState &Music)
{
	pStorage->CreateFolder("data", IStorage::TYPE_SAVE);
	pStorage->CreateFolder(pStateDir, IStorage::TYPE_SAVE);

	char aTempFilePath[IO_MAX_PATH_LENGTH];
	str_format(aTempFilePath, sizeof(aTempFilePath), "%s.tmp", pFilePath);
	IOHANDLE File = pStorage->OpenFile(aTempFilePath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;

	const CMusicQueueSnapshot Snapshot = Music.ExportQueueSnapshot();

	char aStateLine[256];
	str_format(aStateLine, sizeof(aStateLine), "STATE|%d|%lld|%.2f|%s|%d\n",
		Snapshot.m_CurrentSongIndex, Snapshot.m_CurrentSongStartTime, Snapshot.m_CurrentSongDuration,
		Snapshot.m_IsPlayingFromQueue ? "true" : "false", Snapshot.m_NextPreloadIndex);
	io_write(File, aStateLine, str_length(aStateLine));

	for(const SongInfo &Song : Snapshot.m_vSongs)
	{
		const std::string Title = EscapePlaylistField(Song.title);
		const std::string Artist = EscapePlaylistField(Song.artist);
		const std::string Url = EscapePlaylistField(Song.page_url);
		const std::string RequesterName = EscapePlaylistField(Song.requesterName);
		const std::string RequesterSource = EscapePlaylistField(Song.requesterSource);
		const std::string RequesterId = EscapePlaylistField(Song.requesterId);
		char aLine[1024];
		str_format(aLine, sizeof(aLine), "%s|%s|%s|%.2f|%s|%s|%s|%s|%s\n",
			Title.c_str(), Artist.c_str(), Url.c_str(),
			Song.duration, Song.isPreloaded ? "true" : "false", Song.isReady ? "true" : "false",
			RequesterName.c_str(), RequesterSource.c_str(), RequesterId.c_str());

		io_write(File, aLine, str_length(aLine));
	}

	io_close(File);

	char aTempAbsolute[IO_MAX_PATH_LENGTH];
	char aTargetAbsolute[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, aTempFilePath, aTempAbsolute, sizeof(aTempAbsolute));
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, pFilePath, aTargetAbsolute, sizeof(aTargetAbsolute));
	if(fs_rename(aTempAbsolute, aTargetAbsolute) != 0)
	{
		fs_remove(aTempAbsolute);
		return false;
	}
	return true;
}

}
