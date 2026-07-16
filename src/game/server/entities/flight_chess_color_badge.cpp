#include "flight_chess_color_badge.h"

#include "flight_chess.h"

#include <game/generated/protocol.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

namespace
{
constexpr vec2 BADGE_OFFSET(48.0f, -48.0f);
constexpr float FOLLOW_FACTOR = 0.22f;

int BadgeLaserType(int Color)
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

CFlightChessColorBadge::CFlightChessColorBadge(CGameWorld *pGameWorld, int Team, int OwnerClientId, int Color) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_FLIGHT_CHESS_INDICATOR, vec2()),
	m_Team(Team),
	m_OwnerClientId(OwnerClientId),
	m_Color(Color)
{
	GameWorld()->InsertEntity(this);
}

CFlightChessColorBadge::~CFlightChessColorBadge()
{
	if(GameServer())
		GameServer()->OnFlightChessColorBadgeDestroyed(this);
}

bool CFlightChessColorBadge::CanSee(int SnappingClient)
{
	if(!GameServer()->IsFlightChessParticipant(m_Team, m_OwnerClientId))
		return false;
	if(SnappingClient == SERVER_DEMO_CLIENT || SnappingClient < 0 || SnappingClient >= MAX_CLIENTS)
		return false;
	int ViewerTeam = GameServer()->GetDDRaceTeam(SnappingClient);
	CPlayer *pViewer = GameServer()->m_apPlayers[SnappingClient];
	if(pViewer && (pViewer->GetTeam() == TEAM_SPECTATORS || pViewer->IsPaused()) && pViewer->SpectatorId() >= 0 && pViewer->SpectatorId() < MAX_CLIENTS)
		ViewerTeam = GameServer()->GetDDRaceTeam(pViewer->SpectatorId());
	return m_Team != TEAM_FLOCK && ViewerTeam == m_Team;
}

void CFlightChessColorBadge::Tick()
{
	if(!GameServer()->IsFlightChessParticipant(m_Team, m_OwnerClientId))
	{
		Destroy();
		return;
	}
	CCharacter *pCharacter = GameServer()->GetPlayerChar(m_OwnerClientId);
	if(!pCharacter)
		return;
	const vec2 Target = pCharacter->GetPos() + BADGE_OFFSET;
	if(m_Pos == vec2())
		m_Pos = Target;
	else
		m_Pos += (Target - m_Pos) * FOLLOW_FACTOR;
}

void CFlightChessColorBadge::Snap(int SnappingClient)
{
	if(m_Pos == vec2() || !CanSee(SnappingClient) || NetworkClipped(SnappingClient, m_Pos))
		return;
	const CSnapContext Context(GameServer()->GetClientVersion(SnappingClient), Server()->IsSixup(SnappingClient), SnappingClient);
	// The laser head is rendered at To, so keep To on the smoothed badge
	// position and hide the one-unit helper segment behind it.
	GameServer()->SnapLaserObject(Context, GetId(), m_Pos, m_Pos + vec2(1.0f, 0.0f), -1, -1, BadgeLaserType(m_Color));
}
