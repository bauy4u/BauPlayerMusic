#include "gomoku_piece.h"

#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CGomokuPiece::CGomokuPiece(CGameWorld *pGameWorld, int Team, int Owner, vec2 Pos, EKind Kind) :
	CEntity(pGameWorld, Kind == COUNTDOWN ? CGameWorld::ENTTYPE_PROJECTILE : CGameWorld::ENTTYPE_LASER, Pos),
	m_Team(Team), m_Owner(Owner), m_Kind(Kind), m_From(Pos)
{
	GameWorld()->InsertEntity(this);
}

CGomokuPiece::~CGomokuPiece()
{
	// Game state owns no entities, but caches their addresses for updates.
	// Unregister this address before CEntity removes it from the world.
	if(GameServer())
		GameServer()->OnGomokuPieceDestroyed(this);
}

bool CGomokuPiece::CanSee(int SnappingClient)
{
	if(SnappingClient == SERVER_DEMO_CLIENT || SnappingClient < 0 || SnappingClient >= MAX_CLIENTS)
		return false;
	// Temporary public positioning markers for the flight-chess board. Unlike
	// board-game pieces, these must be visible before a DDRace game team exists.
	if(m_Kind == FLIGHT_CHESS_TEST)
		return true;
	if((m_Kind == PREVIEW_BLACK || m_Kind == PREVIEW_WHITE) && SnappingClient != m_Owner)
		return false;
	int ViewerTeam = GameServer()->GetDDRaceTeam(SnappingClient);
	CPlayer *pViewer = GameServer()->m_apPlayers[SnappingClient];
	if(pViewer && (pViewer->GetTeam() == TEAM_SPECTATORS || pViewer->IsPaused()) && pViewer->SpectatorId() >= 0 && pViewer->SpectatorId() < MAX_CLIENTS)
		ViewerTeam = GameServer()->GetDDRaceTeam(pViewer->SpectatorId());
	return m_Team != TEAM_FLOCK && ViewerTeam == m_Team;
}

void CGomokuPiece::Snap(int SnappingClient)
{
	if(!m_Visible || !CanSee(SnappingClient) || NetworkClipped(SnappingClient, m_Pos))
		return;
	if(m_Kind == COUNTDOWN)
	{
		CNetObj_Projectile *pProj = Server()->SnapNewItem<CNetObj_Projectile>(GetId());
		if(!pProj)
			return;
		pProj->m_X = (int)m_Pos.x;
		pProj->m_Y = (int)m_Pos.y;
		pProj->m_VelX = 0;
		pProj->m_VelY = 0;
		pProj->m_StartTick = Server()->Tick();
		pProj->m_Type = WEAPON_HAMMER;
		return;
	}

	const bool IsWhite = m_Kind == WHITE || m_Kind == PREVIEW_WHITE || m_Kind == VICTORY_LINE_WHITE;
	const bool IsLine = m_Kind == VICTORY_LINE_BLACK || m_Kind == VICTORY_LINE_WHITE || m_Kind == CONNECT4_BORDER;
	const int LaserType = m_Kind == CONNECT4_BORDER ? LASERTYPE_DOOR : (IsWhite ? LASERTYPE_SHOTGUN : LASERTYPE_RIFLE);
	const vec2 From = IsLine ? m_From : m_Pos;
	const vec2 To = IsLine ? m_Pos : m_Pos + vec2(6.0f, 0.0f);
	GameServer()->SnapLaserObject(CSnapContext(GameServer()->GetClientVersion(SnappingClient), Server()->IsSixup(SnappingClient), SnappingClient), GetId(),
		From, To, -1, -1, LaserType);
}
