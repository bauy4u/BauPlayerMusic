/* Music chat commands and vote entry points. */
#include "gamecontext.h"
#include "music_config.h"

#include <base/system.h>
#include <engine/server.h>
#include <engine/shared/config.h>

#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace
{

void ResetQueueSongRuntimeState(SongInfo *pSong)
{
	pSong->duration = 0.0f;
	pSong->isPreloaded = false;
	pSong->isReady = false;
}

uint32_t FunHash(const char *pText, int64_t Salt)
{
	uint32_t Hash = 2166136261u;
	for(const unsigned char *p = (const unsigned char *)pText; *p; ++p)
	{
		Hash ^= *p;
		Hash *= 16777619u;
	}
	for(int i = 0; i < 8; ++i)
	{
		Hash ^= (unsigned char)((Salt >> (i * 8)) & 0xff);
		Hash *= 16777619u;
	}
	return Hash;
}

const char *LuckComment(int Score)
{
	if(Score >= 95)
		return "今天闪得离谱，适合点一首压轴曲";
	if(Score >= 80)
		return "气势很好，排队点歌大概率很顺";
	if(Score >= 60)
		return "稳稳的，不惊不险，适合循环喜欢的歌";
	if(Score >= 35)
		return "普通但耐听，今天主打慢慢来";
	if(Score >= 15)
		return "有点卡拍，建议先喝口水再点歌";
	return "今天别硬刚，交给 Bot 做选择题吧";
}

bool ParseDiceSpec(const char *pSpec, int *pCount, int *pSides)
{
	std::string Spec = pSpec && pSpec[0] ? pSpec : "100";
	const size_t DPos = Spec.find_first_of("dD");
	try
	{
		if(DPos == std::string::npos)
		{
			*pCount = 1;
			*pSides = std::stoi(Spec);
		}
		else
		{
			*pCount = DPos == 0 ? 1 : std::stoi(Spec.substr(0, DPos));
			*pSides = std::stoi(Spec.substr(DPos + 1));
		}
	}
	catch(...)
	{
		return false;
	}
	return *pCount >= 1 && *pCount <= 20 && *pSides >= 2 && *pSides <= 1000;
}

std::vector<std::string> SplitPickOptions(const char *pText)
{
	std::vector<std::string> vOptions;
	std::string Current;
	for(const char *p = pText; p && *p; ++p)
	{
		if(*p == '|' || *p == '/' || *p == ',' || *p == ';')
		{
			if(!Current.empty())
			{
				vOptions.push_back(Current);
				Current.clear();
			}
		}
		else
		{
			Current.push_back(*p);
		}
	}
	if(!Current.empty())
		vOptions.push_back(Current);

	if(vOptions.size() < 2)
	{
		vOptions.clear();
		std::istringstream Stream(pText ? pText : "");
		std::string Option;
		while(Stream >> Option)
			vOptions.push_back(Option);
	}

	for(std::string &Option : vOptions)
	{
		while(!Option.empty() && Option.front() == ' ')
			Option.erase(Option.begin());
		while(!Option.empty() && Option.back() == ' ')
			Option.pop_back();
	}
	std::vector<std::string> vClean;
	for(const std::string &Option : vOptions)
	{
		if(!Option.empty())
			vClean.push_back(Option);
	}
	return vClean;
}

std::string TrimAscii(std::string Text)
{
	while(!Text.empty() && (Text.front() == ' ' || Text.front() == '\t'))
		Text.erase(Text.begin());
	while(!Text.empty() && (Text.back() == ' ' || Text.back() == '\t'))
		Text.pop_back();
	return Text;
}

int FunRandomInt(int MaxExclusive)
{
	return (int)((uint32_t)secure_rand() % (uint32_t)MaxExclusive);
}

}

void CGameContext::ConChatSong(IConsole::IResult *pResult, void *pUserData)    
{    
    CGameContext *pSelf = (CGameContext *)pUserData;    
    int ClientID = pResult->m_ClientId;    

    if(pResult->NumArguments() < 1)    
    {    
        pSelf->SendChatTarget(ClientID, "用法: /song <歌名>");    
        return;    
    }    
      
    // 获取客户端IP地址  
    const NETADDR *pAddr = pSelf->Server()->ClientAddr(ClientID);  
      
    int64_t Now = time_timestamp();  
    int SecondsLeft = 0;
    if(pSelf->m_Music.SongSearchInGlobalCooldown(Now, g_Config.m_SvMusicGlobalCooldown, &SecondsLeft))
    {  
        char aBuf[128];  
        str_format(aBuf, sizeof(aBuf), "切歌后需等待 %d 秒才能搜索新歌曲", SecondsLeft);  
        pSelf->SendChatTarget(ClientID, aBuf);  
        return;  
    }  
      
    if(pSelf->m_SongCooldowns.IsCooldown(pAddr))  
    {  
        int SecondsLeft = pSelf->m_SongCooldowns.GetSecondsLeft(pAddr);  
        char aBuf[128];  
        str_format(aBuf, sizeof(aBuf), "请等待 %d 秒后再使用 /song 命令", SecondsLeft);  
        pSelf->SendChatTarget(ClientID, aBuf);  
        return;  
    }  
      
    pSelf->m_SongCooldowns.SetCooldown(pAddr, g_Config.m_SvMusicPlayerCooldown);
      
    const char *pSongName = pResult->GetString(0);    
        
    pSelf->RequestSongSearch(ClientID, pSongName);
        
    pSelf->SendChatTarget(ClientID, "正在搜索歌曲...");    
}
  
void CGameContext::ConChatChoose(IConsole::IResult *pResult, void *pUserData)    
{    
    CGameContext *pSelf = (CGameContext *)pUserData;    
    int ClientID = pResult->m_ClientId;    
        
    if(pResult->NumArguments() < 1)    
    {    
        pSelf->SendChatTarget(ClientID, "用法: /choose <编号>");    
        return;    
    }    
        
    int Index = pResult->GetInteger(0) - 1;   
        
    const int ResultCount = pSelf->m_Music.SearchResultCount(ClientID);
    if(ResultCount == 0)
    {    
        pSelf->SendChatTarget(ClientID, "请先使用 /song 搜索歌曲");    
        return;    
    }    
        
    SongInfo Song;
    if(!pSelf->m_Music.GetSearchResult(ClientID, Index, &Song))
    {    
        char aBuf[256];  
        str_format(aBuf, sizeof(aBuf), "无效的编号，请输入 1 到 %d 之间的数字", ResultCount);
        pSelf->SendChatTarget(ClientID, aBuf);    
        return;    
    }    
    Song.requesterName = pSelf->Server()->ClientName(ClientID);
    Song.requesterSource = "game";
    Song.requesterId = Song.requesterName;
      
    char aVoteDesc[VOTE_DESC_LENGTH];  
    str_format(aVoteDesc, sizeof(aVoteDesc), "将 '%s - %s' 添加到播放队列",   
               Song.title.c_str(), Song.artist.c_str());  
      
    const int VoteId = pSelf->m_Music.AddPendingSongVote(Song);

    char aVoteCmd[VOTE_CMD_LENGTH];  
    str_format(aVoteCmd, sizeof(aVoteCmd), "download_song %d", VoteId);
      
    char aChatMsg[512];  
    str_format(aChatMsg, sizeof(aChatMsg), "'%s' 发起投票将歌曲添加到播放队列",   
               pSelf->Server()->ClientName(ClientID));  

    pSelf->CallVote(ClientID, aVoteDesc, aVoteCmd, "添加到播放队列", aChatMsg, aVoteDesc);  
      
    pSelf->m_Music.ClearSearchResults(ClientID);  
}

void CGameContext::ConChatQQMsg(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientID = pResult->m_ClientId;
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientID])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "qqbot", "qqmsg can only be used by a connected player");
		return;
	}
	if(!g_Config.m_SvMusicQQRelay)
	{
		pSelf->SendChatTarget(ClientID, "QQ 群喊话功能未启用");
		return;
	}
	if(pResult->NumArguments() < 1 || !pResult->GetString(0)[0])
	{
		pSelf->SendChatTarget(ClientID, "用法: /qqmsg <内容>");
		return;
	}

	const char *pMessage = pResult->GetString(0);
	size_t MessageSize = 0;
	size_t MessageLength = 0;
	str_utf8_stats(pMessage, str_length(pMessage) + 1, (size_t)-1, &MessageSize, &MessageLength);
	if(MessageLength > (size_t)g_Config.m_SvMusicQQMessageLength)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "喊话过长，最多允许 %d 个字符", g_Config.m_SvMusicQQMessageLength);
		pSelf->SendChatTarget(ClientID, aBuf);
		return;
	}

	const int64_t Now = time_timestamp();
	if(pSelf->m_LastQQRelayTime > 0 && Now - pSelf->m_LastQQRelayTime < g_Config.m_SvMusicQQGlobalCooldown)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "全服喊话冷却中，请等待 %lld 秒",
			(long long)(g_Config.m_SvMusicQQGlobalCooldown - (Now - pSelf->m_LastQQRelayTime)));
		pSelf->SendChatTarget(ClientID, aBuf);
		return;
	}

	const NETADDR *pAddr = pSelf->Server()->ClientAddr(ClientID);
	if(pSelf->m_QQMessageCooldowns.IsCooldown(pAddr))
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "请等待 %d 秒后再使用 /qqmsg",
			pSelf->m_QQMessageCooldowns.GetSecondsLeft(pAddr));
		pSelf->SendChatTarget(ClientID, aBuf);
		return;
	}

	pSelf->m_LastQQRelayTime = Now;
	pSelf->m_QQMessageCooldowns.SetCooldown(pAddr, g_Config.m_SvMusicQQPlayerCooldown);
	pSelf->RequestQQRelay(ClientID, pSelf->Server()->ClientName(ClientID), pMessage);
	pSelf->SendChatTarget(ClientID, "正在向 QQ 群发送喊话...");
}

void CGameContext::ConChatMusicRank(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pResult->m_ClientId < 0 || pResult->m_ClientId >= MAX_CLIENTS)
		return;
	pSelf->RequestMusicStats(pResult->m_ClientId, false);
	pSelf->SendChatTarget(pResult->m_ClientId, "正在查询点歌排行榜...");
}

void CGameContext::ConChatMyRank(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pResult->m_ClientId < 0 || pResult->m_ClientId >= MAX_CLIENTS)
		return;
	pSelf->RequestMusicStats(pResult->m_ClientId, true);
	pSelf->SendChatTarget(pResult->m_ClientId, "正在查询你的点歌记录...");
}

void CGameContext::ConChatBindQQ(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientID = pResult->m_ClientId;
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientID])
		return;
	pSelf->RequestQQBindCode(ClientID);
	pSelf->SendChatTarget(ClientID, "正在生成 QQ 绑定验证码...");
}

void CGameContext::ConChatBindStatus(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientID = pResult->m_ClientId;
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientID])
		return;
	pSelf->RequestQQBindStatus(ClientID);
	pSelf->SendChatTarget(ClientID, "正在查询 QQ 绑定状态...");
}

void CGameContext::ConChatJrrp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientID = pResult->m_ClientId;
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientID])
		return;
	const char *pName = pSelf->Server()->ClientName(ClientID);
	const int64_t Day = (time_timestamp() + 8 * 60 * 60) / (24 * 60 * 60);
	const int Score = (int)(FunHash(pName, Day) % 101);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s 今日人品：%d/100，%s", pName, Score, LuckComment(Score));
	pSelf->SendChatTarget(-1, aBuf);
}

void CGameContext::ConChatRoll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientID = pResult->m_ClientId;
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientID])
		return;
	int Count = 1;
	int Sides = 100;
	const char *pSpec = pResult->NumArguments() > 0 ? pResult->GetString(0) : "100";
	if(!ParseDiceSpec(pSpec, &Count, &Sides))
	{
		pSelf->SendChatTarget(ClientID, "用法: /roll [面数] 或 /roll 2d6，最多 20 个骰子，每个 2-1000 面");
		return;
	}

	int Total = 0;
	std::string Detail;
	for(int i = 0; i < Count; ++i)
	{
		const int Value = FunRandomInt(Sides) + 1;
		Total += Value;
		if(i > 0)
			Detail += " + ";
		Detail += std::to_string(Value);
	}
	char aBuf[256];
	if(Count == 1)
		str_format(aBuf, sizeof(aBuf), "%s 掷出 d%d：%d", pSelf->Server()->ClientName(ClientID), Sides, Total);
	else
		str_format(aBuf, sizeof(aBuf), "%s 掷出 %dd%d：%s = %d", pSelf->Server()->ClientName(ClientID), Count, Sides, Detail.c_str(), Total);
	pSelf->SendChatTarget(-1, aBuf);
}

void CGameContext::ConChatPick(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientID = pResult->m_ClientId;
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientID])
		return;
	const std::vector<std::string> vOptions = SplitPickOptions(pResult->NumArguments() > 0 ? pResult->GetString(0) : "");
	if(vOptions.size() < 2)
	{
		pSelf->SendChatTarget(ClientID, "用法: /pick 选项A | 选项B | 选项C");
		return;
	}
	if(vOptions.size() > 20)
	{
		pSelf->SendChatTarget(ClientID, "选择太多啦，最多 20 个");
		return;
	}
	const std::string &Choice = vOptions[FunRandomInt((int)vOptions.size())];
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s 的选择题答案：%s", pSelf->Server()->ClientName(ClientID), Choice.c_str());
	pSelf->SendChatTarget(-1, aBuf);
}

void CGameContext::ConChatGuess(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientID = pResult->m_ClientId;
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientID])
		return;
	pSelf->RequestGuess(ClientID, pResult->NumArguments() > 0 ? pResult->GetString(0) : "");
}

void CGameContext::ConChatGuessRank(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientID = pResult->m_ClientId;
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientID])
		return;
	pSelf->RequestGuessRank(ClientID);
	pSelf->SendChatTarget(ClientID, "正在查询猜歌榜...");
}

void CGameContext::ConChatUndercover(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientID = pResult->m_ClientId;
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientID])
		return;

	std::string Input = pResult->NumArguments() > 0 ? pResult->GetString(0) : "";
	Input = TrimAscii(Input);
	if(Input.empty())
		Input = "help";

	std::istringstream Stream(Input);
	std::string Action;
	Stream >> Action;
	std::string Rest;
	std::getline(Stream, Rest);
	Rest = TrimAscii(Rest);

	std::string Room;
	std::string Arg;
	if(str_comp_nocase(Action.c_str(), "create") == 0 || str_comp_nocase(Action.c_str(), "join") == 0 ||
		str_comp_nocase(Action.c_str(), "开房") == 0 || str_comp_nocase(Action.c_str(), "创建") == 0 ||
		str_comp_nocase(Action.c_str(), "加入") == 0)
	{
		Room = Rest;
	}
	else if(str_comp_nocase(Action.c_str(), "vote") == 0 || str_comp_nocase(Action.c_str(), "投票") == 0)
	{
		Arg = Rest;
	}

	pSelf->RequestUndercover(ClientID, Action.c_str(), Room.c_str(), Arg.c_str());
}


void CGameContext::ConDownloadSong(IConsole::IResult *pResult, void *pUserData)      
{      
    CGameContext *pSelf = (CGameContext *)pUserData;      

    const int NumArgs = pResult->NumArguments();
    if(NumArgs != 1 && NumArgs != 3)
    {      
        pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid arguments for download_song");      
        return;      
    }      

    SongInfo Song;
    if(NumArgs == 1)
    {
        const int VoteId = pResult->GetInteger(0);
        if(!pSelf->m_Music.ConsumePendingSongVote(VoteId, &Song))
        {
            pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Unknown or expired song vote id");
            return;
        }
    }
    else
    {
        Song.title = pResult->GetString(0);
        Song.artist = pResult->GetString(1);
        Song.page_url = pResult->GetString(2);
        Song.requesterName = "Server";
        Song.requesterSource = "server";
        Song.requesterId = "server";
    }

    ResetQueueSongRuntimeState(&Song);
    pSelf->AddToPlaylist(Song);      
      
    if(pSelf->m_Music.QueueSize() == 1 && !pSelf->m_Music.IsPlayingFromQueue())
    {  
        pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Starting preload for first song in queue");  
        pSelf->InitializeQueuePlayback();  
    }  
}
