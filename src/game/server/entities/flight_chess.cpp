#include "flight_chess.h"

#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <algorithm>
#include <cmath>

namespace
{
bool ViewerCanSeeTeam(CGameContext *pGameServer, int Team, int SnappingClient)
{
	if(SnappingClient == SERVER_DEMO_CLIENT || SnappingClient < 0 || SnappingClient >= MAX_CLIENTS)
		return false;
	int ViewerTeam = pGameServer->GetDDRaceTeam(SnappingClient);
	CPlayer *pViewer = pGameServer->m_apPlayers[SnappingClient];
	if(pViewer && (pViewer->GetTeam() == TEAM_SPECTATORS || pViewer->IsPaused()) && pViewer->SpectatorId() >= 0 && pViewer->SpectatorId() < MAX_CLIENTS)
		ViewerTeam = pGameServer->GetDDRaceTeam(pViewer->SpectatorId());
	return Team != TEAM_FLOCK && ViewerTeam == Team;
}

int PieceLaserType(int Color)
{
	switch(Color)
	{
	case CFlightChessPiece::RED: return LASERTYPE_FREEZE;
	case CFlightChessPiece::BLUE: return LASERTYPE_RIFLE;
	case CFlightChessPiece::BROWN: return LASERTYPE_SHOTGUN;
	default: return LASERTYPE_DOOR;
	}
}
}

CFlightChessPiece::CFlightChessPiece(CGameWorld *pGameWorld, int Team, int Color, int Plane, vec2 Pos) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_FLIGHT_CHESS_PIECE, Pos),
	m_Team(Team), m_Color(Color), m_Plane(Plane)
{
	GameWorld()->InsertEntity(this);
}

CFlightChessPiece::~CFlightChessPiece()
{
	if(GameServer())
		GameServer()->OnFlightChessEntityDestroyed(this);
}

bool CFlightChessPiece::CanSee(int SnappingClient)
{
	return GameServer()->IsFlightChessTeamActive(m_Team) && ViewerCanSeeTeam(GameServer(), m_Team, SnappingClient);
}

void CFlightChessPiece::SetPos(vec2 Pos)
{
	m_Pos = Pos;
	m_MoveDuration = 0;
	m_MoveArcHeight = 0.0f;
}

void CFlightChessPiece::MoveTo(vec2 Pos, int Ticks, float ArcHeight)
{
	m_MoveStart = m_Pos;
	m_MoveTarget = Pos;
	m_MoveStartTick = Server()->Tick();
	m_MoveDuration = maximum(Ticks, 1);
	m_MoveArcHeight = maximum(ArcHeight, 0.0f);
}

void CFlightChessPiece::Tick()
{
	if(!GameServer()->IsFlightChessTeamActive(m_Team))
	{
		Destroy();
		return;
	}
	if(m_MoveDuration <= 0)
		return;
	const float Amount = std::clamp((Server()->Tick() - m_MoveStartTick) / (float)m_MoveDuration, 0.0f, 1.0f);
	// Smoothstep keeps every cell transition visible without a hard stop.
	const float Smooth = Amount * Amount * (3.0f - 2.0f * Amount);
	m_Pos = m_MoveStart + (m_MoveTarget - m_MoveStart) * Smooth;
	m_Pos.y -= std::sin(Amount * 3.14159265358979323846f) * m_MoveArcHeight;
	if(Amount >= 1.0f)
	{
		m_Pos = m_MoveTarget;
		m_MoveDuration = 0;
		m_MoveArcHeight = 0.0f;
	}
}

void CFlightChessPiece::Snap(int SnappingClient)
{
	if(!CanSee(SnappingClient) || NetworkClipped(SnappingClient, m_Pos))
		return;
	// The client renders the laser head at `To`. Keep that endpoint exactly on
	// the logical board-cell centre and put the one-unit helper segment behind
	// it. This matches the proven flight-chess position markers and the die pips.
	GameServer()->SnapLaserObject(CSnapContext(GameServer()->GetClientVersion(SnappingClient), Server()->IsSixup(SnappingClient), SnappingClient), GetId(),
		m_Pos, m_Pos + vec2(1.0f, 0.0f), -1, -1, PieceLaserType(m_Color));
}
