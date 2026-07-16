/* Music queue persistence and administration. */
#include "gamecontext.h"
#include "music_config.h"
#include "music_playlist.h"

#include <algorithm>
#include <engine/shared/config.h>

void CGameContext::AddToPlaylist(const SongInfo &Song)    
{    
    m_Music.AddQueuedSong(Song);    
        
    char aBuf[256];    
    str_format(aBuf, sizeof(aBuf), "歌曲 '%s - %s' 已添加到播放队列，点歌者: %s (队列长度: %d)",
               Song.title.c_str(), Song.artist.c_str(),
               Song.requesterName.empty() ? "未知" : Song.requesterName.c_str(), m_Music.QueueSize());
    SendChatTarget(-1, aBuf);    
      
    // 保存队列到文件  
    SavePlaylistToFile();  
    RequestMusicHistoryRecord(Song);
}
  
void CGameContext::ShowPlaylist(int ClientID)    
{    
    if(m_Music.QueueEmpty())    
    {    
        SendChatTarget(ClientID, "播放队列为空");    
        return;    
    }    
        
    SendChatTarget(ClientID, "=== 播放队列 ===");    
      
    // 显示当前播放状态  
    if(m_Music.HasCurrentQueueSong())
    {  
        char aStatusBuf[128];  
        str_format(aStatusBuf, sizeof(aStatusBuf), "当前播放: 第 %d 首歌曲", m_Music.CurrentSongIndex() + 1);
        SendChatTarget(ClientID, aStatusBuf);  
    }  
        
    const int NumShown = std::min<int>(m_Music.QueueSize(), g_Config.m_SvMusicPlaylistVisible);
    for(int i = 0; i < NumShown; ++i)
    {    
        const int Index = i + 1;
        const SongInfo &Song = *m_Music.GetQueuedSong(i);
        char aBuf[256];  
          
        // 显示歌曲状态  
        const char *pStatus = "";  
        if(m_Music.IsCurrentQueueIndex(Index - 1))
            pStatus = " [正在播放]";  
        else if(Song.isReady)  
            pStatus = " [已准备]";  
        else if(Song.isPreloaded)  
            pStatus = " [预加载中]";  
              
        str_format(aBuf, sizeof(aBuf), "%d. %s - %s | 点歌: %s%s",
                   Index, Song.title.c_str(), Song.artist.c_str(),
                   Song.requesterName.empty() ? "未知" : Song.requesterName.c_str(), pStatus);
        SendChatTarget(ClientID, aBuf);    
    }    
        
    if(m_Music.QueueSize() > g_Config.m_SvMusicPlaylistVisible)
    {    
        char aBuf[64];    
        str_format(aBuf, sizeof(aBuf), "... 还有 %d 首歌曲",     
                   m_Music.QueueSize() - g_Config.m_SvMusicPlaylistVisible);
        SendChatTarget(ClientID, aBuf);    
    }    
}

void CGameContext::ConChatMls(IConsole::IResult *pResult, void *pUserData)  
{  
    CGameContext *pSelf = (CGameContext *)pUserData;  
    int ClientID = pResult->m_ClientId;  
      
    pSelf->ShowPlaylist(ClientID);  
}

void CGameContext::LoadPlaylistFromFile()  
{  
    const char *pFilePath = MusicConfig::PLAYLIST_FILE;  

    const MusicPlaylistStorage::CLoadResult Result = MusicPlaylistStorage::Load(Storage(), pFilePath, &m_Music);
    if(!Result.m_FileExists)
    {  
        dbg_msg("playlist", "Playlist file does not exist: %s", pFilePath);  
        return;  
    }  

    if(!Result.m_Loaded)
    {  
        dbg_msg("playlist", "Failed to open playlist file for reading: %s", pFilePath);  
        return;  
    }  

    if(Result.m_SkippedLines > 0)
        dbg_msg("playlist", "Loaded %d songs and skipped %d malformed lines from playlist file", Result.m_LoadedSongs, Result.m_SkippedLines);
    else
        dbg_msg("playlist", "Loaded %d songs and queue state from playlist file", Result.m_LoadedSongs);
}

void CGameContext::ConQueueSearch(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const char *pSongName = pResult->GetString(0);
	if(!pSongName || !pSongName[0])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "queue_search", "Usage: queue_search <song-name>");
		return;
	}

	pSelf->m_Music.ClearSearchResults(ADMIN_MUSIC_SEARCH_CLIENT_ID);
	pSelf->RequestSongSearch(ADMIN_MUSIC_SEARCH_CLIENT_ID, pSongName);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "queue_search", "Searching songs...");
}

void CGameContext::ConQueueList(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	if(pSelf->m_Music.QueueEmpty())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "queue_list", "Queue is empty");
		return;
	}

	for(int Index = 0; Index < pSelf->m_Music.QueueSize(); Index++)
	{
		const SongInfo *pSong = pSelf->m_Music.GetQueuedSong(Index);
		if(!pSong)
			continue;
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "%d. %s - %s | requester: %s%s",
			Index + 1, pSong->title.c_str(), pSong->artist.c_str(),
			pSong->requesterName.empty() ? "unknown" : pSong->requesterName.c_str(),
			pSelf->m_Music.IsCurrentQueueIndex(Index) ? " [playing]" : "");
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "queue_list", aBuf);
	}
}

void CGameContext::ConQueueRemove(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int QueueNumber = pResult->GetInteger(0);
	const int QueueIndex = QueueNumber - 1;
	if(QueueIndex < 0 || QueueIndex >= pSelf->m_Music.QueueSize())
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Invalid queue number. Use 1 through %d from queue_list.", pSelf->m_Music.QueueSize());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "queue_remove", aBuf);
		return;
	}
	if(pSelf->m_Music.IsCurrentQueueIndex(QueueIndex))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "queue_remove", "The playing song is protected; use queue_skip to change it.");
		return;
	}
	SongInfo RemovedSong;
	if(!pSelf->m_Music.RemoveQueuedSong(QueueIndex, &RemovedSong))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "queue_remove", "Queue changed before removal; run queue_list again.");
		return;
	}
	pSelf->SavePlaylistToFile();
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "Removed queue item %d: '%s - %s'.", QueueNumber, RemovedSong.title.c_str(), RemovedSong.artist.c_str());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "queue_remove", aBuf);
	pSelf->SendChatTarget(-1, "管理员移除了队列中的一首歌曲。");
}

void CGameContext::ConQueueInsertAfter(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int AfterQueueNumber = pResult->GetInteger(0);
	const int SearchResultNumber = pResult->GetInteger(1);
	if(AfterQueueNumber < 1 || AfterQueueNumber > pSelf->m_Music.QueueSize())
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Invalid queue number. Use 1 through %d from /mls or queue_status.", pSelf->m_Music.QueueSize());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "queue_insert_after", aBuf);
		return;
	}

	SongInfo Song;
	if(SearchResultNumber < 1 || !pSelf->m_Music.GetSearchResult(ADMIN_MUSIC_SEARCH_CLIENT_ID, SearchResultNumber - 1, &Song))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "queue_insert_after", "No such admin search result. Run queue_search <song-name> first.");
		return;
	}

	Song.requesterName = "管理员";
	Song.requesterSource = "admin";
	Song.requesterId = "admin";
	Song.duration = 0.0f;
	Song.isPreloaded = false;
	Song.isReady = false;
	Song.preloadFailures = 0;
	Song.nextPreloadRetryTime = 0;

	const int InsertedIndex = pSelf->m_Music.InsertQueuedSongAfter(AfterQueueNumber - 1, Song);
	if(InsertedIndex < 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "queue_insert_after", "Queue changed before insertion; try again.");
		return;
	}

	pSelf->m_Music.ClearSearchResults(ADMIN_MUSIC_SEARCH_CLIENT_ID);
	pSelf->SavePlaylistToFile();
	pSelf->RequestMusicHistoryRecord(Song);

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "管理员插队：'%s - %s' 已插入第 %d 首之后（新位置：第 %d 首）",
		Song.title.c_str(), Song.artist.c_str(), AfterQueueNumber, InsertedIndex + 1);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "queue_insert_after", aBuf);
	pSelf->SendChatTarget(-1, aBuf);
}

void CGameContext::SavePlaylistToFile()    
{    
    const char *pFilePath = MusicConfig::PLAYLIST_FILE;    
    if(!MusicPlaylistStorage::Save(Storage(), pFilePath, MusicConfig::STATE_DIR, m_Music))
    {    
        dbg_msg("playlist", "Failed to open playlist file for writing: %s", pFilePath);    
        return;    
    }    

    dbg_msg("playlist", "Saved %d songs and queue state to playlist file", m_Music.QueueSize());    
    RefreshMusicVoteMenus(-1);
}

void CGameContext::UpdateSongInQueue(int Index, float Duration)  
{  
    SongInfo *pSong = m_Music.GetQueuedSong(Index);
    if(!pSong)
    {  
        char aBuf[128];  
        str_format(aBuf, sizeof(aBuf), "Invalid queue index for update: %d", Index);  
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);  
        return;  
    }  
      
    m_Music.UpdateQueuedSongDuration(Index, Duration);

    char aBuf[256];
    str_format(aBuf, sizeof(aBuf), "Updated song at index %d: '%s - %s' duration=%.2f, ready=true",
               Index, pSong->title.c_str(), pSong->artist.c_str(), Duration);
    Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

    SavePlaylistToFile();
          
    // 如果更新的是第一首歌且还没开始播放，尝试开始播放  
    if(Index == 0 && m_Music.WaitingForFirstQueueSong() && m_Music.IsPlayingFromQueue())
    {  
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "First song ready, starting playback");  
        PlayNextSong();  
    }  
}

void CGameContext::UpdateSongPreloadedStatusInQueue(int Index, bool IsPreloaded)
{
    if(!m_Music.SetQueuedSongPreloaded(Index, IsPreloaded))
        return;

    SavePlaylistToFile();
}

void CGameContext::ConQueueStatus(IConsole::IResult *pResult, void *pUserData)  
{  
    CGameContext *pSelf = (CGameContext *)pUserData;  
      
    char aBuf[512];  
    str_format(aBuf, sizeof(aBuf), "Queue Status - Size: %d, Current: %d, Playing: %s, Duration: %.2f",  
               pSelf->m_Music.QueueSize(), pSelf->m_Music.CurrentSongIndex(),
               pSelf->m_Music.IsPlayingFromQueue() ? "true" : "false", pSelf->m_Music.CurrentSongDuration());
    pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);  
      
    // 验证队列状态  
    pSelf->ValidateQueueState();  
}  
  
void CGameContext::ConQueueClear(IConsole::IResult *pResult, void *pUserData)  
{  
    CGameContext *pSelf = (CGameContext *)pUserData;  
      
    // 清空队列  
    pSelf->m_Music.ClearQueue();  
      
    // 保存到文件  
    pSelf->SavePlaylistToFile();  
      
    pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Queue cleared successfully");  
    pSelf->SendChatTarget(-1, "播放队列已清空");  
}

void CGameContext::ConQueueSkip(IConsole::IResult *pResult, void *pUserData)  
{  
    CGameContext *pSelf = (CGameContext *)pUserData;  
      
    if(!pSelf->m_Music.IsPlayingFromQueue() || pSelf->m_Music.QueueEmpty())
    {  
        pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "No song currently playing from queue");  
        pSelf->SendChatTarget(-1, "当前没有正在播放的队列歌曲");  
        return;  
    }  
      
    pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Manually skipping current song");  
    pSelf->SendChatTarget(-1, "管理员跳过了当前歌曲");  
      
    // 强制切换到下一首歌  
    pSelf->PlayNextSong();  
}  
  
void CGameContext::ConQueueRestart(IConsole::IResult *pResult, void *pUserData)  
{  
    CGameContext *pSelf = (CGameContext *)pUserData;  
      
    if(pSelf->m_Music.QueueEmpty())  
    {  
        pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Queue is empty, cannot restart");  
        pSelf->SendChatTarget(-1, "播放队列为空，无法重启");  
        return;  
    }  
      
    // 重置播放状态  
    pSelf->m_Music.ResetPlayback();
      
    pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Restarting queue playback");  
    pSelf->SendChatTarget(-1, "重新开始播放队列");  
      
    // 开始播放第一首歌  
    pSelf->InitializeQueuePlayback();  
}

void CGameContext::LogQueueState(const char *pContext)  
{  
    char aBuf[512];  
    str_format(aBuf, sizeof(aBuf), "[%s] Queue State - Size: %d, Index: %d, Playing: %s, Duration: %.2f, StartTime: %lld",  
               pContext, m_Music.QueueSize(), m_Music.CurrentSongIndex(),
               m_Music.IsPlayingFromQueue() ? "true" : "false", m_Music.CurrentSongDuration(), m_Music.CurrentSongStartTime());
    Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);  
      
    // 显示队列中前3首歌的状态  
    const int NumShown = std::min<int>(m_Music.QueueSize(), g_Config.m_SvMusicQueueLogPreview);
    for(int Index = 0; Index < NumShown; ++Index)
    {  
        const SongInfo &Song = *m_Music.GetQueuedSong(Index);
        str_format(aBuf, sizeof(aBuf), "  [%d] %s - %s (%.2fs, preloaded:%s, ready:%s)",  
                   Index, Song.title.c_str(), Song.artist.c_str(), Song.duration,  
                   Song.isPreloaded ? "Y" : "N", Song.isReady ? "Y" : "N");  
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);  
    }  
}

