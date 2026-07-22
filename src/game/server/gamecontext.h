/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONTEXT_H
#define GAME_SERVER_GAMECONTEXT_H

#include <engine/console.h>
#include <engine/server.h>
#include <set>

#include <game/collision.h>
#include <game/generated/protocol.h>
#include <game/layers.h>
#include <game/mapbugs.h>
#include <game/voting.h>
#include <engine/shared/datafile.h>  
#include <game/mapitems.h>

#include "eventhandler.h"
#include "entities/dice_3d.h"
#include "entities/gomoku_piece.h"
#include "entities/flight_chess.h"
#include "gameworld.h"
#include "music.h"
#include "rainbowname.h"
#include "teehistorian.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector> 
#include <deque>

#include <engine/external/json-parser/json.h>

/*
	Tick
		Game Context (CGameContext::tick)
			Game World (GAMEWORLD::tick)
				Reset world if requested (GAMEWORLD::reset)
				All entities in the world (ENTITY::tick)
				All entities in the world (ENTITY::tick_defered)
				Remove entities marked for deletion (GAMEWORLD::remove_entities)
			Game Controller (GAMECONTROLLER::tick)
			All players (CPlayer::tick)


	Snap
		Game Context (CGameContext::snap)
			Game World (GAMEWORLD::snap)
				All entities in the world (ENTITY::snap)
			Game Controller (GAMECONTROLLER::snap)
			Events handler (EVENT_HANDLER::snap)
			All players (CPlayer::snap)

*/

class CCharacter;
class CFlightChessColorBadge;
class CFlightChessTurnIndicator;
class IConfigManager;
class CConfig;
class CHeap;
class CPlayer;
class CScore;
class CUnpacker;
class IAntibot;
class IGameController;
class IEngine;
class IStorage;
struct CAntibotRoundData;
struct CScoreRandomMapResult;
struct SBadmintonGameState;
class CGameContext;  

struct CSnapContext
{
	CSnapContext(int Version, bool Sixup, int ClientId) :
		m_ClientVersion(Version), m_Sixup(Sixup), m_ClientId(ClientId)
	{
	}

	int GetClientVersion() const { return m_ClientVersion; }
	bool IsSixup() const { return m_Sixup; }
	bool ClientId() const { return m_ClientId; }

private:
	int m_ClientVersion;
	bool m_Sixup;
	int m_ClientId;
};

class CMute
{
public:
	int64_t m_Expire;
	bool m_Initialized = false;
	bool m_InitialDelay;
	char m_aReason[128];

	int SecondsLeft() const;
};

class CMutes
{
public:
	CMutes(const char *pSystemName);

	bool Mute(const NETADDR *pAddr, int Seconds, const char *pReason, bool InitialDelay);
	void UnmuteIndex(int Index);
	void UnmuteAddr(const NETADDR *pAddr);
	void UnmuteExpired();
	std::optional<CMute> IsMuted(const NETADDR *pAddr, bool RespectInitialDelay) const;
	void Print(int Page) const;

private:
	const char *m_pSystemName;
	std::map<NETADDR, CMute> m_Mutes;
};

class CGameContext : public IGameServer
{
	friend class CGomokuPiece;
	friend class CFlightChessPiece;
	friend class CFlightChessColorBadge;
	friend class CFlightChessTurnIndicator;
	friend class CDice3D;

	IServer *m_pServer;
	IConfigManager *m_pConfigManager;
	CConfig *m_pConfig;
	IConsole *m_pConsole;
	IEngine *m_pEngine;
	IStorage *m_pStorage;
	IAntibot *m_pAntibot;
	CLayers m_Layers;
	CCollision m_Collision;
	protocol7::CNetObjHandler m_NetObjHandler7;
	CNetObjHandler m_NetObjHandler;
	CTuningParams m_Tuning;
	CTuningParams m_aTuningList[NUM_TUNEZONES];
	std::vector<std::string> m_vCensorlist;


	bool m_TeeHistorianActive;
	CTeeHistorian m_TeeHistorian;
	ASYNCIO *m_pTeeHistorianFile;
	CUuid m_GameUuid;
	CMapBugs m_MapBugs;
	CPrng m_Prng;

	
	bool m_Resetting;

	static void CommandCallback(int ClientId, int FlagMask, const char *pCmd, IConsole::IResult *pResult, void *pUser);
	static void TeeHistorianWrite(const void *pData, int DataSize, void *pUser);

	static void ConTuneParam(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleTuneParam(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneReset(IConsole::IResult *pResult, void *pUserData);
	static void ConTunes(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneZone(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneDumpZone(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneResetZone(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneSetZoneMsgEnter(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneSetZoneMsgLeave(IConsole::IResult *pResult, void *pUserData);
	static void ConSong(IConsole::IResult *pResult, void *pUserData);
	static void ConQueueSearch(IConsole::IResult *pResult, void *pUserData);
	static void ConQueueInsertAfter(IConsole::IResult *pResult, void *pUserData);
	static void ConQueueList(IConsole::IResult *pResult, void *pUserData);
	static void ConStartLyrics(IConsole::IResult *pResult, void *pUserData);  
	static void ConStopLyrics(IConsole::IResult *pResult, void *pUserData);  
	static void ConLoadLyrics(IConsole::IResult *pResult, void *pUserData);
	static void ConLyricPos(IConsole::IResult *pResult, void *pUserData);
	static void ConMapbug(IConsole::IResult *pResult, void *pUserData);
	static void ConSwitchOpen(IConsole::IResult *pResult, void *pUserData);
	static void ConPause(IConsole::IResult *pResult, void *pUserData);
	static void ConChangeMap(IConsole::IResult *pResult, void *pUserData);
	static void ConRandomMap(IConsole::IResult *pResult, void *pUserData);
	static void ConRandomUnfinishedMap(IConsole::IResult *pResult, void *pUserData);
	static void ConRestart(IConsole::IResult *pResult, void *pUserData);
	static void ConModAlert(IConsole::IResult *pResult, void *pUserData);
	static void ConModAlertAll(IConsole::IResult *pResult, void *pUserData);
	static void ConBroadcast(IConsole::IResult *pResult, void *pUserData);
	static void ConBroadcastId(IConsole::IResult *pResult, void *pUserData);
	static void ConSay(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTeamAll(IConsole::IResult *pResult, void *pUserData);
	static void ConHotReload(IConsole::IResult *pResult, void *pUserData);
	static void ConColor(IConsole::IResult *pResult, void *pUserData);
	static void ConAddVote(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveVote(IConsole::IResult *pResult, void *pUserData);
	static void ConForceVote(IConsole::IResult *pResult, void *pUserData);
	static void ConClearVotes(IConsole::IResult *pResult, void *pUserData);
	static void ConAddMapVotes(IConsole::IResult *pResult, void *pUserData);
	static void ConVote(IConsole::IResult *pResult, void *pUserData);
	static void ConVotes(IConsole::IResult *pResult, void *pUserData);
	static void ConVoteNo(IConsole::IResult *pResult, void *pUserData);
	static void ConDrySave(IConsole::IResult *pResult, void *pUserData);
	static void ConDumpAntibot(IConsole::IResult *pResult, void *pUserData);
	static void ConAntibot(IConsole::IResult *pResult, void *pUserData);
	static void ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainSettingUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConDumpLog(IConsole::IResult *pResult, void *pUserData);
	static void ConChatSong(IConsole::IResult *pResult, void *pUserData);  
	static void ConChatChoose(IConsole::IResult *pResult, void *pUserData);
	static void ConDownloadSong(IConsole::IResult *pResult, void *pUserData);  // 投票通过后下载歌曲  
	static void ConChatSkip(IConsole::IResult *pResult, void *pUserData); // 新增  
	static void ConChatMls(IConsole::IResult *pResult, void *pUserData);       // 显示播放列表  
	static void ConChatQQMsg(IConsole::IResult *pResult, void *pUserData);
	static void ConChatMusicRank(IConsole::IResult *pResult, void *pUserData);
	static void ConChatMyRank(IConsole::IResult *pResult, void *pUserData);
	static void ConChatBindQQ(IConsole::IResult *pResult, void *pUserData);
	static void ConChatBindStatus(IConsole::IResult *pResult, void *pUserData);
	static void ConChatJrrp(IConsole::IResult *pResult, void *pUserData);
	static void ConChatRoll(IConsole::IResult *pResult, void *pUserData);
	static void ConChatPick(IConsole::IResult *pResult, void *pUserData);
	static void ConChatGuess(IConsole::IResult *pResult, void *pUserData);
	static void ConChatGuessRank(IConsole::IResult *pResult, void *pUserData);
	static void ConChatUndercover(IConsole::IResult *pResult, void *pUserData);
	static void ConChatDuel(IConsole::IResult *pResult, void *pUserData);
	static void ConChatGomoku(IConsole::IResult *pResult, void *pUserData);
	static void ConChatConnect4(IConsole::IResult *pResult, void *pUserData);
	static void ConChatFlightChess(IConsole::IResult *pResult, void *pUserData);
	static void ConChid(IConsole::IResult *pResult, void *pUserData);
	static void ConChidGrant(IConsole::IResult *pResult, void *pUserData);
	static void ConChidRevoke(IConsole::IResult *pResult, void *pUserData);
	static void ConChris(IConsole::IResult *pResult, void *pUserData);
	static void ConFlightChessTest(IConsole::IResult *pResult, void *pUserData);
	static void ConDice3D(IConsole::IResult *pResult, void *pUserData);
	static void ConDice3DClear(IConsole::IResult *pResult, void *pUserData);
	static void ConBadmintonBall(IConsole::IResult *pResult, void *pUserData);  
	static void ConBadmintonRed(IConsole::IResult *pResult, void *pUserData);  
	static void ConBadmintonBlue(IConsole::IResult *pResult, void *pUserData);  
	static void ConBadmintonStart(IConsole::IResult *pResult, void *pUserData);
	static void ConBadmintonStatus(IConsole::IResult *pResult, void *pUserData);
	static void ConChatGomokuReady(IConsole::IResult *pResult, void *pUserData);

	void Construct(int Resetting);
	void Destruct(int Resetting);
	void AddVote(const char *pDescription, const char *pCommand);
	static int MapScan(const char *pName, int IsDir, int DirType, void *pUserData);

	static std::string EscapeJsonString(const std::string& sInput)
	{
		std::ostringstream ss;
		for (char c : sInput)
		{
			switch (c)
			{
				case '"': ss << "\\\""; break;
				case '\\': ss << "\\\\"; break;
				case '\b': ss << "\\b"; break;
				case '\f': ss << "\\f"; break;
				case '\n': ss << "\\n"; break;
				case '\r': ss << "\\r"; break;
				case '\t': ss << "\\t"; break;
				default:   ss << c; break;
			}
		}
		return ss.str();
	};

	struct CPersistentData
	{
		CUuid m_PrevGameUuid;
	};

	struct CPersistentClientData
	{
		bool m_IsSpectator;
		bool m_IsAfk;
		int m_LastWhisperTo;
	};
	void SavePlaylistToFile();  
	void LoadPlaylistFromFile();
	class CChrisSprite *FindChrisSprite();
	class CChrisSprite *EnsureChrisSprite(int ClientID);

public:
	IServer *Server() const { return m_pServer; }
	IConfigManager *ConfigManager() const { return m_pConfigManager; }
	CConfig *Config() { return m_pConfig; }
	IConsole *Console() { return m_pConsole; }
	IEngine *Engine() { return m_pEngine; }
	IStorage *Storage() { return m_pStorage; }
	CCollision *Collision() { return &m_Collision; }
	CTuningParams *Tuning() { return &m_Tuning; }
	CTuningParams *TuningList() { return &m_aTuningList[0]; }
	IAntibot *Antibot() { return m_pAntibot; }
	CTeeHistorian *TeeHistorian() { return &m_TeeHistorian; }
	bool TeeHistorianActive() const { return m_TeeHistorianActive; }

	void SendPersonalizedMOTD(int ClientID, const std::vector<SongInfo> &Songs); 
	class IHttp *Http() { return Kernel()->RequestInterface<IHttp>(); }

	void QueueMusicSearchResult(int ClientID, std::vector<SongInfo> vSongs, const char *pError, bool ForVoteMenu);
	void QueueMusicPreloadResult(const SongInfo &Song, int QueueIndex, bool Success, float Duration, const char *pMapName, const char *pPreparedMapPath, const char *pMapSha256, const char *pError);
	void QueueMusicUploadResult(bool Success, const char *pError);
	void QueueQQRelayResult(int ClientID, bool Success, const char *pMessage);
	void QueueQQPollResult(bool Success, std::vector<std::string> vMessages, const char *pError);
	void QueueMusicStatsResult(int ClientID, bool Success, const char *pText);
	void QueueMusicStatsEvent(SMusicEvent Event);
	void QueueIdentityCodeResult(int ClientID, bool Success, const char *pText);
	void QueueServerStateResult(bool Success);
	void QueueTurtleSoupResult(int ClientID, bool Success, const char *pText);

	CRainbowName &RainbowName() { return m_RainbowName; }
	const CRainbowName &RainbowName() const { return m_RainbowName; }

private:
	void ProcessMusicEvents();
	CRainbowName m_RainbowName;
	struct SChidChatGrant
	{
		uint32_t m_UniqueClientId = 0;
		char m_aName[MAX_NAME_LENGTH] = {};
	};
	SChidChatGrant m_aChidChatGrants[MAX_CLIENTS];
	bool HasChidChatGrant(int ClientId);
	void ClearChidChatGrant(int ClientId);
	void ExecuteChid(int ClientId, const char *pText);

	CMusicState m_Music;
	static constexpr int ADMIN_MUSIC_SEARCH_CLIENT_ID = -2;
	int m_LastMusicVoteMenuProgressRefreshTick = 0;
	CSongCooldowns m_QQMessageCooldowns;
	int64_t m_LastQQRelayTime = 0;
	bool m_QQPollInFlight = false;
	int m_LastQQPollTick = 0;
	bool m_ServerStateUpdateInFlight = false;
	int m_LastServerStateUpdateTick = 0;
	int m_TurtleSoupDummyClientID = -1;
	struct CUndercoverRoomRuntime
	{
		int m_Team = -1;
		std::string m_Phase;
		std::string m_SpeakerId;
		std::string m_SpeakerName;
		int64_t m_DeadlineTick = 0;
		int m_LastReminderSecond = -1;
		bool m_TimeoutRequested = false;
	};
	std::map<std::string, CUndercoverRoomRuntime> m_UndercoverRooms;
	std::string m_aUndercoverRoom[MAX_CLIENTS];
	int m_aUndercoverTeam[MAX_CLIENTS] = {};
	int m_aUndercoverPreviousTeam[MAX_CLIENTS] = {};
	int EnsureUndercoverTeam(const std::string &Room);
	void ApplyUndercoverEvent(const SMusicEvent &Event);
	void ClearUndercoverPlayer(int ClientID, bool RestoreTeam);
	bool HandleUndercoverChat(const CNetMsg_Cl_Say *pMsg, int ClientId);
	void CheckUndercoverTimers();
	bool HandleTurtleSoupChat(const CNetMsg_Cl_Say *pMsg, int ClientId);
	void RefreshTurtleSoupDummyActivity();

	// Lightweight DM-style duels. These only change combat between the two
	// participants; their DDRace team, position and visibility stay unchanged.
	int m_aDuelOpponent[MAX_CLIENTS];
	int m_aDuelWeapons[MAX_CLIENTS];
	int m_aDuelRequester[MAX_CLIENTS];
	int m_aDuelRequestTick[MAX_CLIENTS];
	int m_aDuelRequestWeapons[MAX_CLIENTS];
	int m_aDuelTuningRestoreTick[MAX_CLIENTS];
	void TickDuels();
	void EndDuel(int WinnerId, int LoserId, const char *pReason);
	void CancelDuelRequest(int TargetId, const char *pReason);
	int FindOutgoingDuelRequest(int ClientId) const;
	bool IsDuelReadyPlayer(int ClientId) const;

	struct SGomokuGame
	{
		int m_Black = -1;
		int m_White = -1;
		int m_ReadyMask = 0;
		int m_Turn = -1;
		int m_DeadlineTick = 0;
		std::vector<std::pair<int, int>> m_vBlack;
		std::vector<std::pair<int, int>> m_vWhite;
		std::vector<CGomokuPiece *> m_vStones;
		std::vector<CGomokuPiece *> m_vCountdown;
		std::vector<CGomokuPiece *> m_vVictoryVisuals;
		std::vector<CGomokuPiece *> m_vVictoryLines;
		std::vector<vec2> m_vVictoryStarts;
		std::vector<std::pair<int, int>> m_vWinningLine;
		int m_VictoryWinner = -1;
		int m_VictoryLoser = -1;
		int m_VictoryStartTick = 0;
		CGomokuPiece *m_pPreview = nullptr;
	};
	// Logical-only state retained across `hot_reload`. Entity pointers are never
	// persisted: they are rebuilt after the saved tees have spawned again.
	struct SGomokuSavedGame
	{
		bool m_Active = false;
		int m_Black = -1;
		int m_White = -1;
		int m_ReadyMask = 0;
		int m_Turn = -1;
		int m_RemainingTurnTicks = 0;
		std::vector<std::pair<int, int>> m_vBlack;
		std::vector<std::pair<int, int>> m_vWhite;
		std::vector<std::pair<int, int>> m_vWinningLine;
		int m_VictoryWinner = -1;
		int m_VictoryElapsedTicks = 0;
	};
	SGomokuGame m_aGomokuGames[NUM_DDRACE_TEAMS];
	SGomokuSavedGame *m_aSavedGomokuGames[NUM_DDRACE_TEAMS];
	int m_aGomokuRequester[MAX_CLIENTS];
	int m_aGomokuRequestTick[MAX_CLIENTS];
	void TickGomoku();
	void ClearGomokuGame(int Team, int Winner, const char *pReason);
	void AbortGomokuFor(int ClientId, const char *pReason);
	bool HandleGomokuStart(int ClientId);
	bool TryGomokuPlace(int ClientId);
	int FindGomokuTeam(int ClientId);
	int FindOutgoingGomokuRequest(int ClientId);
	bool IsGomokuReadyPlayer(int ClientId);
	bool IsGomokuBoardCell(int GridX, int GridY);
	bool GomokuAimCell(int ClientId, int *pGridX, int *pGridY);
	vec2 GomokuSpawn(int Tile);
	void UpdateGomokuPreview(int Team);
	void RebuildGomokuCountdown(int Team);
	void UpdateGomokuCountdown(int Team);
	void StartGomokuVictory(int Team, int Winner, const std::vector<std::pair<int, int>> &vWinningLine);
	void TickGomokuVictory(int Team);
	void FinishGomokuVictory(int Team);
	void SaveGomokuHotReloadState();
	void RestoreGomokuHotReloadState();
	void CancelGomokuRequest(int TargetId, const char *pReason);
	void OnGomokuPieceDestroyed(CGomokuPiece *pPiece);
	std::vector<CGomokuPiece *> m_vFlightChessTestHeads;
	void ToggleFlightChessTestHeads();

	struct SFlightChessGame
	{
		int m_aPlayers[4]{-1, -1, -1, -1}; // slot -> client id; slot is also a unique colour.
		int m_TurnSlot = -1;
		int m_ReadyMask = 0;
		int m_Phase = 0; // -1 ready, 0 waiting for die, 1 rolling, 2 selecting, 3 moving.
		int m_DeadlineTick = 0;
		int m_DieValue = 0;
		int m_aPlaneProgress[4][4]{}; // -1 hangar, 0..49 route, 50..55 home lane, 56 finished.
		std::vector<CFlightChessPiece *> m_vPlanes;
		CFlightChessColorBadge *m_apColorBadges[4]{};
		CDice3D *m_pDice = nullptr;
		CFlightChessTurnIndicator *m_pTurnIndicator = nullptr;
		int m_MovingPlane = -1;
		std::vector<int> m_vMovePath;
		int m_MovePathIndex = 0;
		int m_FastMovePathIndex = -1;
		int m_NextMoveTick = 0;
		bool m_MoveFinishes = false;
	};
	struct SFlightChessInvite
	{
		bool m_Active = false;
		int m_Inviter = -1;
		int m_aPlayers[4]{-1, -1, -1, -1};
		bool m_aAccepted[4]{};
		int m_ExpireTick = 0;
	};
	struct SFlightChessSavedGame
	{
		bool m_Active = false;
		int m_aPlayers[4]{-1, -1, -1, -1};
		int m_TurnSlot = -1;
		int m_ReadyMask = 0;
		int m_Phase = 0;
		int m_DieValue = 0;
		int m_RemainingTicks = 0;
		int m_aPlaneProgress[4][4]{};
		CDice3D::SPersistentState m_DiceState;
	};
	SFlightChessGame m_aFlightChessGames[NUM_DDRACE_TEAMS];
	SFlightChessSavedGame *m_aSavedFlightChessGames[NUM_DDRACE_TEAMS];
	SFlightChessInvite m_aFlightChessInvites[MAX_CLIENTS];
	void TickFlightChess();
	void AbortFlightChessFor(int ClientId, const char *pReason);
	bool TryFlightChessClick(int ClientId);
	int FindFlightChessTeam(int ClientId);
	bool IsFlightChessReadyPlayer(int ClientId);
	bool IsFlightChessTeamActive(int Team) const;
	bool IsFlightChessParticipant(int Team, int ClientId) const;
	void ClearFlightChessGame(int Team, int WinnerSlot, const char *pReason);
	void RebuildFlightChessEntities(int Team);
	bool HandleFlightChessReady(int ClientId);
	void UpdateFlightChessTurnIndicator(int Team);
	void UpdateFlightChessPlanePositions(int Team);
	void AdvanceFlightChessTurn(int Team, bool KeepCurrentPlayer);
	void FinishFlightChessMove(int Team);
	void SaveFlightChessHotReloadState();
	void RestoreFlightChessHotReloadState();
	void OnFlightChessEntityDestroyed(CFlightChessPiece *pPiece);
	void OnFlightChessColorBadgeDestroyed(CFlightChessColorBadge *pBadge);
	void OnFlightChessIndicatorDestroyed(CFlightChessTurnIndicator *pIndicator);
	void OnFlightChessDiceDestroyed(CDice3D *pDice);
	void OnFlightChessDiceSettled(CDice3D *pDice);
	bool CanHammerFlightChessDice(const CDice3D *pDice, int ClientId) const;
	void OnFlightChessDiceThrown(CDice3D *pDice, int ClientId);

	struct SConnect4Game
	{
		int m_Black = -1;
		int m_White = -1;
		int m_ReadyMask = 0;
		int m_Turn = -1;
		int m_DeadlineTick = 0;
		int m_LeftGridX = 0;
		int m_BottomGridY = 0;
		int m_aBoard[6][7]{}; // 0 empty, 1 black, 2 white; row 0 is bottom.
		std::vector<CGomokuPiece *> m_vStones;
		std::vector<CGomokuPiece *> m_vCountdown;
		std::vector<CGomokuPiece *> m_vBorders;
		CGomokuPiece *m_pPreview = nullptr;
		CGomokuPiece *m_pFallingStone = nullptr;
		vec2 m_FallingPos{};
		vec2 m_FallingTarget{};
		float m_FallingVelocity = 0.0f;
		int m_FallingCol = -1;
		int m_FallingRow = -1;
		int m_FallingColor = 0;
	};
	struct SConnect4SavedGame
	{
		bool m_Active = false;
		int m_Black = -1;
		int m_White = -1;
		int m_ReadyMask = 0;
		int m_Turn = -1;
		int m_RemainingTurnTicks = 0;
		int m_LeftGridX = 0;
		int m_BottomGridY = 0;
		int m_aBoard[6][7]{};
		bool m_StoneFalling = false;
		vec2 m_FallingPos{};
		float m_FallingVelocity = 0.0f;
		int m_FallingCol = -1;
		int m_FallingRow = -1;
		int m_FallingColor = 0;
	};
	SConnect4Game m_aConnect4Games[NUM_DDRACE_TEAMS];
	SConnect4SavedGame *m_aSavedConnect4Games[NUM_DDRACE_TEAMS];
	int m_aConnect4Requester[MAX_CLIENTS];
	int m_aConnect4RequestTick[MAX_CLIENTS];
	void TickConnect4();
	void ClearConnect4Game(int Team, int Winner, const char *pReason);
	void AbortConnect4For(int ClientId, const char *pReason);
	bool HandleConnect4Ready(int ClientId);
	bool TryConnect4Place(int ClientId);
	int FindConnect4Team(int ClientId);
	int FindOutgoingConnect4Request(int ClientId);
	bool GetConnect4BoardBounds(int *pLeftGridX, int *pBottomGridY);
	void RebuildConnect4Countdown(int Team);
	void UpdateConnect4Countdown(int Team);
	void UpdateConnect4Preview(int Team);
	bool Connect4AimTarget(int ClientId, vec2 *pTarget);
	void FinishConnect4Drop(int Team);
	void CreateConnect4Borders(int Team);
	void SaveConnect4HotReloadState();
	void RestoreConnect4HotReloadState();
	void CancelConnect4Request(int TargetId, const char *pReason);
	struct SChiGlyph
	{
		std::vector<std::vector<vec2>> m_vStrokes;
		float m_Advance = 1.0f;
	};
	std::map<int, SChiGlyph> m_ChiGlyphs;
	bool m_ChiGlyphsLoaded = false;
	bool LoadChiGlyphs();
	int CountChiDots(int ClientID = -1);
	void ClearChiDots(int ClientID);
	int SpawnChiDynamicText(int ClientID, const char *pText, float SizeScale, int *pUsedChars, int *pMissingChars);
	int SpawnLyricChidCharacter(const char *pCharacterStart, int Bytes, vec2 Pos, float SizeScale, int LifeSpanTicks, float *pAdvance);
	struct SLyricChid
	{
		char m_aText[256];
		int m_NextByte;
		int m_NextShowTick;
		vec2 m_Pos;
		float m_NextXOffset;
		int m_CharacterIntervalTicks;
		int m_StartTick;
		int m_LineDurationTicks;
		int m_NextCharacterIndex;
		std::vector<int> m_vCharacterStartOffsets;
	};
	SLyricChid m_LyricChid;
	void ClearLyricChid();
	void StartLyricChid(const char *pText, int LineDurationTicks, const std::vector<int> &vCharacterStartOffsets = {});
	void TickLyricChid();
	void AddToPlaylist(const SongInfo &Song);  // 添加歌曲到队列  
	void ShowPlaylist(int ClientID);           // 显示播放队列

	void StartPreloadingSong(int QueueIndex);     // 开始预加载指定位置的歌曲  
	void CheckSongTransition();                   // 检查是否需要切歌  
	void PlayNextSong();                          // 播放下一首歌曲  
	void BeginQueueSongPlayback(int QueueIndex, const SongInfo &Song, int64_t StartTime, bool ReloadMap, bool AutoStartLyrics);
	SongInfo* GetQueuedSong(int Index);           // 获取队列中指定位置的歌曲  
	void InitializeQueuePlayback();               // 初始化队列播放  
	void UpdateSongInQueue(int Index, float Duration) ; // 更新队列中的歌曲信息
	void UpdateSongPreloadedStatusInQueue(int Index, bool IsPreloaded);
	void RestoreQueuePlaybackState();                                    // 恢复队列播放状态  
	void ProcessPreloadedSong(const SongInfo &Song, float Duration, int QueueIndex, const char *pMapName, const char *pPreparedMapPath, const char *pMapSha256); // 处理预加载完成的歌曲
	bool InstallPreparedMusicMap(const char *pPreparedMapPath);
	void ValidateQueueState();                                               // 验证队列状态  
	void HandlePreloadFailure(int QueueIndex, const char *pReason, const SongInfo *pSong = nullptr);
	static void ConQueueSkip(IConsole::IResult *pResult, void *pUserData);     // 跳过当前歌曲命令  
	static void ConQueueRestart(IConsole::IResult *pResult, void *pUserData);  // 重启队列命令  
	void LogQueueState(const char *pContext);         

	void LoadLyrics(const std::string& songId);  
	void CheckAndSendLyrics();  
	void StartLyrics(bool Announce = true);
	void StopLyrics();  
	void SaveLyricsState();  
	void LoadLyricsState();  
	bool EmbedSongAudioIntoCurrentMap(const char *pSongId);

public:
	CGameContext();
	CGameContext(int Reset);
	~CGameContext();

	void Clear();

private:
	bool ModifyMapWithAudio(const char *pOriginMapPath, const char *pTargetMapPath, const char *pSoundName, void *pAudioData, unsigned AudioDataSize, bool bGenerateWebMap = true);
	void RequestSongSearch(int ClientID, const char *pSongName, bool ForVoteMenu = false);
	void RequestUploadToObjectStorage(const char *pMapName, const char *pSha256);
	void RequestQQRelay(int ClientID, const char *pPlayerName, const char *pMessage);
	void PollQQMessages();
	void RequestMusicHistoryRecord(const SongInfo &Song);
	void RequestMusicStats(int ClientID, bool Personal);
	void RequestGuess(int ClientID, const char *pAnswer);
	void RequestGuessRank(int ClientID);
	void RequestUndercover(int ClientID, const char *pAction, const char *pRoom, const char *pArg);
	void RequestTurtleSoup(int ClientID, const char *pMessage);
	void RequestQQBindCode(int ClientID);
	void RequestQQBindStatus(int ClientID);
	void UpdateBackendServerState();
	int GetMusicVoteOptionCount(int ClientId) const;
	bool GetMusicVoteOption(int ClientId, int Index, char *pBuf, int BufSize) const;
	bool GetVoteOptionDescriptionForClient(int ClientId, int Index, char *pBuf, int BufSize) const;
	void RefreshVoteOptions(int ClientId);
	void RefreshMusicVoteMenus(int ClientId);
	bool IsMusicSearchVoteOption(const char *pValue) const;
	bool IsMusicSkipVoteOption(const char *pValue) const;
	bool IsMusicMenuVoteOption(int ClientId, const char *pValue) const;
	bool HandleMusicSearchVoteOption(int ClientId, const char *pReason);
	bool HandleMusicSkipVoteOption(int ClientId, const char *pReason);
	bool HandleMusicChooseVoteOption(int ClientId, const char *pValue, const char *pReason);

public:

	CEventHandler m_Events;
	CPlayer *m_apPlayers[MAX_CLIENTS];
	// keep last input to always apply when none is sent
	CNetObj_PlayerInput m_aLastPlayerInput[MAX_CLIENTS];
	bool m_aPlayerHasInput[MAX_CLIENTS];
	CSaveTeam *m_apSavedTeams[MAX_CLIENTS];
	CSaveHotReloadTee *m_apSavedTees[MAX_CLIENTS];
	int m_aTeamMapping[MAX_CLIENTS];

	// returns last input if available otherwise nulled PlayerInput object
	// ClientId has to be valid
	CNetObj_PlayerInput GetLastPlayerInput(int ClientId) const;

	IGameController *m_pController;
	CGameWorld m_World;

	// helper functions
	class CCharacter *GetPlayerChar(int ClientId);
	bool EmulateBug(int Bug) const;
	bool IsDuelParticipant(int ClientId) const;
	bool AreDuelOpponents(int FirstClientId, int SecondClientId) const;
	bool IsDuelWeaponAllowed(int ClientId, int Weapon) const;
	void QueueDuelTuningRestore(int ClientId);
	// Returns true when duel logic consumed the hit (including intentionally
	// ignored hits involving a duelist).
	bool HandleDuelDamage(CCharacter *pVictim, vec2 Force, int Dmg, int From, int Weapon);
	void AbortDuelFor(int ClientId, const char *pReason);
	bool TryGomokuPlaceFromFire(int ClientId) { return TryFlightChessClick(ClientId) || TryGomokuPlace(ClientId) || TryConnect4Place(ClientId); }
	int HammerDice3D(int HammererClientId, vec2 HammerPos, vec2 HammererPos, CClientMask Mask);
	std::vector<SSwitchers> &Switchers() { return m_World.m_Core.m_vSwitchers; }

	enum EGameInviteVoteType
	{
		GAME_INVITE_VOTE_NONE = 0,
		GAME_INVITE_VOTE_DUEL,
		GAME_INVITE_VOTE_GOMOKU,
		GAME_INVITE_VOTE_CONNECT4,
		GAME_INVITE_VOTE_FLIGHT_CHESS,
	};
	struct SGameInviteVote
	{
		EGameInviteVoteType m_Type = GAME_INVITE_VOTE_NONE;
		int m_Inviter = -1;
		int m_ExpireTick = 0;
	};
	SGameInviteVote m_aGameInviteVotes[MAX_CLIENTS];
	bool HasGameInviteVote(int ClientId) const;
	bool ShowGameInviteVote(int ClientId, int Inviter, EGameInviteVoteType Type, int ExpireTick);
	void CloseGameInviteVote(int ClientId);
	void TickGameInviteVotes();
	bool HandleGameInviteVote(int ClientId, int Vote);
	void SendGameInviteVote(int ClientId);
	bool IsGameInviteStillPending(int ClientId, const SGameInviteVote &Invite) const;

	// voting
	void StartVote(const char *pDesc, const char *pCommand, const char *pReason, const char *pSixupDesc);
	void EndVote();
	void SendVoteSet(int ClientId);
	void SendVoteStatus(int ClientId, int Total, int Yes, int No);
	void AbortVoteKickOnDisconnect(int ClientId);

	int m_VoteCreator;
	int m_VoteType;
	int64_t m_VoteCloseTime;
	bool m_VoteUpdate;
	int m_VotePos;
	char m_aVoteDescription[VOTE_DESC_LENGTH];
	char m_aSixupVoteDescription[VOTE_DESC_LENGTH];
	char m_aVoteCommand[VOTE_CMD_LENGTH];
	char m_aVoteReason[VOTE_REASON_LENGTH];
	int m_NumVoteOptions;
	int m_VoteEnforce;
	char m_aaZoneEnterMsg[NUM_TUNEZONES][256]; // 0 is used for switching from or to area without tunings
	char m_aaZoneLeaveMsg[NUM_TUNEZONES][256];

	void CreateAllEntities(bool Initial);
	CPlayer *CreatePlayer(int ClientId, int StartTeam, bool Afk, int LastWhisperTo);

	char m_aDeleteTempfile[128];
	void DeleteTempfile();

	enum
	{
		VOTE_ENFORCE_UNKNOWN = 0,
		VOTE_ENFORCE_NO,
		VOTE_ENFORCE_YES,
		VOTE_ENFORCE_NO_ADMIN,
		VOTE_ENFORCE_YES_ADMIN,
		VOTE_ENFORCE_ABORT,
		VOTE_ENFORCE_CANCEL,
	};
	CHeap *m_pVoteOptionHeap;
	CVoteOptionServer *m_pVoteOptionFirst;
	CVoteOptionServer *m_pVoteOptionLast;

	// helper functions
	void CreateDamageInd(vec2 Pos, float AngleMod, int Amount, CClientMask Mask = CClientMask().set());
	void CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage, int ActivatedTeam, CClientMask Mask = CClientMask().set());
	void CreateHammerHit(vec2 Pos, CClientMask Mask = CClientMask().set());
	void CreatePlayerSpawn(vec2 Pos, CClientMask Mask = CClientMask().set());
	void CreateDeath(vec2 Pos, int ClientId, CClientMask Mask = CClientMask().set());
	void CreateBirthdayEffect(vec2 Pos, CClientMask Mask = CClientMask().set());
	void CreateFinishEffect(vec2 Pos, CClientMask Mask = CClientMask().set());
	void CreateSound(vec2 Pos, int Sound, CClientMask Mask = CClientMask().set());
	void CreateSoundGlobal(int Sound, int Target = -1) const;

	void SnapSwitchers(int SnappingClient);
	bool SnapLaserObject(const CSnapContext &Context, int SnapId, const vec2 &To, const vec2 &From, int StartTick, int Owner = -1, int LaserType = -1, int Subtype = -1, int SwitchNumber = -1) const;
	bool SnapPickup(const CSnapContext &Context, int SnapId, const vec2 &Pos, int Type, int SubType, int SwitchNumber, int Flags) const;

	enum
	{
		FLAG_SIX = 1 << 0,
		FLAG_SIXUP = 1 << 1,
	};

	// network
	void CallVote(int ClientId, const char *pDesc, const char *pCmd, const char *pReason, const char *pChatmsg, const char *pSixupDesc = nullptr);
	void SendChatTarget(int To, const char *pText, int VersionFlags = FLAG_SIX | FLAG_SIXUP) const;
	void SendChatTeam(int Team, const char *pText) const;
	void SendChat(int ClientId, int Team, const char *pText, int SpamProtectionClientId = -1, int VersionFlags = FLAG_SIX | FLAG_SIXUP);
	void SendStartWarning(int ClientId, const char *pMessage);
	void SendEmoticon(int ClientId, int Emoticon, int TargetClientId) const;
	void SendWeaponPickup(int ClientId, int Weapon) const;
	void SendMotd(int ClientId) const;
	void SendSettings(int ClientId) const;
	void SendModeratorAlert(int ToClientId, const char *pMessage);
	void SendBroadcast(const char *pText, int ClientId, bool IsImportant = true);

	void List(int ClientId, const char *pFilter);

	//
	void CheckPureTuning();
	void SendTuningParams(int ClientId, int Zone = 0);

	const CVoteOptionServer *GetVoteOption(int Index) const;
	void ProgressVoteOptions(int ClientId);

	//
	void LoadMapSettings();

	// engine events
	void OnInit(const void *pPersistentData) override;
	void OnConsoleInit() override;
	void RegisterDDRaceCommands();
	void RegisterChatCommands();
	[[nodiscard]] bool OnMapChange(char *pNewMapName, int MapNameSize) override;
	void OnShutdown(void *pPersistentData) override;

	void OnTick() override;
	void OnSnap(int ClientId, bool GlobalSnap) override;
	void OnPostGlobalSnap() override;

	void UpdatePlayerMaps();

	void *PreProcessMsg(int *pMsgId, CUnpacker *pUnpacker, int ClientId);
	void CensorMessage(char *pCensoredMessage, const char *pMessage, int Size);
	void OnMessage(int MsgId, CUnpacker *pUnpacker, int ClientId) override;
	void OnSayNetMessage(const CNetMsg_Cl_Say *pMsg, int ClientId, const CUnpacker *pUnpacker);
	void OnCallVoteNetMessage(const CNetMsg_Cl_CallVote *pMsg, int ClientId);
	void OnVoteNetMessage(const CNetMsg_Cl_Vote *pMsg, int ClientId);
	void OnSetTeamNetMessage(const CNetMsg_Cl_SetTeam *pMsg, int ClientId);
	void OnIsDDNetLegacyNetMessage(const CNetMsg_Cl_IsDDNetLegacy *pMsg, int ClientId, CUnpacker *pUnpacker);
	void OnShowOthersLegacyNetMessage(const CNetMsg_Cl_ShowOthersLegacy *pMsg, int ClientId);
	void OnShowOthersNetMessage(const CNetMsg_Cl_ShowOthers *pMsg, int ClientId);
	void OnShowDistanceNetMessage(const CNetMsg_Cl_ShowDistance *pMsg, int ClientId);
	void OnCameraInfoNetMessage(const CNetMsg_Cl_CameraInfo *pMsg, int ClientId);
	void OnSetSpectatorModeNetMessage(const CNetMsg_Cl_SetSpectatorMode *pMsg, int ClientId);
	void OnChangeInfoNetMessage(const CNetMsg_Cl_ChangeInfo *pMsg, int ClientId);
	void OnEmoticonNetMessage(const CNetMsg_Cl_Emoticon *pMsg, int ClientId);
	void OnKillNetMessage(const CNetMsg_Cl_Kill *pMsg, int ClientId);
	void OnStartInfoNetMessage(const CNetMsg_Cl_StartInfo *pMsg, int ClientId);

	bool OnClientDataPersist(int ClientId, void *pData) override;
	void OnClientConnected(int ClientId, void *pData) override;
	void OnClientEnter(int ClientId) override;
	void OnClientDrop(int ClientId, const char *pReason) override;
	void OnClientPrepareInput(int ClientId, void *pInput) override;
	void OnClientDirectInput(int ClientId, const void *pInput) override;
	void OnClientPredictedInput(int ClientId, const void *pInput) override;
	void OnClientPredictedEarlyInput(int ClientId, const void *pInput) override;

	void PreInputClients(int ClientId, bool *pClients) override;

	void TeehistorianRecordAntibot(const void *pData, int DataSize) override;
	void TeehistorianRecordPlayerJoin(int ClientId, bool Sixup) override;
	void TeehistorianRecordPlayerDrop(int ClientId, const char *pReason) override;
	void TeehistorianRecordPlayerRejoin(int ClientId) override;
	void TeehistorianRecordPlayerName(int ClientId, const char *pName) override;
	void TeehistorianRecordPlayerFinish(int ClientId, int TimeTicks) override;
	void TeehistorianRecordTeamFinish(int TeamId, int TimeTicks) override;

	bool IsClientReady(int ClientId) const override;
	bool IsClientPlayer(int ClientId) const override;
	// Whether the client is allowed to have high bandwidth.
	bool IsClientHighBandwidth(int ClientId) const override;
	int PersistentDataSize() const override { return sizeof(CPersistentData); }
	int PersistentClientDataSize() const override { return sizeof(CPersistentClientData); }

	CUuid GameUuid() const override;
	const char *GameType() const override;
	const char *Version() const override;
	const char *NetVersion() const override;

	// DDRace
	void OnPreTickTeehistorian() override;
	bool OnClientDDNetVersionKnown(int ClientId);
	void FillAntibot(CAntibotRoundData *pData) override;
	bool ProcessSpamProtection(int ClientId, bool RespectChatInitialDelay = true);
	int GetDDRaceTeam(int ClientId) const;
	// Describes the time when the first player joined the server.
	int64_t m_NonEmptySince;
	int64_t m_LastMapVote;
	int GetClientVersion(int ClientId) const;
	CClientMask ClientsMaskExcludeClientVersionAndHigher(int Version) const;
	bool PlayerExists(int ClientId) const override { return m_apPlayers[ClientId]; }
	// Returns true if someone is actively moderating.
	bool PlayerModerating() const;
	void ForceVote(int EnforcerId, bool Success);

	// Checks if player can vote and notify them about the reason
	bool RateLimitPlayerVote(int ClientId);
	bool RateLimitPlayerMapVote(int ClientId) const;

	void OnUpdatePlayerServerInfo(CJsonStringWriter *pJSonWriter, int Id) override;
	void ReadCensorList();

	bool PracticeByDefault() const;
	void SendSkinChangeMessage(int ClientId);

	std::shared_ptr<CScoreRandomMapResult> m_SqlRandomMapResult;

private:
	// starting 1 to make 0 the special value "no client id"
	uint32_t m_NextUniqueClientId = 1;
	bool m_VoteWillPass;
	CScore *m_pScore;
	SBadmintonGameState *m_aSavedBadmintonStates[NUM_DDRACE_TEAMS];  

	// DDRace Console Commands

	static void ConKillPlayer(IConsole::IResult *pResult, void *pUserData);

	static void ConNinja(IConsole::IResult *pResult, void *pUserData);
	static void ConUnNinja(IConsole::IResult *pResult, void *pUserData);
	static void ConEndlessHook(IConsole::IResult *pResult, void *pUserData);
	static void ConUnEndlessHook(IConsole::IResult *pResult, void *pUserData);
	static void ConSolo(IConsole::IResult *pResult, void *pUserData);
	static void ConUnSolo(IConsole::IResult *pResult, void *pUserData);
	static void ConFreeze(IConsole::IResult *pResult, void *pUserData);
	static void ConUnFreeze(IConsole::IResult *pResult, void *pUserData);
	static void ConDeep(IConsole::IResult *pResult, void *pUserData);
	static void ConUnDeep(IConsole::IResult *pResult, void *pUserData);
	static void ConLiveFreeze(IConsole::IResult *pResult, void *pUserData);
	static void ConUnLiveFreeze(IConsole::IResult *pResult, void *pUserData);
	static void ConUnSuper(IConsole::IResult *pResult, void *pUserData);
	static void ConSuper(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleInvincible(IConsole::IResult *pResult, void *pUserData);
	static void ConShotgun(IConsole::IResult *pResult, void *pUserData);
	static void ConGrenade(IConsole::IResult *pResult, void *pUserData);
	static void ConLaser(IConsole::IResult *pResult, void *pUserData);
	static void ConJetpack(IConsole::IResult *pResult, void *pUserData);
	static void ConEndlessJump(IConsole::IResult *pResult, void *pUserData);
	static void ConSetJumps(IConsole::IResult *pResult, void *pUserData);
	static void ConWeapons(IConsole::IResult *pResult, void *pUserData);
	static void ConUnShotgun(IConsole::IResult *pResult, void *pUserData);
	static void ConUnGrenade(IConsole::IResult *pResult, void *pUserData);
	static void ConUnLaser(IConsole::IResult *pResult, void *pUserData);
	static void ConUnJetpack(IConsole::IResult *pResult, void *pUserData);
	static void ConUnEndlessJump(IConsole::IResult *pResult, void *pUserData);
	static void ConUnWeapons(IConsole::IResult *pResult, void *pUserData);
	static void ConAddWeapon(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveWeapon(IConsole::IResult *pResult, void *pUserData);
	void ModifyWeapons(IConsole::IResult *pResult, void *pUserData, int Weapon, bool Remove);
	void MoveCharacter(int ClientId, int X, int Y, bool Raw = false);
	static void ConGoLeft(IConsole::IResult *pResult, void *pUserData);
	static void ConGoRight(IConsole::IResult *pResult, void *pUserData);
	static void ConGoUp(IConsole::IResult *pResult, void *pUserData);
	static void ConGoDown(IConsole::IResult *pResult, void *pUserData);
	static void ConMove(IConsole::IResult *pResult, void *pUserData);
	static void ConMoveRaw(IConsole::IResult *pResult, void *pUserData);
	static void ConQueueStatus(IConsole::IResult *pResult, void *pUserData);  // 队列状态命令  
	static void ConQueueClear(IConsole::IResult *pResult, void *pUserData);   // 清空队列命令  
	static void ConQueueRemove(IConsole::IResult *pResult, void *pUserData);

	static void ConToTeleporter(IConsole::IResult *pResult, void *pUserData);
	static void ConToCheckTeleporter(IConsole::IResult *pResult, void *pUserData);
	void Teleport(CCharacter *pChr, vec2 Pos);
	static void ConTeleport(IConsole::IResult *pResult, void *pUserData);

	static void ConCredits(IConsole::IResult *pResult, void *pUserData);
	static void ConInfo(IConsole::IResult *pResult, void *pUserData);
	static void ConHelp(IConsole::IResult *pResult, void *pUserData);
	static void ConSettings(IConsole::IResult *pResult, void *pUserData);
	static void ConRules(IConsole::IResult *pResult, void *pUserData);
	static void ConKill(IConsole::IResult *pResult, void *pUserData);
	static void ConTogglePause(IConsole::IResult *pResult, void *pUserData);
	static void ConTogglePauseVoted(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleSpec(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleSpecVoted(IConsole::IResult *pResult, void *pUserData);
	static void ConForcePause(IConsole::IResult *pResult, void *pUserData);
	static void ConTeamTop5(IConsole::IResult *pResult, void *pUserData);
	static void ConTop(IConsole::IResult *pResult, void *pUserData);
	static void ConTimes(IConsole::IResult *pResult, void *pUserData);
	static void ConPoints(IConsole::IResult *pResult, void *pUserData);
	static void ConTopPoints(IConsole::IResult *pResult, void *pUserData);
	static void ConTimeCP(IConsole::IResult *pResult, void *pUserData);

	static void ConDND(IConsole::IResult *pResult, void *pUserData);
	static void ConWhispers(IConsole::IResult *pResult, void *pUserData);
	static void ConMapInfo(IConsole::IResult *pResult, void *pUserData);
	static void ConTimeout(IConsole::IResult *pResult, void *pUserData);
	static void ConPractice(IConsole::IResult *pResult, void *pUserData);
	static void ConUnPractice(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeCmdList(IConsole::IResult *pResult, void *pUserData);
	static void ConSwap(IConsole::IResult *pResult, void *pUserData);
	static void ConCancelSwap(IConsole::IResult *pResult, void *pUserData);
	static void ConSave(IConsole::IResult *pResult, void *pUserData);
	static void ConLoad(IConsole::IResult *pResult, void *pUserData);
	static void ConMap(IConsole::IResult *pResult, void *pUserData);
	static void ConTeamRank(IConsole::IResult *pResult, void *pUserData);
	static void ConRank(IConsole::IResult *pResult, void *pUserData);
	static void ConTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConLock(IConsole::IResult *pResult, void *pUserData);
	static void ConUnlock(IConsole::IResult *pResult, void *pUserData);
	static void ConInvite(IConsole::IResult *pResult, void *pUserData);
	static void ConJoin(IConsole::IResult *pResult, void *pUserData);
	static void ConTeam0Mode(IConsole::IResult *pResult, void *pUserData);
	static void ConMe(IConsole::IResult *pResult, void *pUserData);
	static void ConWhisper(IConsole::IResult *pResult, void *pUserData);
	static void ConConverse(IConsole::IResult *pResult, void *pUserData);
	static void ConSetEyeEmote(IConsole::IResult *pResult, void *pUserData);
	static void ConEyeEmote(IConsole::IResult *pResult, void *pUserData);
	static void ConShowOthers(IConsole::IResult *pResult, void *pUserData);
	static void ConShowAll(IConsole::IResult *pResult, void *pUserData);
	static void ConSpecTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConNinjaJetpack(IConsole::IResult *pResult, void *pUserData);
	static void ConSayTime(IConsole::IResult *pResult, void *pUserData);
	static void ConSayTimeAll(IConsole::IResult *pResult, void *pUserData);
	static void ConTime(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTimerType(IConsole::IResult *pResult, void *pUserData);
	static void ConRescue(IConsole::IResult *pResult, void *pUserData);
	static void ConRescueMode(IConsole::IResult *pResult, void *pUserData);
	static void ConBack(IConsole::IResult *pResult, void *pUserData);
	static void ConTeleTo(IConsole::IResult *pResult, void *pUserData);
	static void ConTeleXY(IConsole::IResult *pResult, void *pUserData);
	static void ConTeleCursor(IConsole::IResult *pResult, void *pUserData);
	static void ConLastTele(IConsole::IResult *pResult, void *pUserData);

	// Chat commands for practice mode
	static void ConPracticeToTeleporter(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeToCheckTeleporter(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeUnSolo(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeSolo(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeUnDeep(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeDeep(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeUnLiveFreeze(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeLiveFreeze(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeShotgun(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeGrenade(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeLaser(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeJetpack(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeEndlessJump(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeSetJumps(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeWeapons(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeUnShotgun(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeUnGrenade(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeUnLaser(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeUnJetpack(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeUnEndlessJump(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeUnWeapons(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeNinja(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeUnNinja(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeEndlessHook(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeUnEndlessHook(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeToggleInvincible(IConsole::IResult *pResult, void *pUserData);

	static void ConPracticeAddWeapon(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeRemoveWeapon(IConsole::IResult *pResult, void *pUserData);

	static void ConProtectedKill(IConsole::IResult *pResult, void *pUserData);
	static void ConModerate(IConsole::IResult *pResult, void *pUserData);

	static void ConList(IConsole::IResult *pResult, void *pUserData);
	static void ConSetDDRTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConUninvite(IConsole::IResult *pResult, void *pUserData);

	static void ConReloadCensorlist(IConsole::IResult *pResult, void *pUserData);

	CCharacter *GetPracticeCharacter(IConsole::IResult *pResult);

	CMutes m_Mutes;
	CMutes m_VoteMutes;
	// 最后一次切歌的时间戳
	CSongCooldowns m_SongCooldowns;
	void MuteWithMessage(const NETADDR *pAddr, int Seconds, const char *pReason, const char *pDisplayName);
	void VoteMuteWithMessage(const NETADDR *pAddr, int Seconds, const char *pReason, const char *pDisplayName);

	static void ConMute(IConsole::IResult *pResult, void *pUserData);
	static void ConMuteId(IConsole::IResult *pResult, void *pUserData);
	static void ConMuteIp(IConsole::IResult *pResult, void *pUserData);
	static void ConUnmute(IConsole::IResult *pResult, void *pUserData);
	static void ConUnmuteId(IConsole::IResult *pResult, void *pUserData);
	static void ConUnmuteIp(IConsole::IResult *pResult, void *pUserData);
	static void ConMutes(IConsole::IResult *pResult, void *pUserData);

	static void ConVoteMute(IConsole::IResult *pResult, void *pUserData);
	static void ConVoteMuteId(IConsole::IResult *pResult, void *pUserData);
	static void ConVoteMuteIp(IConsole::IResult *pResult, void *pUserData);
	static void ConVoteUnmute(IConsole::IResult *pResult, void *pUserData);
	static void ConVoteUnmuteId(IConsole::IResult *pResult, void *pUserData);
	static void ConVoteUnmuteIp(IConsole::IResult *pResult, void *pUserData);
	static void ConVoteMutes(IConsole::IResult *pResult, void *pUserData);

	void Whisper(int ClientId, char *pStr);
	void WhisperId(int ClientId, int VictimId, const char *pMessage);
	void Converse(int ClientId, char *pStr);
	bool IsVersionBanned(int Version);
	void UnlockTeam(int ClientId, int Team) const;
	void AttemptJoinTeam(int ClientId, int Team);

	enum
	{
		MAX_LOG_SECONDS = 600,
		MAX_LOGS = 512,
	};
	struct CLog
	{
		int64_t m_Timestamp;
		bool m_FromServer;
		char m_aDescription[128];
		int m_ClientVersion;
		char m_aClientName[MAX_NAME_LENGTH];
		char m_aClientAddrStr[NETADDR_MAXSTRSIZE];
	};
	CLog m_aLogs[MAX_LOGS];
	int m_LatestLog;

	void LogEvent(const char *Description, int ClientId);

public:
	CLayers *Layers() { return &m_Layers; }
	CScore *Score() { return m_pScore; }

	enum
	{
		VOTE_TYPE_UNKNOWN = 0,
		VOTE_TYPE_OPTION,
		VOTE_TYPE_KICK,
		VOTE_TYPE_SPECTATE,
	};
	int m_VoteVictim;

	inline bool IsOptionVote() const { return m_VoteType == VOTE_TYPE_OPTION; }
	inline bool IsKickVote() const { return m_VoteType == VOTE_TYPE_KICK; }
	inline bool IsSpecVote() const { return m_VoteType == VOTE_TYPE_SPECTATE; }

	bool IsRunningVote(int ClientId) const;
	bool IsRunningKickOrSpecVote(int ClientId) const;

	void SendRecord(int ClientId);
	void SendFinish(int ClientId, float Time, float PreviousBestTime);
	void OnSetAuthed(int ClientId, int Level) override;

	void ResetTuning();
};

static inline bool CheckClientId(int ClientId)
{
	return ClientId >= 0 && ClientId < MAX_CLIENTS;
}

#endif
