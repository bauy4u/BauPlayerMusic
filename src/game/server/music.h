#ifndef GAME_SERVER_MUSIC_H
#define GAME_SERVER_MUSIC_H

#include <base/lock.h>
#include <base/system.h>
#include <engine/shared/http.h>
#include <engine/shared/jobs.h>

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

class CGameContext;

struct SongInfo
{
	std::string title;
	std::string artist;
	std::string page_url;
	std::string requesterName;
	std::string requesterSource = "game";
	std::string requesterId;
	float duration = 0.0f;
	bool isPreloaded = false;
	bool isReady = false;
	int preloadFailures = 0;
	int64_t nextPreloadRetryTime = 0;
};

struct LyricLine
{
	int m_Tick = 0;
	std::string m_Text;
};

enum class EMusicEventType
{
	SEARCH_RESULT,
	PRELOAD_RESULT,
	UPLOAD_RESULT,
	QQ_RELAY_RESULT,
	QQ_POLL_RESULT,
	MUSIC_STATS_RESULT,
	IDENTITY_CODE_RESULT,
	SERVER_STATE_RESULT,
	TURTLE_SOUP_RESULT,
};

struct SMusicEvent
{
	EMusicEventType m_Type = EMusicEventType::SEARCH_RESULT;
	int m_ClientID = -1;
	std::vector<SongInfo> m_vSongs;
	SongInfo m_Song;
	int m_QueueIndex = -1;
	bool m_Success = false;
	float m_Duration = 0.0f;
	bool m_ForVoteMenu = false;
	std::string m_MapName;
	std::string m_PreparedMapPath;
	std::string m_MapSha256;
	std::string m_Error;
	std::vector<std::string> m_vMessages;
	bool m_Undercover = false;
	bool m_UndercoverActive = false;
	bool m_UndercoverTeamAudience = false;
	bool m_UndercoverClearRoom = false;
	std::string m_UndercoverRoom;
	std::string m_UndercoverStatus;
	std::string m_UndercoverPhase;
	std::string m_UndercoverSpeakerId;
	std::string m_UndercoverSpeakerName;
	int64_t m_UndercoverDeadline = 0;
};

struct CMusicQueueSnapshot
{
	std::deque<SongInfo> m_vSongs;
	int m_CurrentSongIndex = -1;
	int64_t m_CurrentSongStartTime = 0;
	float m_CurrentSongDuration = 0.0f;
	bool m_IsPlayingFromQueue = false;
	int m_NextPreloadIndex = 0;
};

class CMusicState
{
public:
	CMusicState() = default;
	CMusicState(const CMusicState &Other);

	CMusicState &operator=(const CMusicState &Other);

	void QueueEvent(SMusicEvent Event) EXCLUDES(m_MusicEventMutex);
	std::vector<SMusicEvent> DrainEvents() EXCLUDES(m_MusicEventMutex);
	void AddQueuedSong(const SongInfo &Song);
	// QueueIndex is zero-based. The song is inserted immediately after it.
	// Returns the inserted zero-based index, or -1 when QueueIndex is invalid.
	int InsertQueuedSongAfter(int QueueIndex, const SongInfo &Song);
	// Removes a zero-based queue entry. The currently playing entry is protected
	// and must be advanced with queue_skip instead.
	bool RemoveQueuedSong(int QueueIndex, SongInfo *pRemovedSong = nullptr);
	void ClearQueue();
	CMusicQueueSnapshot ExportQueueSnapshot() const;
	void ImportQueueSnapshot(CMusicQueueSnapshot Snapshot);
	bool QueueEmpty() const;
	int QueueSize() const;
	SongInfo *GetQueuedSong(int Index);
	const SongInfo *GetQueuedSong(int Index) const;
	bool IsQueuedSong(int Index, const SongInfo &Song) const;
	bool IsPlayingFromQueue() const;
	bool HasCurrentQueueSong() const;
	bool WaitingForFirstQueueSong() const;
	bool IsCurrentQueueIndex(int Index) const;
	int CurrentSongIndex() const;
	int NextQueueIndex() const;
	int64_t CurrentSongStartTime() const;
	float CurrentSongDuration() const;
	bool CurrentSongDurationInRange(float MaxDuration) const;
	bool SongSearchInGlobalCooldown(int64_t Now, int CooldownSeconds, int *pSecondsLeft) const;
	void MarkSongChanged(int64_t Time);
	bool UpdateQueuedSongDuration(int Index, float Duration);
	bool SetQueuedSongPreloaded(int Index, bool IsPreloaded);
	void StoreSearchResults(int ClientID, std::vector<SongInfo> vSongs);
	int SearchResultCount(int ClientID) const;
	bool GetSearchResult(int ClientID, int Index, SongInfo *pSong) const;
	void ClearSearchResults(int ClientID);
	void StoreVoteMenuSearchResults(int ClientID, std::vector<SongInfo> vSongs);
	int VoteMenuSearchResultCount(int ClientID) const;
	bool GetVoteMenuSearchResult(int ClientID, int Index, SongInfo *pSong) const;
	void ClearVoteMenuSearchResults(int ClientID);
	int AddPendingSongVote(const SongInfo &Song);
	bool ConsumePendingSongVote(int VoteId, SongInfo *pSong);
	void ResetPlayback();
	void StopPlayback();
	void BeginQueueSong(int Index, const SongInfo &Song, int64_t StartTime);
	bool AdvanceToNextSong(SongInfo *pSong, int64_t StartTime);
	bool ConsumeCurrentSong();
	bool NormalizePlaybackState();
	bool IsPreloading(const std::string &SongId) const;
	void MarkPreloading(const std::string &SongId);
	void FinishPreloading(const std::string &SongId);
	void ClearPreloading();
	bool ShouldLogPreloadSkip(int64_t Now, int IntervalSeconds);
	bool NeedsDeferredStartTime() const;
	void ApplyDeferredStartTime(int64_t StartTime);
	void ResetLyrics();
	void AddLyricLine(int Tick, const std::string &Text);
	bool HasLyrics() const;
	bool LyricsActive() const;
	bool BeginLyricsDisplay(int ServerTick, int TickSpeed, int64_t Now);
	void StopLyricsDisplay();
	bool PopDueLyric(int ServerTick, std::string *pText);
	bool CurrentLyricLine(int ServerTick, std::string *pText) const;
	const std::string &CurrentSongId() const;
	int NextLyricIndex() const;
	void SetNextLyricIndex(size_t Index);
	void ResetRuntime();

private:
	std::deque<SongInfo> m_PlaylistQueue;
	std::set<std::string> m_PreloadingSongIds;
	std::map<int, std::vector<SongInfo>> m_PlayerSongResults;
	std::map<int, std::vector<SongInfo>> m_PlayerVoteMenuSongResults;
	std::map<int, SongInfo> m_PendingSongVotes;
	int m_NextPendingSongVoteId = 1;

	int m_CurrentSongIndex = -1;
	int64_t m_CurrentSongStartTime = 0;
	float m_CurrentSongDuration = 0.0f;
	bool m_IsPlayingFromQueue = false;
	int m_NextPreloadIndex = 0;

	std::vector<LyricLine> m_CurrentLyrics;
	int m_LyricStartTick = 0;
	size_t m_NextLyricIndex = 0;
	bool m_LyricsActive = false;
	std::string m_CurrentSongId;
	int64_t m_LastSongChangeTime = 0;
	int64_t m_LastPreloadSkipLogTime = 0;

	std::mutex m_MusicEventMutex;
	std::vector<SMusicEvent> m_vMusicEvents;

	void CopyFrom(const CMusicState &Other);
};

class CSongSearchJob : public IJob
{
	CGameContext *m_pGameContext;
	int m_ClientID;
	std::shared_ptr<CHttpRequest> m_pRequest;
	bool m_ForVoteMenu;

public:
	CSongSearchJob(CGameContext *pGameContext, int ClientID, std::shared_ptr<CHttpRequest> pRequest, bool ForVoteMenu) :
		m_pGameContext(pGameContext),
		m_ClientID(ClientID),
		m_pRequest(std::move(pRequest)),
		m_ForVoteMenu(ForVoteMenu)
	{
	}

	void Run() override;
};

class CMapUploadJob : public IJob
{
	CGameContext *m_pGameContext;
	std::shared_ptr<CHttpRequest> m_pRequest;

public:
	CMapUploadJob(CGameContext *pGameContext, std::shared_ptr<CHttpRequest> pRequest);
	void Run() override;
};

class CSongPreloadJob : public IJob
{
	CGameContext *m_pGameContext;
	SongInfo m_Song;
	int m_QueueIndex;
	IHttp *m_pHttp;
	std::string m_DownloadEndpoint;
	std::string m_MapName;
	std::string m_TargetMapPath;

public:
	CSongPreloadJob(CGameContext *pGameContext, const SongInfo &Song, int QueueIndex, IHttp *pHttp, std::string DownloadEndpoint, std::string MapName, std::string TargetMapPath) :
		m_pGameContext(pGameContext),
		m_Song(Song),
		m_QueueIndex(QueueIndex),
		m_pHttp(pHttp),
		m_DownloadEndpoint(std::move(DownloadEndpoint)),
		m_MapName(std::move(MapName)),
		m_TargetMapPath(std::move(TargetMapPath))
	{
	}

	void Run() override;
};

class CQQRelayJob : public IJob
{
	CGameContext *m_pGameContext;
	int m_ClientID;
	std::shared_ptr<CHttpRequest> m_pRequest;

public:
	CQQRelayJob(CGameContext *pGameContext, int ClientID, std::shared_ptr<CHttpRequest> pRequest) :
		m_pGameContext(pGameContext),
		m_ClientID(ClientID),
		m_pRequest(std::move(pRequest))
	{
	}

	void Run() override;
};

class CQQPollJob : public IJob
{
	CGameContext *m_pGameContext;
	std::shared_ptr<CHttpRequest> m_pRequest;

public:
	CQQPollJob(CGameContext *pGameContext, std::shared_ptr<CHttpRequest> pRequest) :
		m_pGameContext(pGameContext),
		m_pRequest(std::move(pRequest))
	{
	}

	void Run() override;
};

class CMusicHistoryJob : public IJob
{
	std::shared_ptr<CHttpRequest> m_pRequest;

public:
	explicit CMusicHistoryJob(std::shared_ptr<CHttpRequest> pRequest) :
		m_pRequest(std::move(pRequest))
	{
	}

	void Run() override;
};

class CMusicStatsJob : public IJob
{
	CGameContext *m_pGameContext;
	int m_ClientID;
	std::shared_ptr<CHttpRequest> m_pRequest;

public:
	CMusicStatsJob(CGameContext *pGameContext, int ClientID, std::shared_ptr<CHttpRequest> pRequest) :
		m_pGameContext(pGameContext),
		m_ClientID(ClientID),
		m_pRequest(std::move(pRequest))
	{
	}

	void Run() override;
};

class CIdentityCodeJob : public IJob
{
	CGameContext *m_pGameContext;
	int m_ClientID;
	std::shared_ptr<CHttpRequest> m_pRequest;

public:
	CIdentityCodeJob(CGameContext *pGameContext, int ClientID, std::shared_ptr<CHttpRequest> pRequest) :
		m_pGameContext(pGameContext),
		m_ClientID(ClientID),
		m_pRequest(std::move(pRequest))
	{
	}

	void Run() override;
};

class CServerStateJob : public IJob
{
	CGameContext *m_pGameContext;
	std::shared_ptr<CHttpRequest> m_pRequest;

public:
	CServerStateJob(CGameContext *pGameContext, std::shared_ptr<CHttpRequest> pRequest) :
		m_pGameContext(pGameContext),
		m_pRequest(std::move(pRequest))
	{
	}

	void Run() override;
};

class CTurtleSoupJob : public IJob
{
	CGameContext *m_pGameContext;
	int m_ClientID;
	std::shared_ptr<CHttpRequest> m_pRequest;

public:
	CTurtleSoupJob(CGameContext *pGameContext, int ClientID, std::shared_ptr<CHttpRequest> pRequest) :
		m_pGameContext(pGameContext),
		m_ClientID(ClientID),
		m_pRequest(std::move(pRequest))
	{
	}

	void Run() override;
};

class CSongCooldown
{
public:
	int64_t m_Expire = 0;
	bool m_Initialized = false;

	int SecondsLeft() const;
};

class CSongCooldowns
{
public:
	CSongCooldowns();

	bool SetCooldown(const NETADDR *pAddr, int Seconds);
	bool IsCooldown(const NETADDR *pAddr) const;
	int GetSecondsLeft(const NETADDR *pAddr) const;
	void CleanupExpired();

private:
	std::map<NETADDR, CSongCooldown> m_Cooldowns;
};

#endif
