#include "music.h"

#include <mutex>
#include <utility>

CMusicState::CMusicState(const CMusicState &Other)
{
	CopyFrom(Other);
}

CMusicState &CMusicState::operator=(const CMusicState &Other)
{
	if(this != &Other)
		CopyFrom(Other);
	return *this;
}

void CMusicState::QueueEvent(SMusicEvent Event)
{
	std::lock_guard<std::mutex> Lock(m_MusicEventMutex);
	m_vMusicEvents.push_back(std::move(Event));
}

std::vector<SMusicEvent> CMusicState::DrainEvents()
{
	std::vector<SMusicEvent> vEvents;
	std::lock_guard<std::mutex> Lock(m_MusicEventMutex);
	vEvents.swap(m_vMusicEvents);
	return vEvents;
}

void CMusicState::AddQueuedSong(const SongInfo &Song)
{
	m_PlaylistQueue.push_back(Song);
}

int CMusicState::InsertQueuedSongAfter(int QueueIndex, const SongInfo &Song)
{
	if(QueueIndex < 0 || QueueIndex >= QueueSize())
		return -1;

	const int InsertIndex = QueueIndex + 1;
	m_PlaylistQueue.insert(m_PlaylistQueue.begin() + InsertIndex, Song);
	if(m_CurrentSongIndex >= InsertIndex)
		m_CurrentSongIndex++;

	// Results from already-running preload jobs still carry their old queue
	// indices. Invalidate every shifted song so stale jobs cannot leave one
	// permanently marked as "preloading" at its new position.
	ClearPreloading();
	for(int Index = InsertIndex; Index < QueueSize(); Index++)
	{
		SongInfo &ShiftedSong = m_PlaylistQueue[Index];
		ShiftedSong.duration = 0.0f;
		ShiftedSong.isPreloaded = false;
		ShiftedSong.isReady = false;
		ShiftedSong.preloadFailures = 0;
		ShiftedSong.nextPreloadRetryTime = 0;
	}
	return InsertIndex;
}

bool CMusicState::RemoveQueuedSong(int QueueIndex, SongInfo *pRemovedSong)
{
	if(QueueIndex < 0 || QueueIndex >= QueueSize() || IsCurrentQueueIndex(QueueIndex))
		return false;
	if(pRemovedSong)
		*pRemovedSong = m_PlaylistQueue[QueueIndex];
	m_PlaylistQueue.erase(m_PlaylistQueue.begin() + QueueIndex);
	if(m_CurrentSongIndex > QueueIndex)
		m_CurrentSongIndex--;

	// Queue indices carried by in-flight preload jobs have shifted. Reset their
	// runtime status so only results matching the new slot can be accepted.
	ClearPreloading();
	for(int Index = QueueIndex; Index < QueueSize(); Index++)
	{
		SongInfo &ShiftedSong = m_PlaylistQueue[Index];
		ShiftedSong.duration = 0.0f;
		ShiftedSong.isPreloaded = false;
		ShiftedSong.isReady = false;
		ShiftedSong.preloadFailures = 0;
		ShiftedSong.nextPreloadRetryTime = 0;
	}
	NormalizePlaybackState();
	return true;
}

void CMusicState::ClearQueue()
{
	m_PlaylistQueue.clear();
	ClearPreloading();
	StopPlayback();
}

CMusicQueueSnapshot CMusicState::ExportQueueSnapshot() const
{
	CMusicQueueSnapshot Snapshot;
	Snapshot.m_vSongs = m_PlaylistQueue;
	Snapshot.m_CurrentSongIndex = m_CurrentSongIndex;
	Snapshot.m_CurrentSongStartTime = m_CurrentSongStartTime;
	Snapshot.m_CurrentSongDuration = m_CurrentSongDuration;
	Snapshot.m_IsPlayingFromQueue = m_IsPlayingFromQueue;
	Snapshot.m_NextPreloadIndex = m_NextPreloadIndex;
	return Snapshot;
}

void CMusicState::ImportQueueSnapshot(CMusicQueueSnapshot Snapshot)
{
	m_PlaylistQueue = std::move(Snapshot.m_vSongs);
	m_CurrentSongIndex = Snapshot.m_CurrentSongIndex;
	m_CurrentSongStartTime = Snapshot.m_CurrentSongStartTime;
	m_CurrentSongDuration = Snapshot.m_CurrentSongDuration;
	m_IsPlayingFromQueue = Snapshot.m_IsPlayingFromQueue;
	m_NextPreloadIndex = Snapshot.m_NextPreloadIndex;
	ClearPreloading();
	NormalizePlaybackState();
}

bool CMusicState::QueueEmpty() const
{
	return m_PlaylistQueue.empty();
}

int CMusicState::QueueSize() const
{
	return (int)m_PlaylistQueue.size();
}

SongInfo *CMusicState::GetQueuedSong(int Index)
{
	if(Index < 0 || Index >= QueueSize())
		return nullptr;
	return &m_PlaylistQueue[Index];
}

const SongInfo *CMusicState::GetQueuedSong(int Index) const
{
	if(Index < 0 || Index >= QueueSize())
		return nullptr;
	return &m_PlaylistQueue[Index];
}

bool CMusicState::IsQueuedSong(int Index, const SongInfo &Song) const
{
	const SongInfo *pQueuedSong = GetQueuedSong(Index);
	return pQueuedSong && pQueuedSong->page_url == Song.page_url;
}

bool CMusicState::IsPlayingFromQueue() const
{
	return m_IsPlayingFromQueue;
}

bool CMusicState::HasCurrentQueueSong() const
{
	return m_IsPlayingFromQueue && m_CurrentSongIndex >= 0;
}

bool CMusicState::WaitingForFirstQueueSong() const
{
	return m_CurrentSongIndex == -1;
}

bool CMusicState::IsCurrentQueueIndex(int Index) const
{
	return m_IsPlayingFromQueue && m_CurrentSongIndex == Index;
}

int CMusicState::CurrentSongIndex() const
{
	return m_CurrentSongIndex;
}

int CMusicState::NextQueueIndex() const
{
	return m_CurrentSongIndex + 1;
}

int64_t CMusicState::CurrentSongStartTime() const
{
	return m_CurrentSongStartTime;
}

float CMusicState::CurrentSongDuration() const
{
	return m_CurrentSongDuration;
}

bool CMusicState::CurrentSongDurationInRange(float MaxDuration) const
{
	return m_CurrentSongDuration >= 0.0f && m_CurrentSongDuration <= MaxDuration;
}

bool CMusicState::SongSearchInGlobalCooldown(int64_t Now, int CooldownSeconds, int *pSecondsLeft) const
{
	if(m_LastSongChangeTime <= 0 || Now - m_LastSongChangeTime >= CooldownSeconds)
		return false;

	if(pSecondsLeft)
		*pSecondsLeft = CooldownSeconds - (int)(Now - m_LastSongChangeTime);
	return true;
}

void CMusicState::MarkSongChanged(int64_t Time)
{
	m_LastSongChangeTime = Time;
}

bool CMusicState::UpdateQueuedSongDuration(int Index, float Duration)
{
	SongInfo *pSong = GetQueuedSong(Index);
	if(!pSong)
		return false;

	pSong->duration = Duration;
	pSong->isReady = true;
	pSong->preloadFailures = 0;
	pSong->nextPreloadRetryTime = 0;
	return true;
}

bool CMusicState::SetQueuedSongPreloaded(int Index, bool IsPreloaded)
{
	SongInfo *pSong = GetQueuedSong(Index);
	if(!pSong)
		return false;

	pSong->isPreloaded = IsPreloaded;
	if(!IsPreloaded)
		pSong->isReady = false;
	return true;
}

void CMusicState::StoreSearchResults(int ClientID, std::vector<SongInfo> vSongs)
{
	m_PlayerSongResults[ClientID] = std::move(vSongs);
}

int CMusicState::SearchResultCount(int ClientID) const
{
	auto it = m_PlayerSongResults.find(ClientID);
	if(it == m_PlayerSongResults.end())
		return 0;
	return (int)it->second.size();
}

bool CMusicState::GetSearchResult(int ClientID, int Index, SongInfo *pSong) const
{
	auto it = m_PlayerSongResults.find(ClientID);
	if(it == m_PlayerSongResults.end() || Index < 0 || Index >= (int)it->second.size())
		return false;

	if(pSong)
		*pSong = it->second[Index];
	return true;
}

void CMusicState::ClearSearchResults(int ClientID)
{
	m_PlayerSongResults.erase(ClientID);
}

void CMusicState::StoreVoteMenuSearchResults(int ClientID, std::vector<SongInfo> vSongs)
{
	m_PlayerVoteMenuSongResults[ClientID] = std::move(vSongs);
}

int CMusicState::VoteMenuSearchResultCount(int ClientID) const
{
	auto it = m_PlayerVoteMenuSongResults.find(ClientID);
	if(it == m_PlayerVoteMenuSongResults.end())
		return 0;
	return (int)it->second.size();
}

bool CMusicState::GetVoteMenuSearchResult(int ClientID, int Index, SongInfo *pSong) const
{
	auto it = m_PlayerVoteMenuSongResults.find(ClientID);
	if(it == m_PlayerVoteMenuSongResults.end() || Index < 0 || Index >= (int)it->second.size())
		return false;

	if(pSong)
		*pSong = it->second[Index];
	return true;
}

void CMusicState::ClearVoteMenuSearchResults(int ClientID)
{
	m_PlayerVoteMenuSongResults.erase(ClientID);
}

int CMusicState::AddPendingSongVote(const SongInfo &Song)
{
	const int VoteId = m_NextPendingSongVoteId++;
	m_PendingSongVotes[VoteId] = Song;
	return VoteId;
}

bool CMusicState::ConsumePendingSongVote(int VoteId, SongInfo *pSong)
{
	auto it = m_PendingSongVotes.find(VoteId);
	if(it == m_PendingSongVotes.end())
		return false;

	if(pSong)
		*pSong = it->second;
	m_PendingSongVotes.erase(it);
	return true;
}

void CMusicState::ResetPlayback()
{
	m_CurrentSongIndex = -1;
	m_CurrentSongStartTime = 0;
	m_CurrentSongDuration = 0.0f;
	m_IsPlayingFromQueue = false;
	m_LyricsActive = false;
	m_CurrentSongId.clear();
}

void CMusicState::StopPlayback()
{
	ResetPlayback();
}

void CMusicState::BeginQueueSong(int Index, const SongInfo &Song, int64_t StartTime)
{
	m_CurrentSongIndex = Index;
	m_CurrentSongStartTime = StartTime;
	m_CurrentSongDuration = Song.duration;
	m_IsPlayingFromQueue = true;
	m_CurrentSongId = Song.page_url;
}

bool CMusicState::AdvanceToNextSong(SongInfo *pSong, int64_t StartTime)
{
	if(m_PlaylistQueue.size() <= 1)
		return false;

	SongInfo NextSong = m_PlaylistQueue[1];
	if(!NextSong.isReady)
		return false;

	m_PlaylistQueue.pop_front();
	ClearPreloading();
	BeginQueueSong(0, NextSong, StartTime);

	if(pSong)
		*pSong = NextSong;
	return true;
}

bool CMusicState::ConsumeCurrentSong()
{
	if(m_PlaylistQueue.empty())
	{
		StopPlayback();
		return false;
	}

	if(m_CurrentSongIndex >= 0 && m_CurrentSongIndex < QueueSize())
		m_PlaylistQueue.erase(m_PlaylistQueue.begin() + m_CurrentSongIndex);
	else if(!m_IsPlayingFromQueue)
		return false;

	ClearPreloading();
	StopPlayback();
	return true;
}

bool CMusicState::NormalizePlaybackState()
{
	bool Changed = false;
	if(m_PlaylistQueue.empty())
	{
		if(m_IsPlayingFromQueue || m_CurrentSongIndex != -1 || m_CurrentSongStartTime != 0 || m_CurrentSongDuration != 0.0f || !m_CurrentSongId.empty() || !m_PreloadingSongIds.empty())
		{
			ClearPreloading();
			StopPlayback();
			Changed = true;
		}
		return Changed;
	}

	if(m_CurrentSongIndex >= QueueSize())
	{
		StopPlayback();
		Changed = true;
	}

	if(m_IsPlayingFromQueue && m_CurrentSongIndex > 0 && m_CurrentSongIndex < QueueSize())
	{
		m_PlaylistQueue.erase(m_PlaylistQueue.begin(), m_PlaylistQueue.begin() + m_CurrentSongIndex);
		m_CurrentSongIndex = 0;
		Changed = true;
	}

	if(m_CurrentSongIndex < -1)
	{
		ResetPlayback();
		Changed = true;
	}

	if(!m_IsPlayingFromQueue && m_CurrentSongIndex != -1)
	{
		ResetPlayback();
		Changed = true;
	}

	if(m_CurrentSongDuration < 0.0f)
	{
		StopPlayback();
		Changed = true;
	}

	return Changed;
}

bool CMusicState::IsPreloading(const std::string &SongId) const
{
	return m_PreloadingSongIds.count(SongId) > 0;
}

void CMusicState::MarkPreloading(const std::string &SongId)
{
	m_PreloadingSongIds.insert(SongId);
}

void CMusicState::FinishPreloading(const std::string &SongId)
{
	m_PreloadingSongIds.erase(SongId);
}

void CMusicState::ClearPreloading()
{
	m_PreloadingSongIds.clear();
}

bool CMusicState::ShouldLogPreloadSkip(int64_t Now, int IntervalSeconds)
{
	if(Now - m_LastPreloadSkipLogTime <= IntervalSeconds)
		return false;

	m_LastPreloadSkipLogTime = Now;
	return true;
}

bool CMusicState::NeedsDeferredStartTime() const
{
	return m_IsPlayingFromQueue && m_CurrentSongStartTime == 0;
}

void CMusicState::ApplyDeferredStartTime(int64_t StartTime)
{
	if(NeedsDeferredStartTime())
		m_CurrentSongStartTime = StartTime;
}

void CMusicState::ResetLyrics()
{
	m_CurrentLyrics.clear();
	m_NextLyricIndex = 0;
	m_LyricsActive = false;
}

void CMusicState::AddLyricLine(int Tick, const std::string &Text, const std::vector<int> &vCharacterStartOffsets)
{
	LyricLine Line;
	Line.m_Tick = Tick;
	Line.m_Text = Text;
	Line.m_vCharacterStartOffsets = vCharacterStartOffsets;
	m_CurrentLyrics.push_back(Line);
}

bool CMusicState::HasLyrics() const
{
	return !m_CurrentLyrics.empty();
}

bool CMusicState::LyricsActive() const
{
	return m_LyricsActive;
}

bool CMusicState::BeginLyricsDisplay(int ServerTick, int TickSpeed, int64_t Now)
{
	if(!HasLyrics())
		return false;

	m_LyricsActive = true;
	const int64_t ElapsedSeconds = m_CurrentSongStartTime > 0 ? Now - m_CurrentSongStartTime : 0;
	const int ElapsedTicks = (int)(ElapsedSeconds * TickSpeed);
	m_LyricStartTick = ServerTick - ElapsedTicks;
	m_NextLyricIndex = 0;

	const int CurrentTick = ServerTick - m_LyricStartTick;
	while(m_NextLyricIndex < m_CurrentLyrics.size() && CurrentTick >= m_CurrentLyrics[m_NextLyricIndex].m_Tick)
		m_NextLyricIndex++;
	return true;
}

void CMusicState::StopLyricsDisplay()
{
	m_LyricsActive = false;
}

bool CMusicState::PopDueLyric(int ServerTick, std::string *pText, int *pDurationTicks, int FallbackDurationTicks, std::vector<int> *pCharacterStartOffsets)
{
	if(!m_LyricsActive || m_NextLyricIndex >= m_CurrentLyrics.size())
		return false;

	const int CurrentTick = ServerTick - m_LyricStartTick;
	if(CurrentTick < m_CurrentLyrics[m_NextLyricIndex].m_Tick)
		return false;

	const size_t LyricIndex = m_NextLyricIndex;
	if(pText)
		*pText = m_CurrentLyrics[LyricIndex].m_Text;
	if(pCharacterStartOffsets)
		*pCharacterStartOffsets = m_CurrentLyrics[LyricIndex].m_vCharacterStartOffsets;
	if(pDurationTicks)
	{
		const int CurrentLyricTick = m_CurrentLyrics[LyricIndex].m_Tick;
		const int NextLyricTick = LyricIndex + 1 < m_CurrentLyrics.size() ? m_CurrentLyrics[LyricIndex + 1].m_Tick : CurrentLyricTick + FallbackDurationTicks;
		const int DurationTicks = NextLyricTick - CurrentLyricTick;
		*pDurationTicks = DurationTicks > 1 ? DurationTicks : 1;
	}
	m_NextLyricIndex++;
	return true;
}

bool CMusicState::CurrentLyricLine(int ServerTick, std::string *pText) const
{
	if(!m_LyricsActive || m_CurrentLyrics.empty())
		return false;

	const int CurrentTick = ServerTick - m_LyricStartTick;
	const LyricLine *pCurrent = nullptr;
	for(const LyricLine &Line : m_CurrentLyrics)
	{
		if(CurrentTick < Line.m_Tick)
			break;
		if(!Line.m_Text.empty())
			pCurrent = &Line;
	}
	if(!pCurrent)
		return false;
	if(pText)
		*pText = pCurrent->m_Text;
	return true;
}

const std::string &CMusicState::CurrentSongId() const
{
	return m_CurrentSongId;
}

int CMusicState::NextLyricIndex() const
{
	return (int)m_NextLyricIndex;
}

void CMusicState::SetNextLyricIndex(size_t Index)
{
	m_NextLyricIndex = Index < m_CurrentLyrics.size() ? Index : m_CurrentLyrics.size();
}

void CMusicState::ResetRuntime()
{
	m_PlaylistQueue.clear();
	ClearPreloading();
	m_PlayerSongResults.clear();
	m_PlayerVoteMenuSongResults.clear();
	m_PendingSongVotes.clear();
	m_NextPendingSongVoteId = 1;
	m_CurrentSongIndex = -1;
	m_CurrentSongStartTime = 0;
	m_CurrentSongDuration = 0.0f;
	m_IsPlayingFromQueue = false;
	m_NextPreloadIndex = 0;
	m_CurrentLyrics.clear();
	m_LyricStartTick = 0;
	m_NextLyricIndex = 0;
	m_LyricsActive = false;
	m_CurrentSongId.clear();
	m_LastSongChangeTime = 0;
	m_LastPreloadSkipLogTime = 0;
	m_vMusicEvents.clear();
}

void CMusicState::CopyFrom(const CMusicState &Other)
{
	m_PlaylistQueue = Other.m_PlaylistQueue;
	m_PreloadingSongIds = Other.m_PreloadingSongIds;
	m_PlayerSongResults = Other.m_PlayerSongResults;
	m_PlayerVoteMenuSongResults = Other.m_PlayerVoteMenuSongResults;
	m_PendingSongVotes = Other.m_PendingSongVotes;
	m_NextPendingSongVoteId = Other.m_NextPendingSongVoteId;
	m_CurrentSongIndex = Other.m_CurrentSongIndex;
	m_CurrentSongStartTime = Other.m_CurrentSongStartTime;
	m_CurrentSongDuration = Other.m_CurrentSongDuration;
	m_IsPlayingFromQueue = Other.m_IsPlayingFromQueue;
	m_NextPreloadIndex = Other.m_NextPreloadIndex;
	m_CurrentLyrics = Other.m_CurrentLyrics;
	m_LyricStartTick = Other.m_LyricStartTick;
	m_NextLyricIndex = Other.m_NextLyricIndex;
	m_LyricsActive = Other.m_LyricsActive;
	m_CurrentSongId = Other.m_CurrentSongId;
	m_LastSongChangeTime = Other.m_LastSongChangeTime;
	m_LastPreloadSkipLogTime = Other.m_LastPreloadSkipLogTime;
	m_vMusicEvents = Other.m_vMusicEvents;
}
