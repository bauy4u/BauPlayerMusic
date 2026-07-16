/* Music lyrics loading and playback. */
#include "gamecontext.h"
#include "music_config.h"

#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <cstdio>
#include <string>
#include <vector>

namespace
{

bool ParseLrcTimestamp(const char *pBegin, const char *pEnd, int TickSpeed, int *pTick)
{
    int Minutes = 0;
    int Seconds = 0;
    if(sscanf(pBegin, "[%d:%d", &Minutes, &Seconds) != 2 || Minutes < 0 || Seconds < 0)
        return false;

    int Milliseconds = 0;
    const char *pDot = str_find(pBegin, ".");
    if(pDot && pDot < pEnd)
    {
        int Fraction = 0;
        int Digits = 0;
        for(const char *pDigit = pDot + 1; pDigit < pEnd && *pDigit >= '0' && *pDigit <= '9' && Digits < 3; ++pDigit)
        {
            Fraction = Fraction * 10 + (*pDigit - '0');
            Digits++;
        }
        if(Digits == 1)
            Milliseconds = Fraction * 100;
        else if(Digits == 2)
            Milliseconds = Fraction * 10;
        else if(Digits == 3)
            Milliseconds = Fraction;
    }

    const int64_t TotalMilliseconds = (int64_t)Minutes * 60000 + Seconds * 1000 + Milliseconds;
    *pTick = (int)(TotalMilliseconds * TickSpeed / 1000);
    return true;
}

}

void CGameContext::LoadLyrics(const std::string& songId)    
{    
    m_Music.ResetLyrics();
        
    char aLrcPath[IO_MAX_PATH_LENGTH];    
    str_format(aLrcPath, sizeof(aLrcPath), MusicConfig::LYRICS_FILE, songId.c_str());    
        
    IOHANDLE File = Storage()->OpenFile(aLrcPath, IOFLAG_READ, IStorage::TYPE_ALL);  
    if(!File)  
    {  
        return; // 静默失败，很多歌可能没有歌词文件  
    }  
  
    CLineReader LineReader;  
    LineReader.OpenFile(File);  
        
    int ParsedCount = 0;    
    const char *pLine;  
        
    while((pLine = LineReader.Get()) != nullptr)  
    {
        std::vector<int> vTicks;
        const char *pCursor = pLine;
        while(*pCursor == '[')
        {
            const char *pClosingBracket = str_find(pCursor, "]");
            if(!pClosingBracket)
                break;

            int Tick = 0;
            if(!ParseLrcTimestamp(pCursor, pClosingBracket, Server()->TickSpeed(), &Tick))
                break;

            vTicks.push_back(Tick);
            pCursor = pClosingBracket + 1;
        }

        while(*pCursor == ' ')
            pCursor++;

        if(!vTicks.empty() && pCursor[0] != '\0')
        {
            for(const int Tick : vTicks)
            {
                m_Music.AddLyricLine(Tick, std::string(pCursor));
                ParsedCount++;
            }
        }
    }

    io_close(File);

    char aBuf[256];    
    str_format(aBuf, sizeof(aBuf), "Loaded %d lyric lines for song %s.", ParsedCount, songId.c_str());    
    Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);    
}

void CGameContext::SaveLyricsState()  
{  
    Storage()->CreateFolder("data", IStorage::TYPE_SAVE);  
    Storage()->CreateFolder(MusicConfig::STATE_DIR, IStorage::TYPE_SAVE);  
      
    const char *pFilePath = MusicConfig::LYRICS_STATE_FILE;  
    IOHANDLE File = Storage()->OpenFile(pFilePath, IOFLAG_WRITE, IStorage::TYPE_SAVE);  
    if(!File)  
    {  
        dbg_msg("lyrics", "Failed to open lyrics state file for writing: %s", pFilePath);  
        return;  
    }  
      
    // 保存当前播放的歌曲ID和状态  
    char aLine[256];  
    str_format(aLine, sizeof(aLine), "%s|%s|%d\n",   
               m_Music.CurrentSongId().c_str(),
               m_Music.LyricsActive() ? "true" : "false",
               m_Music.NextLyricIndex());
      
    io_write(File, aLine, str_length(aLine));  
    io_close(File);  
      
    dbg_msg("lyrics", "Saved lyrics state to file");  
}  
  
void CGameContext::LoadLyricsState()  
{  
    const char *pFilePath = MusicConfig::LYRICS_STATE_FILE;  
    if(!Storage()->FileExists(pFilePath, IStorage::TYPE_SAVE))  
    {  
        return;  
    }  
      
    IOHANDLE File = Storage()->OpenFile(pFilePath, IOFLAG_READ, IStorage::TYPE_SAVE);  
    if(!File)  
    {  
        dbg_msg("lyrics", "Failed to open lyrics state file for reading: %s", pFilePath);  
        return;  
    }  
      
    CLineReader LineReader;  
    LineReader.OpenFile(File);  
      
    const char *pLine = LineReader.Get();  
    if(pLine)  
    {  
        char aBuffer[1024];  
        str_copy(aBuffer, pLine, sizeof(aBuffer));  
          
        char *pSongId = aBuffer;  
        char *pActive = (char *)str_find(pSongId, "|");  
        if(!pActive)  
        {
            io_close(File);
            return;  
        }
          
        *pActive = '\0';  
        pActive++;  
          
        char *pIndex = (char *)str_find(pActive, "|");  
        if(!pIndex)  
        {
            io_close(File);
            return;  
        }
          
        *pIndex = '\0';  
        pIndex++;  
          
        // 恢复歌词状态  
        LoadLyrics(std::string(pSongId));  
        if(str_comp(pActive, "true") == 0)  
        {  
            StartLyrics(false);
            m_Music.SetNextLyricIndex(str_toint(pIndex));
        }  
    }  

    io_close(File);

    dbg_msg("lyrics", "Loaded lyrics state from file");  
}

void CGameContext::CheckAndSendLyrics()  
{  
    std::string Text;
    while(m_Music.PopDueLyric(Server()->Tick(), &Text))
    {
        SendBroadcast(Text.c_str(), -1, true);
    }
}  
  
void CGameContext::StartLyrics(bool Announce)
{    
    if(m_Music.BeginLyricsDisplay(Server()->Tick(), Server()->TickSpeed(), time_timestamp()))
    {    
        if(Announce)
            SendChatTarget(-1, "歌词显示已开始");
    }    
}
  
void CGameContext::StopLyrics()  
{  
    m_Music.StopLyricsDisplay();
    SendChatTarget(-1, "歌词显示已停止");  
}

void CGameContext::ConStartLyrics(IConsole::IResult *pResult, void *pUserData)  
{  
    CGameContext *pSelf = (CGameContext *)pUserData;  
    pSelf->StartLyrics();  
}  
  
void CGameContext::ConStopLyrics(IConsole::IResult *pResult, void *pUserData)  
{  
    CGameContext *pSelf = (CGameContext *)pUserData;  
    pSelf->StopLyrics();  
}  
  
void CGameContext::ConLoadLyrics(IConsole::IResult *pResult, void *pUserData)  
{  
    CGameContext *pSelf = (CGameContext *)pUserData;  
    if(pResult->NumArguments() >= 1)  
    {  
        pSelf->LoadLyrics(pResult->GetString(0));  
    }  
}

