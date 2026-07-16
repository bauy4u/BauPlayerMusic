/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
#ifndef GAME_SERVER_RAINBOWNAME_H
#define GAME_SERVER_RAINBOWNAME_H

#include <engine/shared/protocol.h>

class CGameContext;

// Keeps the actual DDRace team untouched and only supplies a temporary team
// value for SV_TEAMSSTATE, which is what clients use to color player names.
class CRainbowName
{
public:
	struct SState
	{
		bool m_aEnabled[MAX_CLIENTS] = {};
		int m_Color = 1;
	};

	void Init(CGameContext *pGameContext);
	SState GetState() const;
	void SetState(const SState &State);

	bool Toggle(int ClientId);
	bool IsEnabled(int ClientId) const;
	void Clear(int ClientId);
	void RequestRefresh();

	// Rebuilds the per-receiver display team tables and sends only changed ones.
	// The color advances every two server ticks, like F-DDrace's rainbowname.
	void Tick();
	void OnChatMessage(int ClientId);

	// Returns -1 when the target should use its real DDRace team.
	int GetColor(int ReceiverClientId, int TargetClientId) const;

private:
	struct SInfo
	{
		bool m_UpdateTeams = false;
		bool m_ResetChatColor = false;
		bool m_ScoreboardOpen = false;
		int m_aTeam[MAX_CLIENTS] = {};
	};

	void Update(int ReceiverClientId);
	void ClearDisplayTeams(int ReceiverClientId);

	CGameContext *m_pGameContext = nullptr;
	bool m_aEnabled[MAX_CLIENTS] = {};
	SInfo m_aInfo[MAX_CLIENTS];
	int m_Color = 1;
	bool m_ForceRefresh = false;
};

#endif
