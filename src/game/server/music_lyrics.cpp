/* Music lyrics loading and playback. */
#include "gamecontext.h"
#include "entities/character.h"
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

bool ParseYrcTimestamp(const char *pBegin, const char *pEnd, int TickSpeed, int *pTick)
{
    int StartMilliseconds = 0;
    int DurationMilliseconds = 0;
    if(sscanf(pBegin, "[%d,%d", &StartMilliseconds, &DurationMilliseconds) != 2 || StartMilliseconds < 0 || DurationMilliseconds < 0)
        return false;

    *pTick = (int)((int64_t)StartMilliseconds * TickSpeed / 1000);
    return true;
}

bool IsYrcWordTimestamp(const char *pBegin, const char *pEnd)
{
    int StartMilliseconds = 0;
    int DurationMilliseconds = 0;
    int Reserved = 0;
    return sscanf(pBegin, "(%d,%d,%d", &StartMilliseconds, &DurationMilliseconds, &Reserved) == 3 && pEnd && *pEnd == ')';
}

std::string StripYrcWordTimestamps(const char *pText)
{
    std::string Text;
    for(const char *pCursor = pText; *pCursor;)
    {
        if(*pCursor == '(')
        {
            const char *pClosingParenthesis = str_find(pCursor, ")");
            if(IsYrcWordTimestamp(pCursor, pClosingParenthesis))
            {
                pCursor = pClosingParenthesis + 1;
                continue;
            }
        }
        Text.push_back(*pCursor++);
    }
    return Text;
}

std::vector<int> ExtractYrcCharacterStartOffsets(const char *pText, int LineStartTick, int TickSpeed)
{
    std::vector<int> vOffsets;
    for(const char *pCursor = pText; *pCursor;)
    {
        if(*pCursor != '(')
        {
            ++pCursor;
            continue;
        }

        const char *pClosingParenthesis = str_find(pCursor, ")");
        int StartMilliseconds = 0;
        int DurationMilliseconds = 0;
        int Reserved = 0;
        if(!pClosingParenthesis || sscanf(pCursor, "(%d,%d,%d", &StartMilliseconds, &DurationMilliseconds, &Reserved) != 3)
        {
            ++pCursor;
            continue;
        }

        const char *pWordStart = pClosingParenthesis + 1;
        const char *pWordEnd = str_find(pWordStart, "(");
        if(!pWordEnd)
            pWordEnd = pWordStart + str_length(pWordStart);

        int CharacterCount = 0;
        for(const char *pCharacter = pWordStart; pCharacter < pWordEnd;)
        {
            const char *pNextCharacter = pCharacter;
            if(str_utf8_decode(&pNextCharacter) <= 0 || pNextCharacter > pWordEnd)
                break;
            ++CharacterCount;
            pCharacter = pNextCharacter;
        }
        for(int CharacterIndex = 0; CharacterIndex < CharacterCount; ++CharacterIndex)
        {
            const int CharacterTick = (int)(((int64_t)StartMilliseconds * TickSpeed + (int64_t)DurationMilliseconds * TickSpeed * CharacterIndex / CharacterCount) / 1000);
            vOffsets.push_back(maximum(0, CharacterTick - LineStartTick));
        }

        pCursor = pWordEnd;
    }
    return vOffsets;
}

}

void CGameContext::LoadLyrics(const std::string& songId)    
{    
    m_Music.ResetLyrics();
        
    char aYrcPath[IO_MAX_PATH_LENGTH];
    char aLrcPath[IO_MAX_PATH_LENGTH];    
    str_format(aYrcPath, sizeof(aYrcPath), MusicConfig::YRC_FILE, songId.c_str());
    str_format(aLrcPath, sizeof(aLrcPath), MusicConfig::LYRICS_FILE, songId.c_str());    
        
    IOHANDLE File = Storage()->OpenFile(aYrcPath, IOFLAG_READ, IStorage::TYPE_ALL);
    const bool IsYrc = File != nullptr;
    if(!File)
        File = Storage()->OpenFile(aLrcPath, IOFLAG_READ, IStorage::TYPE_ALL);
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
            if(!(IsYrc ? ParseYrcTimestamp(pCursor, pClosingBracket, Server()->TickSpeed(), &Tick) : ParseLrcTimestamp(pCursor, pClosingBracket, Server()->TickSpeed(), &Tick)))
                break;

            vTicks.push_back(Tick);
            pCursor = pClosingBracket + 1;
            if(IsYrc)
                break;
        }

        while(*pCursor == ' ')
            pCursor++;

        const std::string Text = IsYrc ? StripYrcWordTimestamps(pCursor) : std::string(pCursor);
        const std::vector<int> vCharacterStartOffsets = IsYrc && !vTicks.empty() ? ExtractYrcCharacterStartOffsets(pCursor, vTicks.front(), Server()->TickSpeed()) : std::vector<int>();
        if(!vTicks.empty() && !Text.empty())
        {
            for(const int Tick : vTicks)
            {
                m_Music.AddLyricLine(Tick, Text, vCharacterStartOffsets);
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
    std::vector<int> vCharacterStartOffsets;
    int DurationTicks = Server()->TickSpeed() * 4;
    while(m_Music.PopDueLyric(Server()->Tick(), &Text, &DurationTicks, Server()->TickSpeed() * 4, &vCharacterStartOffsets))
    {
        SendBroadcast(Text.c_str(), -1, true);
        StartLyricChid(Text.c_str(), DurationTicks, vCharacterStartOffsets);
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
    ClearLyricChid();
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

void CGameContext::ConLyricPos(IConsole::IResult *pResult, void *pUserData)
{
    CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
    const int ClientId = pResult->m_ClientId;
    if(ClientId < 0 || ClientId >= MAX_CLIENTS || pSelf->Server()->GetAuthedState(ClientId) != AUTHED_ADMIN)
    {
        if(ClientId >= 0)
            pSelf->SendChatTarget(ClientId, "lyricpos requires administrator access.");
        return;
    }

    CCharacter *pCharacter = pSelf->GetPlayerChar(ClientId);
    if(!pCharacter)
    {
        pSelf->SendChatTarget(ClientId, "Spawn before setting the lyric position.");
        return;
    }

    pSelf->m_LyricChid.m_Pos = pCharacter->m_Pos;
    pSelf->SendChatTarget(ClientId, "Floating lyric position updated.");
}

