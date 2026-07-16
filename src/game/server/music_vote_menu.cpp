/* Per-player music vote menu entries. */
#include "gamecontext.h"
#include "player.h"

#include <base/system.h>
#include <engine/shared/config.h>

#include <algorithm>

namespace
{

constexpr const char *MUSIC_NOW_HEADER = "---------- 正在播放 ----------";
constexpr const char *MUSIC_QUEUE_HEADER = "---------- 播放队列 ----------";
constexpr const char *MUSIC_QUEUE_EMPTY = "队列为空";
constexpr const char *MUSIC_QUEUE_MORE = "... 还有更多队列歌曲";
constexpr const char *MUSIC_SEARCH_HEADER = "---------- 搜索结果 ----------";
constexpr const char *MUSIC_SEARCH_ACTION = "搜索[理由=歌名]";
constexpr const char *MUSIC_SKIP_ACTION = ">|  跳过当前歌曲";
constexpr int PROGRESS_BAR_WIDTH = 24;

void FormatSongOption(char *pBuf, int BufSize, const char *pPrefix, int Index, const SongInfo &Song)
{
	if(Song.requesterName.empty())
		str_format(pBuf, BufSize, "%s%02d | %s - %s", pPrefix, Index + 1, Song.title.c_str(), Song.artist.c_str());
	else
		str_format(pBuf, BufSize, "%s%02d | %s - %s | %s 点歌", pPrefix, Index + 1, Song.title.c_str(), Song.artist.c_str(), Song.requesterName.c_str());
}

void FormatTime(char *pBuf, int BufSize, int Seconds)
{
	if(Seconds < 0)
		Seconds = 0;
	str_format(pBuf, BufSize, "%02d:%02d", Seconds / 60, Seconds % 60);
}

bool FormatNowPlayingProgress(const CMusicState &Music, char *pBuf, int BufSize)
{
	if(!Music.HasCurrentQueueSong() || Music.CurrentSongDuration() <= 0.0f || Music.CurrentSongStartTime() <= 0)
		return false;

	const int64_t Now = time_timestamp();
	const int DurationSeconds = (int)Music.CurrentSongDuration();
	int ElapsedSeconds = (int)(Now - Music.CurrentSongStartTime());
	ElapsedSeconds = std::max(0, std::min(ElapsedSeconds, DurationSeconds));

	const float Progress = DurationSeconds > 0 ? (float)ElapsedSeconds / (float)DurationSeconds : 0.0f;
	const int Filled = std::max(0, std::min(PROGRESS_BAR_WIDTH, (int)(Progress * PROGRESS_BAR_WIDTH + 0.5f)));

	char aBar[PROGRESS_BAR_WIDTH + 1];
	for(int i = 0; i < PROGRESS_BAR_WIDTH; ++i)
		aBar[i] = i < Filled ? '|' : ' ';
	aBar[PROGRESS_BAR_WIDTH] = '\0';

	char aElapsed[16];
	char aDuration[16];
	FormatTime(aElapsed, sizeof(aElapsed), ElapsedSeconds);
	FormatTime(aDuration, sizeof(aDuration), DurationSeconds);
	str_format(pBuf, BufSize, "[%s] %s/%s", aBar, aElapsed, aDuration);
	return true;
}

bool GetMusicResultIndex(const CMusicState &Music, int ClientId, const char *pValue, int *pIndex)
{
	char aBuf[VOTE_DESC_LENGTH];
	const int ResultCount = Music.VoteMenuSearchResultCount(ClientId);
	for(int i = 0; i < ResultCount; ++i)
	{
		SongInfo Song;
		if(!Music.GetVoteMenuSearchResult(ClientId, i, &Song))
			continue;

		FormatSongOption(aBuf, sizeof(aBuf), "", i, Song);
		if(str_comp_nocase(pValue, aBuf) == 0)
		{
			if(pIndex)
				*pIndex = i;
			return true;
		}
	}

	return false;
}

}

int CGameContext::GetMusicVoteOptionCount(int ClientId) const
{
	int Count = 1; // search action
	Count += 1; // now playing header
	if(m_Music.HasCurrentQueueSong())
	{
		Count += 1; // progress
		if(m_Music.QueueSize() > 1)
			Count += 1; // skip action
	}

	Count += 1; // queue header
	if(m_Music.QueueEmpty())
		Count += 1;
	else
	{
		const int NumShown = std::min<int>(m_Music.QueueSize(), g_Config.m_SvMusicPlaylistVisible);
		Count += NumShown;
		if(m_Music.QueueSize() > NumShown)
			Count += 1;
	}

	Count += 1; // search results header
	Count += m_Music.VoteMenuSearchResultCount(ClientId);
	return Count;
}

bool CGameContext::GetMusicVoteOption(int ClientId, int Index, char *pBuf, int BufSize) const
{
	if(Index < 0)
		return false;

	if(Index == 0)
	{
		str_copy(pBuf, MUSIC_SEARCH_ACTION, BufSize);
		return true;
	}
	Index--;

	if(Index == 0)
	{
		str_copy(pBuf, MUSIC_NOW_HEADER, BufSize);
		return true;
	}
	Index--;

	if(m_Music.HasCurrentQueueSong())
	{
		if(Index == 0)
		{
			if(!FormatNowPlayingProgress(m_Music, pBuf, BufSize))
				str_copy(pBuf, "[                  ] 00:00/00:00", BufSize);
			return true;
		}
		Index--;

		if(m_Music.QueueSize() > 1)
		{
			if(Index == 0)
			{
				str_copy(pBuf, MUSIC_SKIP_ACTION, BufSize);
				return true;
			}
			Index--;
		}
	}

	if(Index == 0)
	{
		str_copy(pBuf, MUSIC_QUEUE_HEADER, BufSize);
		return true;
	}
	Index--;

	if(m_Music.QueueEmpty())
	{
		if(Index == 0)
		{
			str_copy(pBuf, MUSIC_QUEUE_EMPTY, BufSize);
			return true;
		}
		Index--;
	}
	else
	{
		const int NumShown = std::min<int>(m_Music.QueueSize(), g_Config.m_SvMusicPlaylistVisible);
		if(Index < NumShown)
		{
			const SongInfo *pSong = m_Music.GetQueuedSong(Index);
			if(!pSong)
				return false;
			FormatSongOption(pBuf, BufSize, "Q", Index, *pSong);
			return true;
		}
		Index -= NumShown;

		if(m_Music.QueueSize() > NumShown)
		{
			if(Index == 0)
			{
				str_copy(pBuf, MUSIC_QUEUE_MORE, BufSize);
				return true;
			}
			Index--;
		}
	}

	if(Index == 0)
	{
		str_copy(pBuf, MUSIC_SEARCH_HEADER, BufSize);
		return true;
	}
	Index--;

	const int ResultCount = m_Music.VoteMenuSearchResultCount(ClientId);
	if(Index < ResultCount)
	{
		SongInfo Song;
		if(!m_Music.GetVoteMenuSearchResult(ClientId, Index, &Song))
			return false;
		FormatSongOption(pBuf, BufSize, "", Index, Song);
		return true;
	}

	return false;
}

bool CGameContext::GetVoteOptionDescriptionForClient(int ClientId, int Index, char *pBuf, int BufSize) const
{
	const int MusicCount = GetMusicVoteOptionCount(ClientId);
	if(Index < MusicCount)
		return GetMusicVoteOption(ClientId, Index, pBuf, BufSize);

	const CVoteOptionServer *pOption = GetVoteOption(Index - MusicCount);
	if(!pOption)
		return false;

	str_copy(pBuf, pOption->m_aDescription, BufSize);
	return true;
}

void CGameContext::RefreshVoteOptions(int ClientId)
{
	if(ClientId < 0)
	{
		CNetMsg_Sv_VoteClearOptions ClearMsg;
		Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, -1);

		for(auto &pPlayer : m_apPlayers)
		{
			if(pPlayer)
				pPlayer->m_SendVoteIndex = 0;
		}
		return;
	}

	if(!m_apPlayers[ClientId])
		return;

	CNetMsg_Sv_VoteClearOptions ClearMsg;
	Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientId);
	m_apPlayers[ClientId]->m_SendVoteIndex = 0;
}

void CGameContext::RefreshMusicVoteMenus(int ClientId)
{
	RefreshVoteOptions(ClientId);
}

bool CGameContext::IsMusicSearchVoteOption(const char *pValue) const
{
	return str_comp_nocase(pValue, MUSIC_SEARCH_ACTION) == 0;
}

bool CGameContext::IsMusicSkipVoteOption(const char *pValue) const
{
	return str_comp_nocase(pValue, MUSIC_SKIP_ACTION) == 0;
}

bool CGameContext::IsMusicMenuVoteOption(int ClientId, const char *pValue) const
{
	char aBuf[VOTE_DESC_LENGTH];
	const int Count = GetMusicVoteOptionCount(ClientId);
	for(int i = 0; i < Count; ++i)
	{
		if(!GetMusicVoteOption(ClientId, i, aBuf, sizeof(aBuf)))
			continue;
		if(str_comp_nocase(pValue, aBuf) == 0)
			return true;
	}
	return false;
}

bool CGameContext::HandleMusicSearchVoteOption(int ClientId, const char *pReason)
{
	if(!m_apPlayers[ClientId])
		return true;

	if(!pReason || !pReason[0])
	{
		SendChatTarget(ClientId, "请在投票理由中输入歌名");
		return true;
	}

	const NETADDR *pAddr = Server()->ClientAddr(ClientId);

	const int64_t Now = time_timestamp();
	int SecondsLeft = 0;
	if(m_Music.SongSearchInGlobalCooldown(Now, g_Config.m_SvMusicGlobalCooldown, &SecondsLeft))
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "切歌后需等待 %d 秒才能搜索新歌曲", SecondsLeft);
		SendChatTarget(ClientId, aBuf);
		return true;
	}

	if(m_SongCooldowns.IsCooldown(pAddr))
	{
		SecondsLeft = m_SongCooldowns.GetSecondsLeft(pAddr);
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "请等待 %d 秒后再搜索歌曲", SecondsLeft);
		SendChatTarget(ClientId, aBuf);
		return true;
	}

	m_SongCooldowns.SetCooldown(pAddr, g_Config.m_SvMusicPlayerCooldown);
	RequestSongSearch(ClientId, pReason, true);
	return true;
}

bool CGameContext::HandleMusicSkipVoteOption(int ClientId, const char *pReason)
{
	if(!m_Music.IsPlayingFromQueue() || m_Music.QueueEmpty())
	{
		SendChatTarget(ClientId, "当前没有播放队列中的歌曲");
		return true;
	}

	if(m_Music.QueueSize() <= 1)
	{
		SendChatTarget(ClientId, "队列中没有下一首歌曲可以跳过");
		return true;
	}

	SongInfo *pNextSong = GetQueuedSong(1);
	if(!pNextSong || !pNextSong->isReady)
	{
		SendChatTarget(ClientId, "下一首歌曲尚未准备好，无法跳过");
		return true;
	}

	SongInfo *pCurrentSong = GetQueuedSong(0);
	if(!pCurrentSong)
	{
		SendChatTarget(ClientId, "无法获取当前歌曲信息");
		return true;
	}

	char aVoteDesc[VOTE_DESC_LENGTH];
	str_format(aVoteDesc, sizeof(aVoteDesc), "跳过当前歌曲 '%s - %s'",
		pCurrentSong->title.c_str(), pCurrentSong->artist.c_str());

	char aVoteCmd[VOTE_CMD_LENGTH];
	str_copy(aVoteCmd, "queue_skip", sizeof(aVoteCmd));

	char aChatMsg[512];
	str_format(aChatMsg, sizeof(aChatMsg), "'%s' 发起投票跳过当前歌曲",
		Server()->ClientName(ClientId));

	CallVote(ClientId, aVoteDesc, aVoteCmd, pReason && pReason[0] ? pReason : "跳过当前歌曲", aChatMsg, aVoteDesc);
	return true;
}

bool CGameContext::HandleMusicChooseVoteOption(int ClientId, const char *pValue, const char *pReason)
{
	int Index = -1;
	if(!GetMusicResultIndex(m_Music, ClientId, pValue, &Index))
		return false;

	SongInfo Song;
	if(!m_Music.GetVoteMenuSearchResult(ClientId, Index, &Song))
	{
		SendChatTarget(ClientId, "搜索结果已过期，请重新搜索");
		RefreshMusicVoteMenus(ClientId);
		return true;
	}
	Song.requesterName = Server()->ClientName(ClientId);
	Song.requesterSource = "game";
	Song.requesterId = Song.requesterName;

	char aVoteDesc[VOTE_DESC_LENGTH];
	str_format(aVoteDesc, sizeof(aVoteDesc), "将 '%s - %s' 添加到播放队列",
		Song.title.c_str(), Song.artist.c_str());

	const int VoteId = m_Music.AddPendingSongVote(Song);

	char aVoteCmd[VOTE_CMD_LENGTH];
	str_format(aVoteCmd, sizeof(aVoteCmd), "download_song %d", VoteId);

	char aChatMsg[512];
	str_format(aChatMsg, sizeof(aChatMsg), "'%s' 发起投票将歌曲添加到播放队列",
		Server()->ClientName(ClientId));

	CallVote(ClientId, aVoteDesc, aVoteCmd, pReason && pReason[0] ? pReason : "添加到播放队列", aChatMsg, aVoteDesc);

	m_Music.ClearVoteMenuSearchResults(ClientId);
	RefreshMusicVoteMenus(ClientId);
	return true;
}
