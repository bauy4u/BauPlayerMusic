/* Music backend events and HTTP jobs. */
#include "gamecontext.h"
#include "gamecontroller.h"
#include "music_config.h"
#include "player.h"

#include <engine/engine.h>
#include <engine/storage.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>

#include <sstream>
#include <utility>

namespace
{

void FormatMusicBackendUrl(char *pBuffer, int BufferSize, const char *pPath)
{
	const char *pBaseUrl = g_Config.m_SvMusicBackendUrl;
	const int BaseLength = str_length(pBaseUrl);
	const bool StripSlash = BaseLength > 0 && pBaseUrl[BaseLength - 1] == '/';
	const bool PathHasSlash = pPath[0] == '/';

	if(StripSlash && PathHasSlash)
		str_format(pBuffer, BufferSize, "%.*s%s", BaseLength - 1, pBaseUrl, pPath);
	else if(!StripSlash && !PathHasSlash)
		str_format(pBuffer, BufferSize, "%s/%s", pBaseUrl, pPath);
	else
		str_format(pBuffer, BufferSize, "%s%s", pBaseUrl, pPath);
}

}

void CGameContext::SendPersonalizedMOTD(int ClientID, const std::vector<SongInfo> &Songs)  
{  
    if(Songs.empty())  
    {  
        SendChatTarget(ClientID, "未找到相关歌曲");  
        return;  
    }  
      
    SendChatTarget(ClientID, "=== 搜索结果 ===");  
    for(size_t i = 0; i < Songs.size(); i++)  
    {  
        char aBuf[512];  
        str_format(aBuf, sizeof(aBuf), "%d. %s - %s",   
            (int)(i + 1), Songs[i].title.c_str(), Songs[i].artist.c_str());  
        SendChatTarget(ClientID, aBuf);  
    }  
    SendChatTarget(ClientID, "使用 /choose <编号> 选择歌曲");  
}

void CGameContext::QueueMusicSearchResult(int ClientID, std::vector<SongInfo> vSongs, const char *pError, bool ForVoteMenu)
{
	SMusicEvent Event;
	Event.m_Type = EMusicEventType::SEARCH_RESULT;
	Event.m_ClientID = ClientID;
	Event.m_vSongs = std::move(vSongs);
	Event.m_ForVoteMenu = ForVoteMenu;
	if(pError)
		Event.m_Error = pError;

	m_Music.QueueEvent(std::move(Event));
}

void CGameContext::QueueMusicPreloadResult(const SongInfo &Song, int QueueIndex, bool Success, float Duration, const char *pMapName, const char *pPreparedMapPath, const char *pMapSha256, const char *pError)
{
	SMusicEvent Event;
	Event.m_Type = EMusicEventType::PRELOAD_RESULT;
	Event.m_Song = Song;
	Event.m_QueueIndex = QueueIndex;
	Event.m_Success = Success;
	Event.m_Duration = Duration;
	if(pMapName)
		Event.m_MapName = pMapName;
	if(pPreparedMapPath)
		Event.m_PreparedMapPath = pPreparedMapPath;
	if(pMapSha256)
		Event.m_MapSha256 = pMapSha256;
	if(pError)
		Event.m_Error = pError;

	m_Music.QueueEvent(std::move(Event));
}

void CGameContext::QueueMusicUploadResult(bool Success, const char *pError)
{
	SMusicEvent Event;
	Event.m_Type = EMusicEventType::UPLOAD_RESULT;
	Event.m_Success = Success;
	if(pError)
		Event.m_Error = pError;

	m_Music.QueueEvent(std::move(Event));
}

void CGameContext::QueueQQRelayResult(int ClientID, bool Success, const char *pMessage)
{
	SMusicEvent Event;
	Event.m_Type = EMusicEventType::QQ_RELAY_RESULT;
	Event.m_ClientID = ClientID;
	Event.m_Success = Success;
	if(pMessage)
		Event.m_Error = pMessage;
	m_Music.QueueEvent(std::move(Event));
}

void CGameContext::QueueQQPollResult(bool Success, std::vector<std::string> vMessages, const char *pError)
{
	SMusicEvent Event;
	Event.m_Type = EMusicEventType::QQ_POLL_RESULT;
	Event.m_Success = Success;
	Event.m_vMessages = std::move(vMessages);
	if(pError)
		Event.m_Error = pError;
	m_Music.QueueEvent(std::move(Event));
}

void CGameContext::QueueMusicStatsResult(int ClientID, bool Success, const char *pText)
{
	SMusicEvent Event;
	Event.m_Type = EMusicEventType::MUSIC_STATS_RESULT;
	Event.m_ClientID = ClientID;
	Event.m_Success = Success;
	if(pText)
		Event.m_Error = pText;
	m_Music.QueueEvent(std::move(Event));
}

void CGameContext::QueueMusicStatsEvent(SMusicEvent Event)
{
	Event.m_Type = EMusicEventType::MUSIC_STATS_RESULT;
	m_Music.QueueEvent(std::move(Event));
}

void CGameContext::QueueTurtleSoupResult(int ClientID, bool Success, const char *pText)
{
	SMusicEvent Event;
	Event.m_Type = EMusicEventType::TURTLE_SOUP_RESULT;
	Event.m_ClientID = ClientID;
	Event.m_Success = Success;
	if(pText)
		Event.m_Error = pText;
	m_Music.QueueEvent(std::move(Event));
}

int CGameContext::EnsureUndercoverTeam(const std::string &Room)
{
	if(Room.empty())
		return TEAM_FLOCK;
	auto &State = m_UndercoverRooms[Room];
	if(State.m_Team > TEAM_FLOCK && State.m_Team < TEAM_SUPER)
		return State.m_Team;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_apPlayers[i] && m_aUndercoverRoom[i] == Room && m_aUndercoverTeam[i] > TEAM_FLOCK)
		{
			State.m_Team = m_aUndercoverTeam[i];
			return State.m_Team;
		}
	}

	int Team = m_pController->Teams().GetFirstEmptyTeam();
	if(Team <= TEAM_FLOCK || Team >= TEAM_SUPER)
		Team = 1;
	State.m_Team = Team;
	return Team;
}

void CGameContext::ApplyUndercoverEvent(const SMusicEvent &Event)
{
	if(!Event.m_Undercover)
		return;

	if(!Event.m_UndercoverActive)
	{
		if(Event.m_UndercoverClearRoom && !Event.m_UndercoverRoom.empty())
		{
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				if(m_aUndercoverRoom[i] == Event.m_UndercoverRoom)
					ClearUndercoverPlayer(i, true);
			}
			m_UndercoverRooms.erase(Event.m_UndercoverRoom);
		}
		else if(Event.m_ClientID >= 0 && Event.m_ClientID < MAX_CLIENTS)
		{
			const std::string Room = m_aUndercoverRoom[Event.m_ClientID];
			ClearUndercoverPlayer(Event.m_ClientID, true);
			if(!Room.empty())
			{
				bool AnyLeft = false;
				for(int i = 0; i < MAX_CLIENTS; ++i)
				{
					if(m_aUndercoverRoom[i] == Room)
					{
						AnyLeft = true;
						break;
					}
				}
				if(!AnyLeft)
					m_UndercoverRooms.erase(Room);
			}
		}
		return;
	}

	if(Event.m_UndercoverRoom.empty() || Event.m_ClientID < 0 || Event.m_ClientID >= MAX_CLIENTS || !m_apPlayers[Event.m_ClientID])
		return;

	const int Team = EnsureUndercoverTeam(Event.m_UndercoverRoom);
	if(m_aUndercoverRoom[Event.m_ClientID].empty() || m_aUndercoverRoom[Event.m_ClientID] != Event.m_UndercoverRoom)
		m_aUndercoverPreviousTeam[Event.m_ClientID] = GetDDRaceTeam(Event.m_ClientID);
	m_aUndercoverRoom[Event.m_ClientID] = Event.m_UndercoverRoom;
	m_aUndercoverTeam[Event.m_ClientID] = Team;
	if(GetDDRaceTeam(Event.m_ClientID) != Team)
	{
		m_pController->Teams().SetForceCharacterTeam(Event.m_ClientID, Team);
		char aBuf[160];
		str_format(aBuf, sizeof(aBuf), "已为谁是卧底房间自动加入 DDNet 队伍 %d", Team);
		SendChatTarget(Event.m_ClientID, aBuf);
	}

	CUndercoverRoomRuntime &State = m_UndercoverRooms[Event.m_UndercoverRoom];
	State.m_Team = Team;
	State.m_Phase = Event.m_UndercoverPhase;
	State.m_SpeakerId = Event.m_UndercoverSpeakerId;
	State.m_SpeakerName = Event.m_UndercoverSpeakerName;
	State.m_TimeoutRequested = false;
	State.m_LastReminderSecond = -1;
	if(Event.m_UndercoverDeadline > 0)
	{
		const int64_t Delta = Event.m_UndercoverDeadline - time_timestamp();
		const int64_t SecondsLeft = Delta > 0 ? Delta : 0;
		State.m_DeadlineTick = Server()->Tick() + SecondsLeft * Server()->TickSpeed();
	}
	else
	{
		State.m_DeadlineTick = 0;
	}
}

void CGameContext::ClearUndercoverPlayer(int ClientID, bool RestoreTeam)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	const int PreviousTeam = m_aUndercoverPreviousTeam[ClientID];
	m_aUndercoverRoom[ClientID].clear();
	m_aUndercoverTeam[ClientID] = 0;
	m_aUndercoverPreviousTeam[ClientID] = TEAM_FLOCK;

	if(RestoreTeam && m_apPlayers[ClientID] && PreviousTeam >= TEAM_FLOCK && PreviousTeam < TEAM_SUPER && GetDDRaceTeam(ClientID) != PreviousTeam)
	{
		m_pController->Teams().SetForceCharacterTeam(ClientID, PreviousTeam);
		SendChatTarget(ClientID, "已离开谁是卧底队伍，恢复原 DDNet 队伍");
	}
}

bool CGameContext::HandleUndercoverChat(const CNetMsg_Cl_Say *pMsg, int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !m_apPlayers[ClientId] || pMsg->m_pMessage[0] == '/')
		return false;
	const std::string Room = m_aUndercoverRoom[ClientId];
	if(Room.empty() || !m_UndercoverRooms.count(Room))
		return false;

	CUndercoverRoomRuntime &State = m_UndercoverRooms[Room];
	const int Team = EnsureUndercoverTeam(Room);
	char aCensoredMessage[256];
	CensorMessage(aCensoredMessage, pMsg->m_pMessage, sizeof(aCensoredMessage));

	if(State.m_Phase == "speaking")
	{
		if(State.m_SpeakerId != Server()->ClientName(ClientId))
		{
			char aBuf[192];
			str_format(aBuf, sizeof(aBuf), "还没轮到你发言。当前发言者：%s", State.m_SpeakerName.empty() ? "未知" : State.m_SpeakerName.c_str());
			SendChatTarget(ClientId, aBuf);
			return true;
		}
		m_apPlayers[ClientId]->UpdatePlaytime();
		SendChat(ClientId, Team, aCensoredMessage, ClientId);
		State.m_Phase = "advancing";
		State.m_SpeakerId.clear();
		State.m_SpeakerName.clear();
		State.m_DeadlineTick = 0;
		State.m_TimeoutRequested = true;
		RequestUndercover(ClientId, "speak", "", aCensoredMessage);
		return true;
	}

	if(State.m_Phase == "advancing")
	{
		SendChatTarget(ClientId, "正在切到下一位发言者，请稍等。");
		return true;
	}

	if(State.m_Phase == "voting")
	{
		SendChatTarget(ClientId, "发言轮已结束，请使用 /uc vote 玩家名 投票。");
		return true;
	}

	m_apPlayers[ClientId]->UpdatePlaytime();
	SendChat(ClientId, Team, aCensoredMessage, ClientId);
	return true;
}

bool CGameContext::HandleTurtleSoupChat(const CNetMsg_Cl_Say *pMsg, int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !m_apPlayers[ClientId] || Server()->IsTurtleSoupDummy(ClientId) || pMsg->m_pMessage[0] == '/')
		return false;

	std::string Text = pMsg->m_pMessage ? pMsg->m_pMessage : "";
	const std::string Prefix = std::string(g_Config.m_SvTurtleSoupDummyName) + ": ";
	if(Text.rfind(Prefix, 0) != 0)
		return false;

	std::string Message = Text.substr(Prefix.size());
	while(!Message.empty() && (Message.front() == ' ' || Message.front() == '\t'))
		Message.erase(Message.begin());
	if(Message.empty())
	{
		SendChatTarget(ClientId, "用法：Tee探长: 开始游戏 / Tee探长: 你的问题");
		return true;
	}
	if(m_TurtleSoupDummyClientID < 0 || !m_apPlayers[m_TurtleSoupDummyClientID])
	{
		SendChatTarget(ClientId, "Tee探长还没上线，稍等片刻再试。");
		return true;
	}

	size_t MessageSize = 0;
	size_t MessageLength = 0;
	str_utf8_stats(Message.c_str(), str_length(Message.c_str()) + 1, (size_t)-1, &MessageSize, &MessageLength);
	if(MessageLength > 120)
	{
		SendChatTarget(ClientId, "问题太长了，Tee探长的笔记本写不下。");
		return true;
	}

	RequestTurtleSoup(ClientId, Message.c_str());
	SendChat(ClientId, TEAM_ALL, pMsg->m_pMessage, ClientId);
	return true;
}

void CGameContext::RefreshTurtleSoupDummyActivity()
{
	if(m_TurtleSoupDummyClientID < 0 || m_TurtleSoupDummyClientID >= MAX_CLIENTS)
		return;
	CPlayer *pPlayer = m_apPlayers[m_TurtleSoupDummyClientID];
	if(!pPlayer || !Server()->IsTurtleSoupDummy(m_TurtleSoupDummyClientID))
		return;

	pPlayer->SetAfk(false);
	pPlayer->UpdatePlaytime();
	pPlayer->m_LastActionTick = Server()->Tick();
	if(pPlayer->GetTeam() == TEAM_SPECTATORS)
		m_pController->DoTeamChange(pPlayer, m_pController->GetAutoTeam(m_TurtleSoupDummyClientID), false);
}

void CGameContext::CheckUndercoverTimers()
{
	if(Server()->Tick() % Server()->TickSpeed() != 0)
		return;
	for(auto &Entry : m_UndercoverRooms)
	{
		CUndercoverRoomRuntime &State = Entry.second;
		if(State.m_Phase != "speaking" || State.m_DeadlineTick <= 0 || State.m_Team <= TEAM_FLOCK)
			continue;
		const int Remaining = (int)((State.m_DeadlineTick - Server()->Tick()) / Server()->TickSpeed());
		if(Remaining > 0)
		{
			if((Remaining == 30 || Remaining == 15 || Remaining == 10 || Remaining <= 5) && State.m_LastReminderSecond != Remaining)
			{
				char aBuf[160];
				str_format(aBuf, sizeof(aBuf), "%s 发言倒计时：%d 秒", State.m_SpeakerName.empty() ? "当前玩家" : State.m_SpeakerName.c_str(), Remaining);
				SendChatTeam(State.m_Team, aBuf);
				State.m_LastReminderSecond = Remaining;
			}
			continue;
		}
		if(State.m_TimeoutRequested)
			continue;
		int RequestClient = -1;
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(!m_apPlayers[i] || m_aUndercoverRoom[i] != Entry.first)
				continue;
			if(State.m_SpeakerId == Server()->ClientName(i))
			{
				RequestClient = i;
				break;
			}
			if(RequestClient < 0)
				RequestClient = i;
		}
		if(RequestClient >= 0)
		{
			State.m_TimeoutRequested = true;
			SendChatTeam(State.m_Team, "发言超时，正在切到下一位...");
			RequestUndercover(RequestClient, "timeout", "", "");
		}
	}
}

void CGameContext::QueueIdentityCodeResult(int ClientID, bool Success, const char *pText)
{
	SMusicEvent Event;
	Event.m_Type = EMusicEventType::IDENTITY_CODE_RESULT;
	Event.m_ClientID = ClientID;
	Event.m_Success = Success;
	if(pText)
		Event.m_Error = pText;
	m_Music.QueueEvent(std::move(Event));
}

void CGameContext::QueueServerStateResult(bool Success)
{
	SMusicEvent Event;
	Event.m_Type = EMusicEventType::SERVER_STATE_RESULT;
	Event.m_Success = Success;
	m_Music.QueueEvent(std::move(Event));
}

void CGameContext::ProcessMusicEvents()
{
	std::vector<SMusicEvent> vEvents = m_Music.DrainEvents();

	for(const SMusicEvent &Event : vEvents)
	{
		if(Event.m_Type == EMusicEventType::SEARCH_RESULT)
		{
			if(Event.m_ClientID == ADMIN_MUSIC_SEARCH_CLIENT_ID)
			{
				if(!Event.m_Error.empty())
				{
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "queue_search", Event.m_Error.c_str());
					continue;
				}

				m_Music.StoreSearchResults(ADMIN_MUSIC_SEARCH_CLIENT_ID, Event.m_vSongs);
				if(Event.m_vSongs.empty())
				{
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "queue_search", "No matching songs found");
					continue;
				}

				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "queue_search", "Results (use: queue_insert_after <after-queue-number> <result-number>):");
				for(size_t Index = 0; Index < Event.m_vSongs.size(); Index++)
				{
					const SongInfo &Song = Event.m_vSongs[Index];
					char aBuf[512];
					str_format(aBuf, sizeof(aBuf), "%d. %s - %s (song id: %s)", (int)Index + 1, Song.title.c_str(), Song.artist.c_str(), Song.page_url.c_str());
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "queue_search", aBuf);
				}
				continue;
			}

			if(!Event.m_Error.empty())
			{
				SendChatTarget(Event.m_ClientID, Event.m_Error.c_str());
				continue;
			}

			if(Event.m_ForVoteMenu)
			{
				m_Music.StoreVoteMenuSearchResults(Event.m_ClientID, Event.m_vSongs);
				RefreshMusicVoteMenus(Event.m_ClientID);
			}
			else
			{
				m_Music.StoreSearchResults(Event.m_ClientID, Event.m_vSongs);
				SendPersonalizedMOTD(Event.m_ClientID, Event.m_vSongs);
			}
		}
		else if(Event.m_Type == EMusicEventType::PRELOAD_RESULT)
		{
			if(Event.m_Success && Event.m_Duration > 0.0f)
				ProcessPreloadedSong(Event.m_Song, Event.m_Duration, Event.m_QueueIndex, Event.m_MapName.c_str(), Event.m_PreparedMapPath.c_str(), Event.m_MapSha256.c_str());
			else
				HandlePreloadFailure(Event.m_QueueIndex, Event.m_Error.empty() ? "Unknown preload error" : Event.m_Error.c_str(), &Event.m_Song);
		}
		else if(Event.m_Type == EMusicEventType::UPLOAD_RESULT)
		{
			char aBuf[256];
			if(Event.m_Success)
			{
				str_format(aBuf, sizeof(aBuf), "Map upload completed. Queue state: CurrentSongIndex=%d, QueueSize=%d, IsPlaying=%s",
					m_Music.CurrentSongIndex(), m_Music.QueueSize(), m_Music.IsPlayingFromQueue() ? "true" : "false");
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), "Map upload skipped or failed: %s", Event.m_Error.empty() ? "unknown error" : Event.m_Error.c_str());
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			}
		}
		else if(Event.m_Type == EMusicEventType::QQ_RELAY_RESULT)
		{
			SendChatTarget(Event.m_ClientID, Event.m_Success ? "喊话已发送到 QQ 群" :
				(Event.m_Error.empty() ? "发送 QQ 群喊话失败" : Event.m_Error.c_str()));
		}
		else if(Event.m_Type == EMusicEventType::QQ_POLL_RESULT)
		{
			m_QQPollInFlight = false;
			if(Event.m_Success)
			{
				for(const std::string &Message : Event.m_vMessages)
					SendChat(-1, TEAM_ALL, Message.c_str());
			}
			else if(!Event.m_Error.empty())
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "qqbot", Event.m_Error.c_str());
			}
		}
		else if(Event.m_Type == EMusicEventType::MUSIC_STATS_RESULT)
		{
			const std::string Text = Event.m_Error.empty() ?
				(Event.m_Success ? "暂无点歌统计" : "点歌统计查询失败") : Event.m_Error;
			if(Event.m_Undercover)
				ApplyUndercoverEvent(Event);
			std::istringstream Stream(Text);
			std::string Line;
			const bool SendToUndercoverTeam = Event.m_Undercover && Event.m_UndercoverTeamAudience &&
				!Event.m_UndercoverRoom.empty() && m_UndercoverRooms.count(Event.m_UndercoverRoom) &&
				m_UndercoverRooms[Event.m_UndercoverRoom].m_Team > 0;
			while(std::getline(Stream, Line))
			{
				if(!Line.empty())
				{
					if(SendToUndercoverTeam)
						SendChatTeam(m_UndercoverRooms[Event.m_UndercoverRoom].m_Team, Line.c_str());
					else
						SendChatTarget(Event.m_ClientID, Line.c_str());
				}
			}
		}
		else if(Event.m_Type == EMusicEventType::IDENTITY_CODE_RESULT)
		{
			const std::string Text = Event.m_Error.empty() ?
				(Event.m_Success ? "验证码生成成功" : "验证码生成失败") : Event.m_Error;
			std::istringstream Stream(Text);
			std::string Line;
			while(std::getline(Stream, Line))
			{
				if(!Line.empty())
					SendChatTarget(Event.m_ClientID, Line.c_str());
			}
		}
		else if(Event.m_Type == EMusicEventType::SERVER_STATE_RESULT)
		{
			m_ServerStateUpdateInFlight = false;
		}
		else if(Event.m_Type == EMusicEventType::TURTLE_SOUP_RESULT)
		{
			const std::string Text = Event.m_Error.empty() ?
				(Event.m_Success ? "线索暂时断了。" : "Tee探长暂时没有回应。") : Event.m_Error;
			const int Chatter = (m_TurtleSoupDummyClientID >= 0 && m_apPlayers[m_TurtleSoupDummyClientID]) ? m_TurtleSoupDummyClientID : -1;
			std::istringstream Stream(Text);
			std::string Line;
			while(std::getline(Stream, Line))
			{
				if(!Line.empty())
					SendChat(Chatter, TEAM_ALL, Line.c_str(), -1);
			}
		}
	}
}

void CGameContext::RequestQQRelay(int ClientID, const char *pPlayerName, const char *pMessage)
{
	char aUrl[512];
	FormatMusicBackendUrl(aUrl, sizeof(aUrl), "/qqbot/send");

	const std::string EscapedPlayer = EscapeJsonString(pPlayerName);
	const std::string EscapedMessage = EscapeJsonString(pMessage);

	char aJsonBody[1400];
	str_format(aJsonBody, sizeof(aJsonBody),
		"{\"player_name\":\"%s\",\"message\":\"%s\"}",
		EscapedPlayer.c_str(), EscapedMessage.c_str());

	auto pRequest = std::make_shared<CHttpRequest>(aUrl);
	pRequest->PostJson(aJsonBody);
	pRequest->WriteToMemory();
	pRequest->Timeout(CTimeout{5000, 10000, 0, 0});
	if(g_Config.m_SvMusicQQToken[0])
	{
		const std::string Authorization = std::string("Bearer ") + g_Config.m_SvMusicQQToken;
		pRequest->HeaderString("Authorization", Authorization.c_str());
	}
	Kernel()->RequestInterface<IHttp>()->Run(pRequest);
	Engine()->AddJob(std::make_shared<CQQRelayJob>(this, ClientID, pRequest));
}

void CGameContext::PollQQMessages()
{
	char aUrl[512];
	FormatMusicBackendUrl(aUrl, sizeof(aUrl), "/qqbot/poll");

	auto pRequest = std::make_shared<CHttpRequest>(aUrl);
	pRequest->PostJson("{}");
	pRequest->WriteToMemory();
	pRequest->Timeout(CTimeout{3000, 5000, 0, 0});
	if(g_Config.m_SvMusicQQToken[0])
	{
		const std::string Authorization = std::string("Bearer ") + g_Config.m_SvMusicQQToken;
		pRequest->HeaderString("Authorization", Authorization.c_str());
	}

	m_QQPollInFlight = true;
	m_LastQQPollTick = Server()->Tick();
	Kernel()->RequestInterface<IHttp>()->Run(pRequest);
	Engine()->AddJob(std::make_shared<CQQPollJob>(this, pRequest));
}

void CGameContext::RequestMusicHistoryRecord(const SongInfo &Song)
{
	char aUrl[512];
	FormatMusicBackendUrl(aUrl, sizeof(aUrl), "/history/record");

	const std::string Title = EscapeJsonString(Song.title);
	const std::string Artist = EscapeJsonString(Song.artist);
	const std::string SongId = EscapeJsonString(Song.page_url);
	const std::string RequesterName = EscapeJsonString(Song.requesterName);
	const std::string RequesterSource = EscapeJsonString(Song.requesterSource);
	const std::string RequesterId = EscapeJsonString(Song.requesterId);

	char aJsonBody[2048];
	str_format(aJsonBody, sizeof(aJsonBody),
		"{\"song_id\":\"%s\",\"title\":\"%s\",\"artist\":\"%s\","
		"\"requester_name\":\"%s\",\"requester_source\":\"%s\",\"requester_id\":\"%s\"}",
		SongId.c_str(), Title.c_str(), Artist.c_str(), RequesterName.c_str(),
		RequesterSource.c_str(), RequesterId.c_str());

	auto pRequest = std::make_shared<CHttpRequest>(aUrl);
	pRequest->PostJson(aJsonBody);
	pRequest->WriteToMemory();
	pRequest->Timeout(CTimeout{5000, 10000, 0, 0});
	if(g_Config.m_SvMusicQQToken[0])
	{
		const std::string Authorization = std::string("Bearer ") + g_Config.m_SvMusicQQToken;
		pRequest->HeaderString("Authorization", Authorization.c_str());
	}
	Kernel()->RequestInterface<IHttp>()->Run(pRequest);
	Engine()->AddJob(std::make_shared<CMusicHistoryJob>(pRequest));
}

void CGameContext::RequestMusicStats(int ClientID, bool Personal)
{
	char aUrl[1024];
	if(Personal)
	{
		char aEscapedId[256];
		char aEscapedName[256];
		EscapeUrl(aEscapedId, sizeof(aEscapedId), Server()->ClientName(ClientID));
		EscapeUrl(aEscapedName, sizeof(aEscapedName), Server()->ClientName(ClientID));
		char aPath[768];
		str_format(aPath, sizeof(aPath), "/stats/myrank?source=game&id=%s&name=%s", aEscapedId, aEscapedName);
		FormatMusicBackendUrl(aUrl, sizeof(aUrl), aPath);
	}
	else
	{
		FormatMusicBackendUrl(aUrl, sizeof(aUrl), "/stats/musicrank");
	}

	auto pRequest = std::make_shared<CHttpRequest>(aUrl);
	pRequest->WriteToMemory();
	pRequest->Timeout(CTimeout{5000, 10000, 0, 0});
	Kernel()->RequestInterface<IHttp>()->Run(pRequest);
	Engine()->AddJob(std::make_shared<CMusicStatsJob>(this, ClientID, pRequest));
}

void CGameContext::RequestGuess(int ClientID, const char *pAnswer)
{
	char aEscapedId[256];
	char aEscapedName[256];
	char aEscapedAnswer[512];
	EscapeUrl(aEscapedId, sizeof(aEscapedId), Server()->ClientName(ClientID));
	EscapeUrl(aEscapedName, sizeof(aEscapedName), Server()->ClientName(ClientID));
	EscapeUrl(aEscapedAnswer, sizeof(aEscapedAnswer), pAnswer ? pAnswer : "");
	char aPath[1024];
	str_format(aPath, sizeof(aPath), "/guess?source=game&id=%s&name=%s&answer=%s", aEscapedId, aEscapedName, aEscapedAnswer);
	char aUrl[1024];
	FormatMusicBackendUrl(aUrl, sizeof(aUrl), aPath);

	auto pRequest = std::make_shared<CHttpRequest>(aUrl);
	pRequest->WriteToMemory();
	pRequest->Timeout(CTimeout{5000, 10000, 0, 0});
	if(g_Config.m_SvMusicQQToken[0])
	{
		const std::string Authorization = std::string("Bearer ") + g_Config.m_SvMusicQQToken;
		pRequest->HeaderString("Authorization", Authorization.c_str());
	}
	Kernel()->RequestInterface<IHttp>()->Run(pRequest);
	Engine()->AddJob(std::make_shared<CMusicStatsJob>(this, ClientID, pRequest));
}

void CGameContext::RequestGuessRank(int ClientID)
{
	char aUrl[512];
	FormatMusicBackendUrl(aUrl, sizeof(aUrl), "/guessrank");
	auto pRequest = std::make_shared<CHttpRequest>(aUrl);
	pRequest->WriteToMemory();
	pRequest->Timeout(CTimeout{5000, 10000, 0, 0});
	if(g_Config.m_SvMusicQQToken[0])
	{
		const std::string Authorization = std::string("Bearer ") + g_Config.m_SvMusicQQToken;
		pRequest->HeaderString("Authorization", Authorization.c_str());
	}
	Kernel()->RequestInterface<IHttp>()->Run(pRequest);
	Engine()->AddJob(std::make_shared<CMusicStatsJob>(this, ClientID, pRequest));
}

void CGameContext::RequestUndercover(int ClientID, const char *pAction, const char *pRoom, const char *pArg)
{
	char aEscapedId[256];
	char aEscapedName[256];
	char aEscapedAction[128];
	char aEscapedRoom[256];
	char aEscapedArg[512];
	EscapeUrl(aEscapedId, sizeof(aEscapedId), Server()->ClientName(ClientID));
	EscapeUrl(aEscapedName, sizeof(aEscapedName), Server()->ClientName(ClientID));
	EscapeUrl(aEscapedAction, sizeof(aEscapedAction), pAction ? pAction : "help");
	EscapeUrl(aEscapedRoom, sizeof(aEscapedRoom), pRoom ? pRoom : "");
	EscapeUrl(aEscapedArg, sizeof(aEscapedArg), pArg ? pArg : "");

	char aPath[1200];
	str_format(aPath, sizeof(aPath), "/undercover?source=game&id=%s&name=%s&action=%s&room=%s&arg=%s",
		aEscapedId, aEscapedName, aEscapedAction, aEscapedRoom, aEscapedArg);
	char aUrl[1200];
	FormatMusicBackendUrl(aUrl, sizeof(aUrl), aPath);

	auto pRequest = std::make_shared<CHttpRequest>(aUrl);
	pRequest->WriteToMemory();
	pRequest->Timeout(CTimeout{5000, 10000, 0, 0});
	if(g_Config.m_SvMusicQQToken[0])
	{
		const std::string Authorization = std::string("Bearer ") + g_Config.m_SvMusicQQToken;
		pRequest->HeaderString("Authorization", Authorization.c_str());
	}
	Kernel()->RequestInterface<IHttp>()->Run(pRequest);
	Engine()->AddJob(std::make_shared<CMusicStatsJob>(this, ClientID, pRequest));
}

void CGameContext::RequestTurtleSoup(int ClientID, const char *pMessage)
{
	char aUrl[512];
	FormatMusicBackendUrl(aUrl, sizeof(aUrl), "/turtle_soup");

	const std::string PlayerName = EscapeJsonString(Server()->ClientName(ClientID));
	const std::string Message = EscapeJsonString(pMessage ? pMessage : "");
	char aJsonBody[1024];
	str_format(aJsonBody, sizeof(aJsonBody),
		"{\"source\":\"game\",\"id\":\"%s\",\"name\":\"%s\",\"message\":\"%s\"}",
		PlayerName.c_str(), PlayerName.c_str(), Message.c_str());

	auto pRequest = std::make_shared<CHttpRequest>(aUrl);
	pRequest->PostJson(aJsonBody);
	pRequest->WriteToMemory();
	pRequest->Timeout(CTimeout{5000, 30000, 0, 0});
	if(g_Config.m_SvMusicQQToken[0])
	{
		const std::string Authorization = std::string("Bearer ") + g_Config.m_SvMusicQQToken;
		pRequest->HeaderString("Authorization", Authorization.c_str());
	}
	Kernel()->RequestInterface<IHttp>()->Run(pRequest);
	Engine()->AddJob(std::make_shared<CTurtleSoupJob>(this, ClientID, pRequest));
}

void CGameContext::RequestQQBindCode(int ClientID)
{
	char aUrl[512];
	FormatMusicBackendUrl(aUrl, sizeof(aUrl), "/identity/code");
	const std::string GameName = EscapeJsonString(Server()->ClientName(ClientID));
	char aJsonBody[512];
	str_format(aJsonBody, sizeof(aJsonBody), "{\"game_name\":\"%s\"}", GameName.c_str());

	auto pRequest = std::make_shared<CHttpRequest>(aUrl);
	pRequest->PostJson(aJsonBody);
	pRequest->WriteToMemory();
	pRequest->Timeout(CTimeout{5000, 10000, 0, 0});
	if(g_Config.m_SvMusicQQToken[0])
	{
		const std::string Authorization = std::string("Bearer ") + g_Config.m_SvMusicQQToken;
		pRequest->HeaderString("Authorization", Authorization.c_str());
	}
	Kernel()->RequestInterface<IHttp>()->Run(pRequest);
	Engine()->AddJob(std::make_shared<CIdentityCodeJob>(this, ClientID, pRequest));
}

void CGameContext::RequestQQBindStatus(int ClientID)
{
	char aEscapedName[256];
	EscapeUrl(aEscapedName, sizeof(aEscapedName), Server()->ClientName(ClientID));
	char aPath[512];
	str_format(aPath, sizeof(aPath), "/identity/status?game_name=%s", aEscapedName);
	char aUrl[512];
	FormatMusicBackendUrl(aUrl, sizeof(aUrl), aPath);

	auto pRequest = std::make_shared<CHttpRequest>(aUrl);
	pRequest->WriteToMemory();
	pRequest->Timeout(CTimeout{5000, 10000, 0, 0});
	if(g_Config.m_SvMusicQQToken[0])
	{
		const std::string Authorization = std::string("Bearer ") + g_Config.m_SvMusicQQToken;
		pRequest->HeaderString("Authorization", Authorization.c_str());
	}
	Kernel()->RequestInterface<IHttp>()->Run(pRequest);
	Engine()->AddJob(std::make_shared<CIdentityCodeJob>(this, ClientID, pRequest));
}

void CGameContext::UpdateBackendServerState()
{
	std::ostringstream Json;
	Json << "{\"map_name\":\"" << EscapeJsonString(Server()->GetMapName()) << "\",\"players\":[";
	bool First = true;
	for(int ClientID = 0; ClientID < MAX_CLIENTS; ++ClientID)
	{
		if(!Server()->ClientIngame(ClientID) || Server()->IsTurtleSoupDummy(ClientID))
			continue;
		if(!First)
			Json << ',';
		First = false;
		Json << '"' << EscapeJsonString(Server()->ClientName(ClientID)) << '"';
	}
	Json << "]}";

	char aUrl[512];
	FormatMusicBackendUrl(aUrl, sizeof(aUrl), "/server/state");
	auto pRequest = std::make_shared<CHttpRequest>(aUrl);
	pRequest->PostJson(Json.str().c_str());
	pRequest->WriteToMemory();
	pRequest->Timeout(CTimeout{3000, 5000, 0, 0});
	if(g_Config.m_SvMusicQQToken[0])
	{
		const std::string Authorization = std::string("Bearer ") + g_Config.m_SvMusicQQToken;
		pRequest->HeaderString("Authorization", Authorization.c_str());
	}

	m_ServerStateUpdateInFlight = true;
	m_LastServerStateUpdateTick = Server()->Tick();
	Kernel()->RequestInterface<IHttp>()->Run(pRequest);
	Engine()->AddJob(std::make_shared<CServerStateJob>(this, pRequest));
}

void CGameContext::RequestSongSearch(int ClientID, const char *pSongName, bool ForVoteMenu)
{
	char aEscapedName[256];
	EscapeUrl(aEscapedName, sizeof(aEscapedName), pSongName);

	char aUrl[512];
	char aSearchPath[512];
	const int SearchLimit = ForVoteMenu ? g_Config.m_SvMusicVoteSearchResults : g_Config.m_SvMusicSearchResults;
	str_format(aSearchPath, sizeof(aSearchPath), "/search?name=%s&limit=%d", aEscapedName, SearchLimit);
	FormatMusicBackendUrl(aUrl, sizeof(aUrl), aSearchPath);

	auto pRequest = std::make_shared<CHttpRequest>(aUrl);
	pRequest->WriteToMemory();
	pRequest->Timeout(CTimeout{30000, 100000, 500, 30});

	Kernel()->RequestInterface<IHttp>()->Run(pRequest);

	auto pJob = std::make_shared<CSongSearchJob>(this, ClientID, pRequest, ForVoteMenu);
	Engine()->AddJob(pJob);
}

void CGameContext::RequestUploadToObjectStorage(const char *pMapName, const char *pSha256)  
{  
    // 构造请求URL  
    char aUrl[512];  
    FormatMusicBackendUrl(aUrl, sizeof(aUrl), "/upload_map");  
      
    // 构造JSON请求体  
    char aJsonBody[256];  
    str_format(aJsonBody, sizeof(aJsonBody),   
               "{\"map_name\":\"%s\",\"hash\":\"%s\"}",   
               pMapName, pSha256);  
      
    dbg_msg("song", "Sending upload request: %s", aJsonBody);  
      
    // 创建HTTP POST请求  
    auto pRequest = std::make_shared<CHttpRequest>(aUrl);  
    pRequest->WriteToMemory();  
    pRequest->PostJson(aJsonBody);  
    pRequest->Timeout(CTimeout{30000, 60000, 500, 30}); // 30秒超时  
    // 启动HTTP请求  
    Kernel()->RequestInterface<IHttp>()->Run(pRequest);  
      
    // 创建作业来处理响应  
    auto pJob = std::make_shared<CMapUploadJob>(this, pRequest);  
    Engine()->AddJob(pJob);  
}

void CGameContext::StartPreloadingSong(int QueueIndex)    
{    
    if(m_Music.QueueEmpty())    
    {    
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Cannot preload: queue is empty");    
        return;    
    }    
      
    SongInfo *pSong = GetQueuedSong(QueueIndex);    
    if(!pSong)    
    {    
        char aBuf[128];    
        str_format(aBuf, sizeof(aBuf), "Cannot get song at index %d", QueueIndex);    
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);    
        return;    
    }    

    const int64_t Now = time_timestamp();

        if(g_Config.m_SvMusicMaxPreloadFailures > 0 && pSong->preloadFailures >= g_Config.m_SvMusicMaxPreloadFailures)
    {
        if(m_Music.ShouldLogPreloadSkip(Now, g_Config.m_SvMusicPreloadLogInterval))
        {
            char aBuf[256];
            str_format(aBuf, sizeof(aBuf), "Skipping preload for '%s - %s': reached max failures (%d)",
                pSong->title.c_str(), pSong->artist.c_str(), pSong->preloadFailures);
            Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
        }
        return;
    }

    if(pSong->nextPreloadRetryTime > Now)
    {
        if(m_Music.ShouldLogPreloadSkip(Now, g_Config.m_SvMusicPreloadLogInterval))
        {
            char aBuf[256];
            str_format(aBuf, sizeof(aBuf), "Preload retry cooling down for '%s - %s' (%llds left)",
                pSong->title.c_str(), pSong->artist.c_str(), (long long)(pSong->nextPreloadRetryTime - Now));
            Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
        }
        return;
    }
      
    if(m_Music.IsPreloading(pSong->page_url))  
    {  
        if(m_Music.ShouldLogPreloadSkip(Now, g_Config.m_SvMusicPreloadLogInterval))
        {  
            Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Song already being preloaded, skipping");  
        }  
        return;  
    }  
      
    if(pSong->isPreloaded)    
    {    
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Song already preloaded");    
        return;    
    }    
      
    m_Music.MarkPreloading(pSong->page_url);  
      
    char aBuf[256];    
    str_format(aBuf, sizeof(aBuf), "Starting preload for: '%s - %s'",     
               pSong->title.c_str(), pSong->artist.c_str());    
    Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);    
        
    IHttp *pHttp = Kernel()->RequestInterface<IHttp>();    
    char aDownloadUrl[512];
    FormatMusicBackendUrl(aDownloadUrl, sizeof(aDownloadUrl), "/download");

	char aTargetMapPath[IO_MAX_PATH_LENGTH];
	str_format(aTargetMapPath, sizeof(aTargetMapPath), "maps/%s.map", Server()->GetMapName());
	char aTargetMapAbsolute[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, aTargetMapPath, aTargetMapAbsolute, sizeof(aTargetMapAbsolute));

    auto pJob = std::make_shared<CSongPreloadJob>(this, *pSong, QueueIndex, pHttp, aDownloadUrl, Server()->GetMapName(), aTargetMapAbsolute);
    Engine()->AddJob(pJob);    
        
    UpdateSongPreloadedStatusInQueue(QueueIndex, true);    
}
