/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
#include "rainbowname.h"

#include <base/system.h>

#include <game/teamscore.h>

#include "entities/character.h"
#include "gamemodes/DDRace.h"
#include "gamecontext.h"
#include "player.h"

// The client derives the DDRace team hue using the golden angle. F-Ddrace's
// s_aLegacyTeams is this same hue-sorted permutation. Keeping the logical
// color counter in this order makes the visible hue move smoothly instead of
// jumping around the color wheel (1, 2, 3... would jump by 137.5 degrees).
static const int s_aRainbowDisplayTeams[TEAM_SUPER] = {
	0, 1, 56, 22, 43, 9, 9, 30, 51, 17, 38, 4, 59, 25, 46, 12,
	33, 54, 20, 41, 7, 62, 28, 49, 15, 36, 2, 57, 23, 44, 10, 31,
	52, 18, 39, 5, 60, 26, 47, 13, 34, 55, 21, 42, 8, 63, 29, 50,
	16, 37, 3, 58, 24, 45, 11, 32, 53, 19, 40, 6, 61, 27, 48, 14,
};

static int RainbowDisplayTeam(int Color)
{
	return Color > 0 && Color < TEAM_SUPER ? s_aRainbowDisplayTeams[Color] : TEAM_FLOCK;
}

void CRainbowName::Init(CGameContext *pGameContext)
{
	m_pGameContext = pGameContext;
	m_Color = 0;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		m_aInfo[i].m_UpdateTeams = false;
		m_aInfo[i].m_ResetChatColor = false;
		m_aInfo[i].m_ScoreboardOpen = false;
		mem_zero(m_aInfo[i].m_aTeam, sizeof(m_aInfo[i].m_aTeam));
	}
}

CRainbowName::SState CRainbowName::GetState() const
{
	SState State;
	mem_copy(State.m_aEnabled, m_aEnabled, sizeof(m_aEnabled));
	State.m_Color = m_Color;
	return State;
}

void CRainbowName::SetState(const SState &State)
{
	mem_copy(m_aEnabled, State.m_aEnabled, sizeof(m_aEnabled));
	m_Color = State.m_Color;
	RequestRefresh();
}

bool CRainbowName::Toggle(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	m_aEnabled[ClientId] = !m_aEnabled[ClientId];
	RequestRefresh();
	return m_aEnabled[ClientId];
}

bool CRainbowName::IsEnabled(int ClientId) const
{
	return ClientId >= 0 && ClientId < MAX_CLIENTS && m_aEnabled[ClientId];
}

void CRainbowName::Clear(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !m_aEnabled[ClientId])
		return;
	m_aEnabled[ClientId] = false;
	RequestRefresh();
}

void CRainbowName::RequestRefresh()
{
	m_ForceRefresh = true;
}

void CRainbowName::OnChatMessage(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !m_pGameContext || !m_pGameContext->m_apPlayers[ClientId])
		return;
	if(m_aEnabled[ClientId] || m_aInfo[ClientId].m_UpdateTeams)
		m_aInfo[ClientId].m_ResetChatColor = true;
}

void CRainbowName::Tick()
{
	if(!m_pGameContext)
		return;

	CGameControllerDDRace *pController = static_cast<CGameControllerDDRace *>(m_pGameContext->m_pController);
	if(!pController)
		return;

	// The client opens its scoreboard immediately while the server sees the
	// button on the next input tick. Clear the fake teams at that first possible
	// tick instead of waiting for the next two-tick color update.
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		CPlayer *pPlayer = m_pGameContext->m_apPlayers[ClientId];
		if(!pPlayer || !m_pGameContext->Server()->ClientIngame(ClientId))
		{
			m_aInfo[ClientId].m_ScoreboardOpen = false;
			continue;
		}

		const bool ScoreboardOpen = (pPlayer->m_PlayerFlags & PLAYERFLAG_SCOREBOARD) != 0;
		if(ScoreboardOpen && !m_aInfo[ClientId].m_ScoreboardOpen)
		{
			ClearDisplayTeams(ClientId);
			pController->Teams().SendTeamsState(ClientId);
		}
		m_aInfo[ClientId].m_ScoreboardOpen = ScoreboardOpen;
	}

	if(!m_ForceRefresh && m_pGameContext->Server()->Tick() % 2 != 0)
		return;

	const bool AdvanceColor = !m_ForceRefresh;
	m_ForceRefresh = false;
	if(AdvanceColor || m_Color == 0)
		// F-Ddrace uses VANILLA_MAX_CLIENTS == 64. In this fork it is 16,
		// so use the actual DDRace display-team range 1..63 instead.
		m_Color = m_Color % (TEAM_SUPER - 1) + 1;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(!m_pGameContext->m_apPlayers[ClientId] || !m_pGameContext->Server()->ClientIngame(ClientId))
			continue;

		const bool UpdatedLastRun = m_aInfo[ClientId].m_UpdateTeams;
		m_aInfo[ClientId].m_UpdateTeams = false;
		Update(ClientId);

		// Send once more after a color disappears, otherwise the client's last
		// fake team would remain in its team table.
		if(m_aInfo[ClientId].m_UpdateTeams || UpdatedLastRun)
			pController->Teams().SendTeamsState(ClientId);
	}

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		m_aInfo[ClientId].m_ResetChatColor = false;
}

int CRainbowName::GetColor(int ReceiverClientId, int TargetClientId) const
{
	if(ReceiverClientId < 0 || ReceiverClientId >= MAX_CLIENTS || TargetClientId < 0 || TargetClientId >= MAX_CLIENTS)
		return -1;
	return m_aInfo[ReceiverClientId].m_aTeam[TargetClientId];
}

void CRainbowName::Update(int ReceiverClientId)
{
	CPlayer *pReceiver = m_pGameContext->m_apPlayers[ReceiverClientId];
	if(!pReceiver)
		return;

	SInfo &Info = m_aInfo[ReceiverClientId];
	const int OwnMapId = ReceiverClientId;
	Info.m_aTeam[OwnMapId] = -1;

	CGameControllerDDRace *pController = static_cast<CGameControllerDDRace *>(m_pGameContext->m_pController);
	CTeamsCore *pCore = &pController->Teams().m_Core;
	const bool IsPaused = pReceiver->GetTeam() == TEAM_SPECTATORS || pReceiver->IsPaused();

	for(int TargetClientId = 0; TargetClientId < MAX_CLIENTS; ++TargetClientId)
	{
		if(TargetClientId == OwnMapId)
			continue;

		Info.m_aTeam[TargetClientId] = -1;
		// Do not mutate the team table while the receiver sees the scoreboard:
		// it would cause the client-side team sorting to flicker every two ticks.
		if(pReceiver->m_PlayerFlags & PLAYERFLAG_SCOREBOARD || IsPaused)
			continue;

		CPlayer *pTarget = m_pGameContext->m_apPlayers[TargetClientId];
		if(!pTarget || !m_aEnabled[TargetClientId] || pCore->Team(TargetClientId) != TEAM_FLOCK)
			continue;

		CCharacter *pTargetCharacter = pTarget->GetCharacter();
		const bool InRange = pTargetCharacter && pTargetCharacter->CanSnapCharacter(ReceiverClientId) && !pTargetCharacter->NetworkClipped(ReceiverClientId);
		if(!InRange && !m_aInfo[TargetClientId].m_ResetChatColor)
			continue;

		Info.m_aTeam[TargetClientId] = RainbowDisplayTeam(m_Color);
		Info.m_UpdateTeams = true;

		// SV_TEAMSSTATE also controls IsOtherTeam on the client. Marking the
		// receiver itself as the display-only super team makes the colored player
		// stay fully opaque and keeps local prediction/hooking behavior intact.
		if(InRange && pCore->Team(ReceiverClientId) == pCore->Team(TargetClientId))
			Info.m_aTeam[OwnMapId] = TEAM_SUPER;
	}

	// Let the chatter's own client see its current rainbow team for one update.
	// This resets the temporary super display team and keeps chat-name coloring
	// consistent after the player has sent a message.
	if(Info.m_ResetChatColor)
	{
		Info.m_aTeam[OwnMapId] = m_aEnabled[ReceiverClientId] ? RainbowDisplayTeam(m_Color) : pCore->Team(ReceiverClientId);
		Info.m_UpdateTeams = true;
	}
}

void CRainbowName::ClearDisplayTeams(int ReceiverClientId)
{
	SInfo &Info = m_aInfo[ReceiverClientId];
	Info.m_UpdateTeams = false;
	Info.m_ResetChatColor = false;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		Info.m_aTeam[ClientId] = -1;
}
