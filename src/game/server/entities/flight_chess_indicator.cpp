#include "flight_chess_indicator.h"

#include <game/generated/protocol.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr float PI = 3.14159265358979323846f;
constexpr float TRANSITION_SECONDS = 0.62f;
constexpr float TRANSITION_ARC_HEIGHT = 34.0f;
}

CFlightChessTurnIndicator::CFlightChessTurnIndicator(CGameWorld *pGameWorld, int Team, int TargetClientId) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_FLIGHT_CHESS_INDICATOR, vec2()),
	m_Team(Team),
	m_TargetClientId(TargetClientId)
{
	GameWorld()->InsertEntity(this);
}

CFlightChessTurnIndicator::~CFlightChessTurnIndicator()
{
	if(GameServer())
		GameServer()->OnFlightChessIndicatorDestroyed(this);
}

bool CFlightChessTurnIndicator::CanSee(int SnappingClient)
{
	if(!GameServer()->IsFlightChessParticipant(m_Team, m_TargetClientId))
		return false;
	if(SnappingClient == SERVER_DEMO_CLIENT || SnappingClient < 0 || SnappingClient >= MAX_CLIENTS)
		return false;
	int ViewerTeam = GameServer()->GetDDRaceTeam(SnappingClient);
	CPlayer *pViewer = GameServer()->m_apPlayers[SnappingClient];
	if(pViewer && (pViewer->GetTeam() == TEAM_SPECTATORS || pViewer->IsPaused()) && pViewer->SpectatorId() >= 0 && pViewer->SpectatorId() < MAX_CLIENTS)
		ViewerTeam = GameServer()->GetDDRaceTeam(pViewer->SpectatorId());
	return m_Team != TEAM_FLOCK && ViewerTeam == m_Team;
}

void CFlightChessTurnIndicator::SetTargetClientId(int ClientId)
{
	if(ClientId == m_TargetClientId)
		return;
	m_TransitionStart = m_Pos;
	m_TransitionStartTick = Server()->Tick();
	m_Transitioning = m_Pos != vec2();
	m_TargetClientId = ClientId;
}

void CFlightChessTurnIndicator::Tick()
{
	if(!GameServer()->IsFlightChessParticipant(m_Team, m_TargetClientId))
	{
		Destroy();
		return;
	}
	CCharacter *pCharacter = GameServer()->GetPlayerChar(m_TargetClientId);
	if(!pCharacter)
		return;
	const vec2 Target = pCharacter->GetPos() + vec2(0.0f, -56.0f);
	if(m_Pos == vec2())
	{
		m_Pos = Target;
		m_Transitioning = false;
	}
	else if(m_Transitioning)
	{
		const int Duration = maximum((int)(Server()->TickSpeed() * TRANSITION_SECONDS), 1);
		const float Amount = std::clamp((Server()->Tick() - m_TransitionStartTick) / (float)Duration, 0.0f, 1.0f);
		// Quintic ease has zero velocity at both players. A shallow upward arc
		// makes the hand-over visible even when they stand at a similar height.
		const float Smooth = Amount * Amount * Amount * (Amount * (Amount * 6.0f - 15.0f) + 10.0f);
		m_Pos = m_TransitionStart + (Target - m_TransitionStart) * Smooth;
		m_Pos.y -= std::sin(Amount * PI) * TRANSITION_ARC_HEIGHT;
		if(Amount >= 1.0f)
		{
			m_Pos = Target;
			m_Transitioning = false;
		}
	}
	else
		m_Pos += (Target - m_Pos) * 0.22f;
}

void CFlightChessTurnIndicator::Snap(int SnappingClient)
{
	if(!CanSee(SnappingClient) || NetworkClipped(SnappingClient, m_Pos))
		return;
	const CSnapContext Context(GameServer()->GetClientVersion(SnappingClient), Server()->IsSixup(SnappingClient), SnappingClient);
	// This is only the network appearance of a health pickup. The entity is not
	// ENTTYPE_PICKUP and has no collision, freeze, collection or respawn logic.
	const float Hover = std::sin(Server()->Tick() * 0.13f) * 2.0f;
	GameServer()->SnapPickup(Context, GetId(), m_Pos + vec2(0.0f, Hover), POWERUP_HEALTH, 0, 0, PICKUPFLAG_NO_PREDICT);
}
