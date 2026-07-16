/* Music playback scheduling and skip voting. */
#include "gamecontext.h"
#include "music_config.h"

#include <base/hash.h>
#include <base/system.h>
#include <engine/console.h>
#include <engine/server.h>
#include <engine/storage.h>
#include <engine/shared/config.h>

#include <string>

namespace
{

bool CopyAbsoluteFile(const char *pSourcePath, const char *pTargetPath)
{
	IOHANDLE SrcFile = io_open(pSourcePath, IOFLAG_READ);
	if(!SrcFile)
		return false;

	if(fs_makedir_rec_for(pTargetPath) != 0)
	{
		io_close(SrcFile);
		return false;
	}

	char aTempPath[IO_MAX_PATH_LENGTH];
	str_format(aTempPath, sizeof(aTempPath), "%s.copytmp", pTargetPath);
	IOHANDLE DstFile = io_open(aTempPath, IOFLAG_WRITE);
	if(!DstFile)
	{
		io_close(SrcFile);
		return false;
	}

	char aBuffer[64 * 1024];
	bool Success = true;
	while(true)
	{
		const unsigned BytesRead = io_read(SrcFile, aBuffer, sizeof(aBuffer));
		if(BytesRead == 0)
			break;
		if(io_write(DstFile, aBuffer, BytesRead) != BytesRead)
		{
			Success = false;
			break;
		}
	}

	io_close(DstFile);
	io_close(SrcFile);

	if(!Success)
	{
		fs_remove(aTempPath);
		return false;
	}

	if(fs_rename(aTempPath, pTargetPath) != 0)
	{
		fs_remove(aTempPath);
		return false;
	}
	return true;
}

}

void CGameContext::InitializeQueuePlayback()    
{    
    if(m_Music.QueueEmpty())    
    {    
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Cannot initialize: queue is empty");    
        return;    
    }    
        
    // 重置播放状态  
    m_Music.ResetPlayback();
        
    // 开始预加载第一首歌    
    StartPreloadingSong(0);    
        
    Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Queue playback initialized, starting first song preload");    
}

SongInfo* CGameContext::GetQueuedSong(int Index)  
{  
    return m_Music.GetQueuedSong(Index);  
}

void CGameContext::BeginQueueSongPlayback(int QueueIndex, const SongInfo &Song, int64_t StartTime, bool ReloadMap, bool AutoStartLyrics)
{
    const int64_t EffectiveStartTime = ReloadMap ? 0 : StartTime;
    m_Music.BeginQueueSong(QueueIndex, Song, EffectiveStartTime);
    SavePlaylistToFile();

    LoadLyrics(m_Music.CurrentSongId());
    if(AutoStartLyrics && m_Music.HasLyrics())
        StartLyrics(false);

    if(ReloadMap)
        Console()->ExecuteLine("hot_reload");
}

bool CGameContext::InstallPreparedMusicMap(const char *pPreparedMapPath)
{
	if(!pPreparedMapPath || !pPreparedMapPath[0])
		return false;

	if(fs_is_relative_path(pPreparedMapPath))
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Prepared music map path must be absolute");
		return false;
	}

	if(!fs_is_file(pPreparedMapPath))
	{
		char aBuf[IO_MAX_PATH_LENGTH + 64];
		str_format(aBuf, sizeof(aBuf), "Prepared music map does not exist: %s", pPreparedMapPath);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return false;
	}

	char aTargetMapPath[IO_MAX_PATH_LENGTH];
	str_format(aTargetMapPath, sizeof(aTargetMapPath), "maps/%s.map", Server()->GetMapName());

	char aTargetAbsolute[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, aTargetMapPath, aTargetAbsolute, sizeof(aTargetAbsolute));
	if(str_comp(pPreparedMapPath, aTargetAbsolute) == 0)
	{
		// The backend wrote the complete map directly to the target path.
		return true;
	}

	const char *pPreparedFileName = fs_filename(pPreparedMapPath);
	char aExpectedFileName[IO_MAX_PATH_LENGTH];
	str_format(aExpectedFileName, sizeof(aExpectedFileName), "%s.map", Server()->GetMapName());
	if(str_comp(pPreparedFileName, aExpectedFileName) == 0)
	{
		if(!fs_is_file(aTargetAbsolute) && !CopyAbsoluteFile(pPreparedMapPath, aTargetAbsolute))
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Backend wrote final music map, but TYPE_SAVE copy failed");
			return false;
		}
		return true;
	}

	if(fs_makedir_rec_for(aTargetAbsolute) != 0)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Failed to create target map directory");
		return false;
	}

	char aBackupAbsolute[IO_MAX_PATH_LENGTH];
	str_format(aBackupAbsolute, sizeof(aBackupAbsolute), "%s.musicbak", aTargetAbsolute);
	fs_remove(aBackupAbsolute);

	const bool HadTarget = fs_is_file(aTargetAbsolute);
	if(HadTarget && fs_rename(aTargetAbsolute, aBackupAbsolute) != 0)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Failed to backup current map before installing music map");
		return false;
	}

	if(fs_rename(pPreparedMapPath, aTargetAbsolute) != 0)
	{
		if(HadTarget)
			fs_rename(aBackupAbsolute, aTargetAbsolute);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Failed to install prepared music map");
		return false;
	}

	if(HadTarget)
		fs_remove(aBackupAbsolute);
	return true;
}

void CGameContext::ProcessPreloadedSong(const SongInfo &Song, float Duration, int QueueIndex, const char *pMapName, const char *pPreparedMapPath, const char *pMapSha256)
{  
    if(!m_Music.IsQueuedSong(QueueIndex, Song))
    {
        m_Music.FinishPreloading(Song.page_url);
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Ignoring stale preload result for song no longer in queue");
        return;
    }

	if(!pMapName || str_comp(pMapName, Server()->GetMapName()) != 0)
	{
		m_Music.FinishPreloading(Song.page_url);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Ignoring preload result for a different map");
		return;
	}

	if(!InstallPreparedMusicMap(pPreparedMapPath))
    {  
        m_Music.FinishPreloading(Song.page_url);
        HandlePreloadFailure(QueueIndex, "Failed to install backend-prepared music map");
        return;  
    }

	if(pMapSha256 && pMapSha256[0])
	{
		char aTargetMapPath[IO_MAX_PATH_LENGTH];
		str_format(aTargetMapPath, sizeof(aTargetMapPath), "maps/%s.map", Server()->GetMapName());

		char aTargetAbsolute[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, aTargetMapPath, aTargetAbsolute, sizeof(aTargetAbsolute));

		SHA256_DIGEST TargetSha256;
		char aActualSha256[SHA256_MAXSTRSIZE];
		if(!Storage()->CalculateHashes(aTargetAbsolute, IStorage::TYPE_ABSOLUTE, &TargetSha256) ||
			(sha256_str(TargetSha256, aActualSha256, sizeof(aActualSha256)), str_comp_nocase(aActualSha256, pMapSha256) != 0))
		{
			m_Music.FinishPreloading(Song.page_url);
			HandlePreloadFailure(QueueIndex, "Installed music map SHA256 mismatch");
			return;
		}

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Installed backend-prepared music map sha256=%s", pMapSha256);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}

    // 更新队列中的歌曲信息  
    UpdateSongInQueue(QueueIndex, Duration);  
          
    // 如果这是第一首歌且当前没有播放任何歌曲，立即开始播放  
    if(QueueIndex == 0 && !m_Music.IsPlayingFromQueue())
    {  
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "First song ready, starting playback immediately");  
        SongInfo CurrentSong = Song;
        CurrentSong.duration = Duration;
        BeginQueueSongPlayback(0, CurrentSong, time_timestamp(), true, true);
          
        char aBuf[256];  
        str_format(aBuf, sizeof(aBuf), "开始播放: '%s - %s'", Song.title.c_str(), Song.artist.c_str());  
        SendChatTarget(-1, aBuf);  
    }  
      
	m_Music.FinishPreloading(Song.page_url);  
}

void CGameContext::CheckSongTransition()    
{    
    // 如果没有启用队列播放，直接返回    
    if(m_Music.QueueEmpty())
    {
        if(m_Music.IsPlayingFromQueue())
        {
            Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Queue became empty during playback, stopping");
            m_Music.StopPlayback();
            SavePlaylistToFile();
        }
        return;
    }

    if(!m_Music.IsPlayingFromQueue())
        return;
        
    // 如果当前没有播放歌曲，尝试开始播放第一首    
    if(m_Music.WaitingForFirstQueueSong())
    {    
        SongInfo *pFirstSong = GetQueuedSong(0);    
        if(pFirstSong && pFirstSong->isReady)    
        {    
            Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Starting first song in queue");    
            PlayNextSong();    
        }    
        return;    
    }    
        
    // 检查当前歌曲是否播放完毕    
    if(m_Music.CurrentSongDuration() > 0.0f && m_Music.CurrentSongStartTime() > 0)
    {    
        int64_t Now = time_timestamp();    
        float ElapsedSeconds = (float)(Now - m_Music.CurrentSongStartTime());
            
        if(ElapsedSeconds >= m_Music.CurrentSongDuration() * ((float)g_Config.m_SvMusicNextPreloadPercent / 100.0f))
        {    
            int NextIndex = m_Music.NextQueueIndex();
            if(NextIndex < m_Music.QueueSize())    
            {    
                SongInfo *pNextSong = GetQueuedSong(NextIndex);    
                
                if(pNextSong && !pNextSong->isPreloaded)    
                {  
                    char aBuf[128];
                    str_format(aBuf, sizeof(aBuf), "Starting preload for next song (%d%% progress)", g_Config.m_SvMusicNextPreloadPercent);
                    Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);    
                    StartPreloadingSong(NextIndex);  
                }    
            }    
        }   
            
        // 检查当前歌曲是否播放完毕    
        if(ElapsedSeconds >= m_Music.CurrentSongDuration())
        {    
            Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Current song finished, switching to next");    
            PlayNextSong();    
        }    
    }    
}

void CGameContext::PlayNextSong()  
{  
    if(m_Music.QueueEmpty())  
    {  
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Queue is empty, stopping playback");  
        m_Music.StopPlayback();
        return;  
    }  
      
    // 如果是第一首歌  
    if(m_Music.WaitingForFirstQueueSong())
    {  
        SongInfo *pFirstSong = GetQueuedSong(0);  
        if(!pFirstSong || !pFirstSong->isReady)  
        {  
            Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "First song not ready yet");  
            return;  
        }  
          
        BeginQueueSongPlayback(0, *pFirstSong, time_timestamp(), true, true);
          
        char aBuf[256];  
        str_format(aBuf, sizeof(aBuf), "Starting first song: '%s - %s' (%.2f seconds)",   
                   pFirstSong->title.c_str(), pFirstSong->artist.c_str(), pFirstSong->duration);  
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);  
          
        // 开始预加载下一首歌  
        if(m_Music.QueueSize() > 1)  
        {  
            StartPreloadingSong(1);  
        }  
          
        return;  
    }  
      
    // 切换到下一首歌  
    int NextIndex = m_Music.NextQueueIndex();
    if(NextIndex >= m_Music.QueueSize())  
    {  
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Reached end of queue");  
        m_Music.ConsumeCurrentSong();
        SavePlaylistToFile();
        SendChatTarget(-1, "播放队列已结束");
        return;  
    }  
      
    SongInfo *pNextSong = GetQueuedSong(NextIndex);  
    if(!pNextSong || !pNextSong->isReady)  
    {  
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Next song not ready, waiting...");  
        if(pNextSong && !pNextSong->isPreloaded)
            StartPreloadingSong(NextIndex);
        return;  
    }  
      
    SongInfo NextSong;
    if(!m_Music.AdvanceToNextSong(&NextSong, time_timestamp()))
    {
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Failed to advance queue to next song");
        return;
    }

    BeginQueueSongPlayback(0, NextSong, m_Music.CurrentSongStartTime(), true, true);
      
    char aBuf[256];  
    str_format(aBuf, sizeof(aBuf), "Switching to next song: '%s - %s' (%.2f seconds)",   
               NextSong.title.c_str(), NextSong.artist.c_str(), NextSong.duration);  
    Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);  
    SendChatTarget(-1, aBuf);  
      
    // 开始预加载下下首歌  
	int NextNextIndex = 1; // 修改：固定为1，因为当前播放的是索引0  
	if(NextNextIndex < m_Music.QueueSize())    
	{    
		StartPreloadingSong(NextNextIndex);    
	}
		
    // 更新歌曲变更时间  
    m_Music.MarkSongChanged(time_timestamp());
}

void CGameContext::RestoreQueuePlaybackState()  
{  
    if(m_Music.NormalizePlaybackState())
        SavePlaylistToFile();
      
    if(m_Music.QueueEmpty())  
    {  
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "No songs in queue to restore");  
        return;  
    }  
      
    // 如果之前正在播放队列中的歌曲  
    if(m_Music.HasCurrentQueueSong())
    {  
        char aBuf[256];  
        str_format(aBuf, sizeof(aBuf), "Resuming queue playback from index %d", m_Music.CurrentSongIndex());
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);  
          
        // 检查当前歌曲是否还在队列中  
        SongInfo *pCurrentSong = GetQueuedSong(m_Music.CurrentSongIndex());
		if(pCurrentSong)    
		{    
            const bool RestartLyrics = m_Music.LyricsActive();
            BeginQueueSongPlayback(m_Music.CurrentSongIndex(), *pCurrentSong, m_Music.CurrentSongStartTime(), false, RestartLyrics);
				
			char aBuf2[256];    
			str_format(aBuf2, sizeof(aBuf2), "Resumed playing: '%s - %s' (Duration: %.2f)",     
					pCurrentSong->title.c_str(), pCurrentSong->artist.c_str(), pCurrentSong->duration);    
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf2);    
		}
        else  
        {  
            Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Current song index invalid, resetting queue playback");  
            m_Music.StopPlayback();
        }  
    }  
    else if(!m_Music.QueueEmpty())  
    {  
        // 如果有歌曲但没有在播放，检查是否需要开始播放  
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Queue has songs but not playing, checking if first song is ready");  
        SongInfo *pFirstSong = GetQueuedSong(0);  
        if(pFirstSong && pFirstSong->isReady)  
        {  
            Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "First song is ready, starting playback");
            PlayNextSong();
        }  
        else if(pFirstSong && !pFirstSong->isPreloaded)  
        {  
            Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "First song not preloaded, starting preload");  
            StartPreloadingSong(0);  
        }  
    }  
}

void CGameContext::ValidateQueueState()  
{  
    if(m_Music.NormalizePlaybackState())
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Queue playback state was normalized");
      
    // 检查时长是否合理  
    if(!m_Music.CurrentSongDurationInRange((float)g_Config.m_SvMusicMaxSongDuration))
    {  
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid song duration detected, resetting");  
        m_Music.StopPlayback();
    }  
      
    char aBuf[256];  
    str_format(aBuf, sizeof(aBuf), "Queue validation complete - Index: %d, Playing: %s, Queue size: %d",   
               m_Music.CurrentSongIndex(), m_Music.IsPlayingFromQueue() ? "true" : "false", m_Music.QueueSize());
    Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);  
}

void CGameContext::HandlePreloadFailure(int QueueIndex, const char *pReason, const SongInfo *pFailedSong)
{  
    char aBuf[256];  
    str_format(aBuf, sizeof(aBuf), "Preload failed for song at index %d: %s", QueueIndex, pReason);  
    Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);  

    if(pFailedSong)
        m_Music.FinishPreloading(pFailedSong->page_url);

    if(pFailedSong && !m_Music.IsQueuedSong(QueueIndex, *pFailedSong))
    {
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Ignoring preload failure for song no longer in queue");
        return;
    }

    // 重置该歌曲的预加载状态  
    SongInfo *pSong = GetQueuedSong(QueueIndex);  
    if(pSong)  
    {  
        m_Music.FinishPreloading(pSong->page_url);
        UpdateSongPreloadedStatusInQueue(QueueIndex, false);  

        pSong->preloadFailures++;
        pSong->nextPreloadRetryTime = time_timestamp() + g_Config.m_SvMusicPreloadRetryDelay;

        str_format(aBuf, sizeof(aBuf), "Preload will retry later for '%s - %s' (failure %d/%d)",
            pSong->title.c_str(), pSong->artist.c_str(), pSong->preloadFailures, g_Config.m_SvMusicMaxPreloadFailures);
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

        if(g_Config.m_SvMusicMaxPreloadFailures > 0 && pSong->preloadFailures >= g_Config.m_SvMusicMaxPreloadFailures)
            str_copy(aBuf, "歌曲预加载失败次数过多，已暂停自动重试，请检查音频文件路径", sizeof(aBuf));
        else
            str_format(aBuf, sizeof(aBuf), "歌曲预加载失败，将在 %d 秒后重试", g_Config.m_SvMusicPreloadRetryDelay);

        SendChatTarget(-1, aBuf);
        return;
    }  

    str_copy(aBuf, "歌曲预加载失败，将稍后重试", sizeof(aBuf));
    SendChatTarget(-1, aBuf);  
}

void CGameContext::ConChatSkip(IConsole::IResult *pResult, void *pUserData)      
{      
    CGameContext *pSelf = (CGameContext *)pUserData;      
    int ClientID = pResult->m_ClientId;      
        
    // 检查是否正在播放队列中的歌曲  
    if(!pSelf->m_Music.IsPlayingFromQueue() || pSelf->m_Music.QueueEmpty())
    {  
        pSelf->SendChatTarget(ClientID, "当前没有播放队列中的歌曲");  
        return;  
    }  
      
    // 检查队列中是否有下一首歌  
    if(pSelf->m_Music.QueueSize() <= 1)  
    {  
        pSelf->SendChatTarget(ClientID, "队列中没有下一首歌曲可以跳过");  
        return;  
    }  
      
    // 检查下一首歌是否准备好  
    SongInfo *pNextSong = pSelf->GetQueuedSong(1);  
    if(!pNextSong || !pNextSong->isReady)  
    {  
        pSelf->SendChatTarget(ClientID, "下一首歌曲尚未准备好，无法跳过");  
        return;  
    }  
      
    // 获取当前播放的歌曲信息  
    SongInfo *pCurrentSong = pSelf->GetQueuedSong(0);  
    if(!pCurrentSong)  
    {  
        pSelf->SendChatTarget(ClientID, "无法获取当前歌曲信息");  
        return;  
    }  
      
    // 创建投票描述    
    char aVoteDesc[VOTE_DESC_LENGTH];    
    str_format(aVoteDesc, sizeof(aVoteDesc), "跳过当前歌曲 '%s - %s'",     
               pCurrentSong->title.c_str(), pCurrentSong->artist.c_str());    
        
    // 创建投票命令  
    char aVoteCmd[VOTE_CMD_LENGTH];    
    str_copy(aVoteCmd, "queue_skip", sizeof(aVoteCmd));
        
    // 启动投票    
    char aChatMsg[512];    
    str_format(aChatMsg, sizeof(aChatMsg), "'%s' 发起投票跳过当前歌曲",     
               pSelf->Server()->ClientName(ClientID));    
        
    pSelf->CallVote(ClientID, aVoteDesc, aVoteCmd, "跳过当前歌曲", aChatMsg, aVoteDesc);    
}

