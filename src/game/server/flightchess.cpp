#include "gamecontext.h"
#include "gamecontroller.h"

#include <base/system.h>
#include <game/server/entities/character.h>
#include <game/server/entities/dice_3d.h>
#include <game/server/entities/flight_chess_color_badge.h>
#include <game/server/entities/flight_chess_indicator.h>
#include <game/server/player.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr int FLIGHT_INVITE_SECONDS = 45;
constexpr int FLIGHT_TURN_SECONDS = 60;
constexpr int FLIGHT_ROUTE_CELLS = 52;
constexpr int FLIGHT_MAIN_CELLS = 50;
constexpr int FLIGHT_HOME_CELLS = 6;
constexpr int FLIGHT_FINISHED = FLIGHT_MAIN_CELLS + FLIGHT_HOME_CELLS;
constexpr int FLIGHT_MOVE_TICKS = 7;
constexpr int FLIGHT_LAUNCH_TICKS = 18;
constexpr int FLIGHT_FAST_MOVE_TICKS = 24;
constexpr int FLIGHT_CAPTURE_TICKS = 20;
constexpr float FLIGHT_STEP_ARC = 3.5f;
constexpr float FLIGHT_LAUNCH_ARC = 30.0f;
constexpr float FLIGHT_CAPTURE_ARC = 38.0f;

// Outer route in actual travel order. Index 0 is the red launch cell. The 12
// icon-bearing cells missing from the old marker list were recovered from the
// same 4928,352..5344,768 board transform instead of being guessed.
constexpr std::array<vec2, FLIGHT_ROUTE_CELLS> s_aMainRoute = {
	vec2(4962.04f, 483.88f), vec2(4988.63f, 473.70f), vec2(5012.88f, 473.70f), vec2(5039.67f, 483.18f),
	vec2(5060.38f, 464.22f), vec2(5049.72f, 437.61f), vec2(5049.26f, 412.62f), vec2(5059.98f, 386.24f),
	vec2(5086.10f, 376.06f), vec2(5110.82f, 376.06f), vec2(5135.28f, 376.59f), vec2(5159.78f, 376.06f),
	vec2(5184.50f, 376.06f), vec2(5211.09f, 385.78f), vec2(5221.35f, 412.62f), vec2(5220.88f, 437.61f),
	vec2(5211.23f, 464.22f), vec2(5231.26f, 484.14f), vec2(5259.12f, 473.70f), vec2(5281.97f, 473.70f),
	vec2(5308.09f, 483.42f), vec2(5318.35f, 510.72f), vec2(5318.82f, 534.78f), vec2(5318.11f, 559.57f),
	vec2(5318.35f, 584.29f), vec2(5318.35f, 608.82f), vec2(5309.02f, 635.66f), vec2(5281.97f, 645.84f),
	vec2(5257.26f, 645.37f), vec2(5231.26f, 636.21f), vec2(5210.37f, 655.28f), vec2(5220.88f, 682.86f),
	vec2(5220.88f, 706.46f), vec2(5210.15f, 733.76f), vec2(5183.10f, 742.55f), vec2(5159.78f, 743.94f),
	vec2(5135.32f, 742.85f), vec2(5110.82f, 743.01f), vec2(5086.10f, 743.94f), vec2(5059.98f, 733.29f),
	vec2(5049.26f, 706.92f), vec2(5049.72f, 681.93f), vec2(5059.32f, 655.39f), vec2(5039.68f, 635.21f),
	vec2(5013.35f, 645.37f), vec2(4988.63f, 645.37f), vec2(4962.04f, 635.66f), vec2(4951.78f, 608.82f),
	vec2(4952.25f, 584.29f), vec2(4952.69f, 559.67f), vec2(4951.78f, 534.78f), vec2(4951.78f, 510.72f),
};

// Red, blue, brown/yellow, green respectively. The four directional circles
// are the gates into these lanes; the lane ends at the centre star.
constexpr std::array<std::array<vec2, FLIGHT_HOME_CELLS>, 4> s_aaHomeRoute = {{
	{{vec2(4988.63f, 559.77f), vec2(5013.81f, 559.31f), vec2(5039.00f, 559.31f), vec2(5064.18f, 559.77f), vec2(5089.36f, 559.31f), vec2(5114.55f, 559.31f)}},
	{{vec2(5282.44f, 559.77f), vec2(5257.26f, 559.77f), vec2(5232.07f, 559.31f), vec2(5206.42f, 559.31f), vec2(5181.70f, 559.77f), vec2(5156.52f, 559.77f)}},
	{{vec2(5135.53f, 412.62f), vec2(5135.53f, 437.61f), vec2(5135.53f, 463.52f), vec2(5135.53f, 488.51f), vec2(5135.53f, 513.49f), vec2(5135.53f, 538.48f)}},
	{{vec2(5135.53f, 708.31f), vec2(5135.53f, 683.32f), vec2(5135.53f, 657.87f), vec2(5135.53f, 632.88f), vec2(5135.53f, 607.89f), vec2(5135.53f, 581.05f)}},
}};

constexpr std::array<std::array<vec2, 4>, 4> s_aaHangars = {{
	{{vec2(4955.53f, 380.69f), vec2(4998.44f, 380.69f), vec2(4955.07f, 420.44f), vec2(4998.44f, 420.44f)}},
	{{vec2(5272.97f, 698.28f), vec2(5315.88f, 699.20f), vec2(5272.97f, 738.48f), vec2(5315.42f, 739.41f)}},
	{{vec2(5272.97f, 380.23f), vec2(5315.88f, 381.16f), vec2(5272.97f, 420.91f), vec2(5315.88f, 420.91f)}},
	{{vec2(4955.53f, 698.28f), vec2(4998.44f, 698.28f), vec2(4955.53f, 738.94f), vec2(4998.44f, 738.94f)}},
}};

// Index after launching for the four colours. They are spaced one quadrant
// apart along the same cyclic main route.
constexpr std::array<int, 4> s_aStartIndices = {0, 26, 13, 39};
struct SFastRoute
{
	int m_Color;
	int m_From;
	int m_To;
};

// The eight aircraft icons form four colour-locked shortcut pairs. Their
// physical order around the board differs from the player-colour enum order.
constexpr std::array<SFastRoute, 4> s_aFastRoutes = {{
	{CFlightChessPiece::GREEN, 4, 16},
	{CFlightChessPiece::RED, 17, 29},
	{CFlightChessPiece::BROWN, 30, 42},
	{CFlightChessPiece::BLUE, 43, 3},
}};

bool SamePos(vec2 A, vec2 B)
{
	return distance(A, B) < 1.0f;
}

int MainIndexFor(int Color, int Progress)
{
	return (s_aStartIndices[Color] + Progress) % FLIGHT_ROUTE_CELLS;
}

vec2 PlaneBasePosition(int Color, int Plane, int Progress)
{
	if(Progress < 0)
		return s_aaHangars[Color][Plane];
	if(Progress < FLIGHT_MAIN_CELLS)
		return s_aMainRoute[MainIndexFor(Color, Progress)];
	if(Progress < FLIGHT_FINISHED)
		return s_aaHomeRoute[Color][Progress - FLIGHT_MAIN_CELLS];
	return s_aaHomeRoute[Color].back();
}

}

int CGameContext::FindFlightChessTeam(int ClientId)
{
	for(int Team = 1; Team < TEAM_SUPER; Team++)
		for(int Slot = 0; Slot < 4; Slot++)
			if(m_aFlightChessGames[Team].m_aPlayers[Slot] == ClientId)
				return Team;
	return -1;
}

bool CGameContext::IsFlightChessReadyPlayer(int ClientId)
{
	return ClientId >= 0 && ClientId < MAX_CLIENTS && m_apPlayers[ClientId] && Server()->ClientIngame(ClientId) &&
		m_apPlayers[ClientId]->GetTeam() != TEAM_SPECTATORS && !m_apPlayers[ClientId]->IsPaused() && GetPlayerChar(ClientId);
}

bool CGameContext::IsFlightChessTeamActive(int Team) const
{
	return Team > TEAM_FLOCK && Team < TEAM_SUPER && m_aFlightChessGames[Team].m_aPlayers[0] >= 0;
}

bool CGameContext::IsFlightChessParticipant(int Team, int ClientId) const
{
	if(!IsFlightChessTeamActive(Team) || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	for(int Slot = 0; Slot < 4; Slot++)
		if(m_aFlightChessGames[Team].m_aPlayers[Slot] == ClientId)
			return true;
	return false;
}

void CGameContext::OnFlightChessEntityDestroyed(CFlightChessPiece *pPiece)
{
	for(int Team = 1; Team < TEAM_SUPER; Team++)
	{
		SFlightChessGame &Game = m_aFlightChessGames[Team];
		if(pPiece)
			for(CFlightChessPiece *&pPlane : Game.m_vPlanes)
				if(pPlane == pPiece)
					pPlane = nullptr;
	}
}

void CGameContext::OnFlightChessColorBadgeDestroyed(CFlightChessColorBadge *pBadge)
{
	for(SFlightChessGame &Game : m_aFlightChessGames)
		for(CFlightChessColorBadge *&pStoredBadge : Game.m_apColorBadges)
			if(pStoredBadge == pBadge)
				pStoredBadge = nullptr;
}

void CGameContext::OnFlightChessIndicatorDestroyed(CFlightChessTurnIndicator *pIndicator)
{
	for(SFlightChessGame &Game : m_aFlightChessGames)
		if(Game.m_pTurnIndicator == pIndicator)
			Game.m_pTurnIndicator = nullptr;
}

void CGameContext::OnFlightChessDiceDestroyed(CDice3D *pDice)
{
	for(SFlightChessGame &Game : m_aFlightChessGames)
		if(Game.m_pDice == pDice)
			Game.m_pDice = nullptr;
}

bool CGameContext::CanHammerFlightChessDice(const CDice3D *pDice, int ClientId) const
{
	if(!pDice || !pDice->IsFlightChessDice())
		return true;
	const int Team = pDice->FlightChessTeam();
	if(Team <= TEAM_FLOCK || Team >= TEAM_SUPER)
		return false;
	const SFlightChessGame &Game = m_aFlightChessGames[Team];
	return Game.m_pDice == pDice && Game.m_Phase == 0 && Game.m_TurnSlot >= 0 && Game.m_TurnSlot < 4 &&
		Game.m_aPlayers[Game.m_TurnSlot] == ClientId && pDice->OwnerClientId() == ClientId;
}

void CGameContext::OnFlightChessDiceThrown(CDice3D *pDice, int ClientId)
{
	if(!CanHammerFlightChessDice(pDice, ClientId))
		return;
	SFlightChessGame &Game = m_aFlightChessGames[pDice->FlightChessTeam()];
	Game.m_Phase = 1;
	Game.m_DieValue = 0;
	Game.m_DeadlineTick = Server()->Tick() + FLIGHT_TURN_SECONDS * Server()->TickSpeed();
}

void CGameContext::OnFlightChessDiceSettled(CDice3D *pDice)
{
	if(!pDice || !pDice->IsFlightChessDice())
		return;
	const int Team = pDice->FlightChessTeam();
	if(Team <= TEAM_FLOCK || Team >= TEAM_SUPER)
		return;
	SFlightChessGame &Game = m_aFlightChessGames[Team];
	if(Game.m_pDice != pDice || Game.m_Phase != 1 || Game.m_TurnSlot < 0 || Game.m_TurnSlot >= 4)
		return;
	Game.m_DieValue = pDice->Result();
	const int Slot = Game.m_TurnSlot;
	bool HasMove = false;
	for(int Plane = 0; Plane < 4; Plane++)
	{
		const int Progress = Game.m_aPlaneProgress[Slot][Plane];
		HasMove = HasMove || (Progress < 0 && Game.m_DieValue == 6) || (Progress >= 0 && Progress < FLIGHT_FINISHED);
	}
	char aBuf[160];
	str_format(aBuf, sizeof(aBuf), "点数：%d。", Game.m_DieValue);
	SendChatTeam(Team, aBuf);
	if(!HasMove)
	{
		SendChatTarget(Game.m_aPlayers[Slot], "当前没有可移动的飞机。 ");
		AdvanceFlightChessTurn(Team, false);
		return;
	}
	Game.m_Phase = 2;
	Game.m_DeadlineTick = Server()->Tick() + FLIGHT_TURN_SECONDS * Server()->TickSpeed();
	SendChatTarget(Game.m_aPlayers[Slot], "用准心点击要移动的飞机。 ");
}

void CGameContext::UpdateFlightChessPlanePositions(int Team)
{
	if(Team <= TEAM_FLOCK || Team >= TEAM_SUPER)
		return;
	SFlightChessGame &Game = m_aFlightChessGames[Team];
	static constexpr std::array<vec2, 8> s_aOverlapOffsets = {vec2(-2.5f, -2.5f), vec2(2.5f, -2.5f), vec2(-2.5f, 2.5f), vec2(2.5f, 2.5f), vec2(-3.5f, 0), vec2(3.5f, 0), vec2(0, -3.5f), vec2(0, 3.5f)};
	for(int Color = 0; Color < 4; Color++)
		if(Game.m_aPlayers[Color] >= 0)
		for(int Plane = 0; Plane < 4; Plane++)
		{
			const int Index = Color * 4 + Plane;
			const vec2 Base = PlaneBasePosition(Color, Plane, Game.m_aPlaneProgress[Color][Plane]);
			int Occupants = 0, Rank = 0;
			for(int OtherColor = 0; OtherColor < 4; OtherColor++)
				if(Game.m_aPlayers[OtherColor] >= 0)
					for(int OtherPlane = 0; OtherPlane < 4; OtherPlane++)
						if(SamePos(Base, PlaneBasePosition(OtherColor, OtherPlane, Game.m_aPlaneProgress[OtherColor][OtherPlane])))
						{
							Occupants++;
							if(OtherColor * 4 + OtherPlane < Index)
								Rank++;
						}
			const vec2 Offset = Occupants > 1 ? s_aOverlapOffsets[Rank % (int)s_aOverlapOffsets.size()] : vec2();
			if(Index < (int)Game.m_vPlanes.size() && Game.m_vPlanes[Index])
			{
				CFlightChessPiece *pPiece = Game.m_vPlanes[Index];
				const vec2 Target = Base + Offset;
				const float Distance = distance(pPiece->GetPos(), Target);
				if(Distance > 0.05f && Distance <= 12.0f)
					pPiece->MoveTo(Target, 5);
				else
					pPiece->SetPos(Target);
			}
		}
}

void CGameContext::UpdateFlightChessTurnIndicator(int Team)
{
	if(Team <= TEAM_FLOCK || Team >= TEAM_SUPER)
		return;
	SFlightChessGame &Game = m_aFlightChessGames[Team];
	const int ClientId = Game.m_TurnSlot >= 0 && Game.m_TurnSlot < 4 ? Game.m_aPlayers[Game.m_TurnSlot] : -1;
	if(ClientId < 0 || Game.m_Phase < 0)
		return;
	if(!Game.m_pTurnIndicator)
		Game.m_pTurnIndicator = new CFlightChessTurnIndicator(&m_World, Team, ClientId);
	else
		Game.m_pTurnIndicator->SetTargetClientId(ClientId);
}

void CGameContext::RebuildFlightChessEntities(int Team)
{
	if(Team <= TEAM_FLOCK || Team >= TEAM_SUPER)
		return;
	SFlightChessGame &Game = m_aFlightChessGames[Team];
	for(CFlightChessPiece *pPlane : Game.m_vPlanes)
		if(pPlane)
			pPlane->Destroy();
	Game.m_vPlanes.clear();
	for(CFlightChessColorBadge *pBadge : Game.m_apColorBadges)
		if(pBadge)
			pBadge->Destroy();
	if(Game.m_pDice)
		Game.m_pDice->Destroy();
	Game.m_pDice = nullptr;
	if(Game.m_pTurnIndicator)
		Game.m_pTurnIndicator->Destroy();
	Game.m_pTurnIndicator = nullptr;
	if(Game.m_aPlayers[0] < 0 || Game.m_Phase < 0)
		return;
	Game.m_vPlanes.assign(16, nullptr);
	for(int Color = 0; Color < 4; Color++)
		if(Game.m_aPlayers[Color] >= 0)
		{
			Game.m_apColorBadges[Color] = new CFlightChessColorBadge(&m_World, Team, Game.m_aPlayers[Color], Color);
			for(int Plane = 0; Plane < 4; Plane++)
				Game.m_vPlanes[Color * 4 + Plane] = new CFlightChessPiece(&m_World, Team, Color, Plane, PlaneBasePosition(Color, Plane, Game.m_aPlaneProgress[Color][Plane]));
		}
	const vec2 SpawnA = GomokuSpawn(166);
	const vec2 SpawnB = GomokuSpawn(167);
	const vec2 DicePos = SpawnA.x >= 0.0f && SpawnB.x >= 0.0f ? (SpawnA + SpawnB) * 0.5f + vec2(0.0f, -112.0f) : vec2(5135.0f, 700.0f);
	Game.m_pDice = new CDice3D(&m_World, DicePos, Team, Game.m_aPlayers[Game.m_TurnSlot]);
	UpdateFlightChessPlanePositions(Team);
	UpdateFlightChessTurnIndicator(Team);
}

void CGameContext::ClearFlightChessGame(int Team, int WinnerSlot, const char *pReason)
{
	if(Team <= TEAM_FLOCK || Team >= TEAM_SUPER)
		return;
	SFlightChessGame &Game = m_aFlightChessGames[Team];
	bool HasStoredState = !Game.m_vPlanes.empty() || Game.m_pDice || Game.m_pTurnIndicator;
	for(int Slot = 0; Slot < 4; Slot++)
		HasStoredState = HasStoredState || Game.m_aPlayers[Slot] >= 0 || Game.m_apColorBadges[Slot];
	if(!HasStoredState)
		return;
	const std::array<int, 4> Players = {Game.m_aPlayers[0], Game.m_aPlayers[1], Game.m_aPlayers[2], Game.m_aPlayers[3]};
	char aBuf[128];
	if(WinnerSlot >= 0 && WinnerSlot < 4 && Game.m_aPlayers[WinnerSlot] >= 0)
		str_format(aBuf, sizeof(aBuf), "%s 获胜（%s）。", Server()->ClientName(Game.m_aPlayers[WinnerSlot]), pReason ? pReason : "结束");
	else
		str_format(aBuf, sizeof(aBuf), "飞行棋结束（%s）。", pReason ? pReason : "结束");
	if(m_pController->Teams().Count(Team) > 0)
		SendChatTeam(Team, aBuf);
	for(CFlightChessPiece *pPlane : Game.m_vPlanes)
		if(pPlane)
			pPlane->Destroy();
	Game.m_vPlanes.clear();
	for(CFlightChessColorBadge *pBadge : Game.m_apColorBadges)
		if(pBadge)
			pBadge->Destroy();
	if(Game.m_pDice)
		Game.m_pDice->Destroy();
	if(Game.m_pTurnIndicator)
		Game.m_pTurnIndicator->Destroy();
	Game = SFlightChessGame{};
	if(m_aSavedFlightChessGames[Team])
		*m_aSavedFlightChessGames[Team] = SFlightChessSavedGame{};
	m_pController->Teams().SetTeamLock(Team, false);
	// Clean both the recorded participants and any unexpected player that entered
	// the reused/unlocked team. This keeps a stale game from leaking into the next
	// minigame that happens to receive the same DDRace team number.
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		const bool RecordedParticipant = std::find(Players.begin(), Players.end(), ClientId) != Players.end();
		if(m_apPlayers[ClientId] && (RecordedParticipant || GetDDRaceTeam(ClientId) == Team))
		{
			m_pController->Teams().SetForceCharacterTeam(ClientId, TEAM_FLOCK);
			if(CCharacter *pChar = GetPlayerChar(ClientId))
				pChar->Die(WEAPON_GAME, WEAPON_GAME);
		}
	}
}

bool CGameContext::HandleFlightChessReady(int ClientId)
{
	const int Team = FindFlightChessTeam(ClientId);
	if(Team < 0)
		return false;
	SFlightChessGame &Game = m_aFlightChessGames[Team];
	if(Game.m_Phase >= 0)
		return true;
	int Slot = -1, PlayerMask = 0;
	for(int i = 0; i < 4; i++)
		if(Game.m_aPlayers[i] >= 0)
		{
			PlayerMask |= 1 << i;
			if(Game.m_aPlayers[i] == ClientId)
				Slot = i;
		}
	if(Slot < 0)
		return true;
	Game.m_ReadyMask |= 1 << Slot;
	if(Game.m_ReadyMask != PlayerMask)
	{
		SendChatTeam(Team, "等待其余玩家输入 /ready。 ");
		return true;
	}
	Game.m_TurnSlot = (int)(secure_rand() % 4);
	while(Game.m_aPlayers[Game.m_TurnSlot] < 0)
		Game.m_TurnSlot = (Game.m_TurnSlot + 1) % 4;
	for(int SlotIndex = 0; SlotIndex < 4; SlotIndex++)
		if(Game.m_aPlayers[SlotIndex] >= 0)
			if(CCharacter *pChar = GetPlayerChar(Game.m_aPlayers[SlotIndex]))
			{
				for(int Weapon = WEAPON_GUN; Weapon < NUM_WEAPONS; Weapon++)
				{
					pChar->SetWeaponGot(Weapon, false);
					pChar->SetWeaponAmmo(Weapon, 0);
				}
				pChar->GiveWeapon(WEAPON_HAMMER, true);
				pChar->SetActiveWeapon(WEAPON_HAMMER);
			}
	Game.m_Phase = 0;
	Game.m_DieValue = 0;
	Game.m_DeadlineTick = Server()->Tick() + FLIGHT_TURN_SECONDS * Server()->TickSpeed();
	RebuildFlightChessEntities(Team);
	char aBuf[160];
	str_format(aBuf, sizeof(aBuf), "飞行棋开始。%s 先手。", Server()->ClientName(Game.m_aPlayers[Game.m_TurnSlot]));
	SendChatTeam(Team, aBuf);
	return true;
}

void CGameContext::SaveFlightChessHotReloadState()
{
	for(int Team = 1; Team < TEAM_SUPER; Team++)
	{
		SFlightChessSavedGame &Saved = *m_aSavedFlightChessGames[Team];
		Saved = SFlightChessSavedGame{};
		const SFlightChessGame &Game = m_aFlightChessGames[Team];
		if(Game.m_aPlayers[0] < 0)
			continue;
		Saved.m_Active = true;
		std::copy(std::begin(Game.m_aPlayers), std::end(Game.m_aPlayers), std::begin(Saved.m_aPlayers));
		Saved.m_TurnSlot = Game.m_TurnSlot;
		Saved.m_ReadyMask = Game.m_ReadyMask;
		// Plane movement is rebuilt from the committed board progress. Dice motion,
		// however, is deterministic recorded playback and can resume at the exact
		// frame where hot_reload interrupted it.
		Saved.m_Phase = Game.m_Phase == 3 ? 0 : Game.m_Phase;
		Saved.m_DieValue = Game.m_DieValue;
		Saved.m_RemainingTicks = maximum(Game.m_DeadlineTick - Server()->Tick(), 0);
		std::copy(&Game.m_aPlaneProgress[0][0], &Game.m_aPlaneProgress[0][0] + 16, &Saved.m_aPlaneProgress[0][0]);
		if(Game.m_pDice)
			Game.m_pDice->SavePersistentState(&Saved.m_DiceState);
	}
}

void CGameContext::RestoreFlightChessHotReloadState()
{
	for(int Team = 1; Team < TEAM_SUPER; Team++)
	{
		SFlightChessSavedGame &Saved = *m_aSavedFlightChessGames[Team];
		if(!Saved.m_Active)
			continue;
		if(m_aFlightChessGames[Team].m_aPlayers[0] >= 0)
		{
			Saved = SFlightChessSavedGame{};
			continue;
		}

		// Client slots temporarily leave the ingame state while the same map is
		// reloaded. Do not mistake that transition for a disconnect and erase the
		// only saved copy of the game. Wait until every participating tee has been
		// recreated and its hot-reload character/team state has been loaded.
		bool PlayersRestored = true;
		for(int Slot = 0; Slot < 4; Slot++)
			if(Saved.m_aPlayers[Slot] >= 0)
			{
				const int ClientId = Saved.m_aPlayers[Slot];
				PlayersRestored = PlayersRestored && ClientId < MAX_CLIENTS && m_apPlayers[ClientId] && Server()->ClientIngame(ClientId) && GetPlayerChar(ClientId);
			}
		if(!PlayersRestored)
			continue;

		SFlightChessGame &Game = m_aFlightChessGames[Team];
		Game = SFlightChessGame{};
		std::copy(std::begin(Saved.m_aPlayers), std::end(Saved.m_aPlayers), std::begin(Game.m_aPlayers));
		Game.m_TurnSlot = Saved.m_TurnSlot;
		Game.m_ReadyMask = Saved.m_ReadyMask;
		Game.m_Phase = Saved.m_Phase;
		Game.m_DieValue = Saved.m_DieValue;
		Game.m_DeadlineTick = Server()->Tick() + Saved.m_RemainingTicks;
		std::copy(&Saved.m_aPlaneProgress[0][0], &Saved.m_aPlaneProgress[0][0] + 16, &Game.m_aPlaneProgress[0][0]);
		m_pController->Teams().SetTeamLock(Team, true);
		for(int Slot = 0; Slot < 4; Slot++)
			if(Game.m_aPlayers[Slot] >= 0)
				m_pController->Teams().SetForceCharacterTeam(Game.m_aPlayers[Slot], Team);
		RebuildFlightChessEntities(Team);
		if(Game.m_pDice && Saved.m_DiceState.m_Valid)
			Game.m_pDice->LoadPersistentState(Saved.m_DiceState);
		else if(Game.m_Phase == 1)
		{
			// Compatibility with saves made before dice-state persistence existed.
			Game.m_Phase = 0;
			Game.m_DieValue = 0;
		}
		Saved = SFlightChessSavedGame{};
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "飞行棋已恢复，轮到 %s。", Server()->ClientName(Game.m_aPlayers[Game.m_TurnSlot]));
		SendChatTeam(Team, aBuf);
	}
}

void CGameContext::AbortFlightChessFor(int ClientId, const char *pReason)
{
	const int Team = FindFlightChessTeam(ClientId);
	if(Team >= 0)
	{
		int Winner = -1;
		for(int Slot = 0; Slot < 4; Slot++)
			if(m_aFlightChessGames[Team].m_aPlayers[Slot] >= 0 && m_aFlightChessGames[Team].m_aPlayers[Slot] != ClientId)
			{
				Winner = Slot;
				break;
			}
		ClearFlightChessGame(Team, Winner, pReason);
	}
	for(SFlightChessInvite &Invite : m_aFlightChessInvites)
		if(Invite.m_Active)
			for(int &Player : Invite.m_aPlayers)
				if(Player == ClientId)
					Invite = SFlightChessInvite{};
}

bool CGameContext::TryFlightChessClick(int ClientId)
{
	const int Team = FindFlightChessTeam(ClientId);
	if(Team < 0)
		return false;
	SFlightChessGame &Game = m_aFlightChessGames[Team];
	int Slot = -1;
	for(int i = 0; i < 4; i++)
		if(Game.m_aPlayers[i] == ClientId)
			Slot = i;
	if(Slot < 0 || Game.m_Phase < 0 || Game.m_TurnSlot != Slot)
		return false;
	CCharacter *pChar = GetPlayerChar(ClientId);
	if(!pChar || !m_apPlayers[ClientId])
		return true;
	const vec2 Target = m_apPlayers[ClientId]->m_CameraInfo.ConvertTargetToWorld(pChar->GetPos(), pChar->AimTarget());
	if(Game.m_Phase == 0)
		return false;
	if(Game.m_Phase != 2)
		return true;
	int Picked = -1;
	for(int Plane = 0; Plane < 4; Plane++)
	{
		const int Index = Slot * 4 + Plane;
		if(Index < (int)Game.m_vPlanes.size() && Game.m_vPlanes[Index] && distance(Target, Game.m_vPlanes[Index]->GetPos()) <= 18.0f)
		{
			Picked = Plane;
			break;
		}
	}
	if(Picked < 0)
	{
		SendChatTarget(ClientId, "请用准心点击自己要移动的飞机。 ");
		return true;
	}
	int &Progress = Game.m_aPlaneProgress[Slot][Picked];
	const bool Launching = Progress < 0;
	if(Progress < 0)
	{
		if(Game.m_DieValue != 6)
		{
			SendChatTarget(ClientId, "只有掷出 6 才能让机坪内的飞机起飞。 ");
			return true;
		}
	}
	else if(Progress >= FLIGHT_FINISHED)
	{
		SendChatTarget(ClientId, "这架飞机已经到达终点。 ");
		return true;
	}

	Game.m_vMovePath.clear();
	Game.m_MoveFinishes = false;
	Game.m_FastMovePathIndex = -1;
	if(Progress < 0)
	{
		Game.m_vMovePath.push_back(0);
	}
	else
	{
		int Current = Progress;
		int Direction = 1;
		for(int Step = 0; Step < Game.m_DieValue; Step++)
		{
			if(Current == FLIGHT_FINISHED - 1 && Direction > 0)
				Direction = -1;
			Current += Direction;
			Game.m_vMovePath.push_back(Current);
		}
		Game.m_MoveFinishes = Current == FLIGHT_FINISHED - 1 && Direction > 0;

		// A fast route only accepts its matching colour. Store only the exit as
		// one extra movement segment so the piece flies directly between the two
		// aircraft icons instead of walking through the intervening grid cells.
		if(!Game.m_MoveFinishes && Current >= 0 && Current < FLIGHT_MAIN_CELLS)
		{
			const int Global = MainIndexFor(Slot, Current);
			for(const SFastRoute &FastRoute : s_aFastRoutes)
				if(FastRoute.m_Color == Slot && Global == FastRoute.m_From)
				{
					const int Delta = (FastRoute.m_To - FastRoute.m_From + FLIGHT_ROUTE_CELLS) % FLIGHT_ROUTE_CELLS;
					if(Current + Delta < FLIGHT_MAIN_CELLS)
					{
						Game.m_FastMovePathIndex = (int)Game.m_vMovePath.size();
						Game.m_vMovePath.push_back(Current + Delta);
					}
					break;
				}
		}
	}

	if(Game.m_vMovePath.empty())
		return true;
	Game.m_Phase = 3;
	Game.m_MovingPlane = Picked;
	Game.m_MovePathIndex = 1;
	Progress = Game.m_vMovePath.front();
	const int PieceIndex = Slot * 4 + Picked;
	const int FirstMoveTicks = Launching ? FLIGHT_LAUNCH_TICKS : FLIGHT_MOVE_TICKS;
	if(PieceIndex < (int)Game.m_vPlanes.size() && Game.m_vPlanes[PieceIndex])
		Game.m_vPlanes[PieceIndex]->MoveTo(PlaneBasePosition(Slot, Picked, Progress), FirstMoveTicks, Launching ? FLIGHT_LAUNCH_ARC : FLIGHT_STEP_ARC);
	Game.m_NextMoveTick = Server()->Tick() + FirstMoveTicks;
	Game.m_DeadlineTick = Server()->Tick() + FLIGHT_TURN_SECONDS * Server()->TickSpeed();
	return true;
}

void CGameContext::AdvanceFlightChessTurn(int Team, bool KeepCurrentPlayer)
{
	if(Team <= TEAM_FLOCK || Team >= TEAM_SUPER)
		return;
	SFlightChessGame &Game = m_aFlightChessGames[Team];
	if(Game.m_aPlayers[0] < 0 || Game.m_TurnSlot < 0)
		return;
	if(!KeepCurrentPlayer)
		do
		{
			Game.m_TurnSlot = (Game.m_TurnSlot + 1) % 4;
		} while(Game.m_aPlayers[Game.m_TurnSlot] < 0);
	Game.m_Phase = 0;
	Game.m_DieValue = 0;
	Game.m_MovingPlane = -1;
	Game.m_vMovePath.clear();
	Game.m_MovePathIndex = 0;
	Game.m_FastMovePathIndex = -1;
	Game.m_MoveFinishes = false;
	if(Game.m_pDice)
		Game.m_pDice->SetOwnerClientId(Game.m_aPlayers[Game.m_TurnSlot]);
	UpdateFlightChessTurnIndicator(Team);
	Game.m_DeadlineTick = Server()->Tick() + FLIGHT_TURN_SECONDS * Server()->TickSpeed();
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), KeepCurrentPlayer ? "%s 继续掷骰子。" : "轮到 %s 掷骰子。", Server()->ClientName(Game.m_aPlayers[Game.m_TurnSlot]));
	SendChatTeam(Team, aBuf);
}

void CGameContext::FinishFlightChessMove(int Team)
{
	if(Team <= TEAM_FLOCK || Team >= TEAM_SUPER)
		return;
	SFlightChessGame &Game = m_aFlightChessGames[Team];
	const int Slot = Game.m_TurnSlot;
	const int Plane = Game.m_MovingPlane;
	if(Slot < 0 || Slot >= 4 || Plane < 0 || Plane >= 4)
	{
		AdvanceFlightChessTurn(Team, false);
		return;
	}
	int &Progress = Game.m_aPlaneProgress[Slot][Plane];
	if(Game.m_MoveFinishes)
		Progress = FLIGHT_FINISHED;

	std::array<CFlightChessPiece *, 16> apCaptured{};
	std::array<vec2, 16> aCaptureStarts{};
	int NumCaptured = 0;
	if(Progress >= 0 && Progress < FLIGHT_MAIN_CELLS)
	{
		const int Global = MainIndexFor(Slot, Progress);
		for(int OtherColor = 0; OtherColor < 4; OtherColor++)
			if(OtherColor != Slot && Game.m_aPlayers[OtherColor] >= 0)
				for(int OtherPlane = 0; OtherPlane < 4; OtherPlane++)
					if(Game.m_aPlaneProgress[OtherColor][OtherPlane] >= 0 && Game.m_aPlaneProgress[OtherColor][OtherPlane] < FLIGHT_MAIN_CELLS && MainIndexFor(OtherColor, Game.m_aPlaneProgress[OtherColor][OtherPlane]) == Global)
					{
						const int CapturedIndex = OtherColor * 4 + OtherPlane;
						if(CapturedIndex < (int)Game.m_vPlanes.size() && Game.m_vPlanes[CapturedIndex] && NumCaptured < (int)apCaptured.size())
						{
							apCaptured[NumCaptured] = Game.m_vPlanes[CapturedIndex];
							aCaptureStarts[NumCaptured] = Game.m_vPlanes[CapturedIndex]->GetPos();
							NumCaptured++;
						}
						Game.m_aPlaneProgress[OtherColor][OtherPlane] = -1;
					}
	}
	UpdateFlightChessPlanePositions(Team);
	for(int Captured = 0; Captured < NumCaptured; Captured++)
	{
		CFlightChessPiece *pPiece = apCaptured[Captured];
		if(!pPiece)
			continue;
		const vec2 HangarTarget = pPiece->GetPos();
		pPiece->SetPos(aCaptureStarts[Captured]);
		pPiece->MoveTo(HangarTarget, FLIGHT_CAPTURE_TICKS, FLIGHT_CAPTURE_ARC);
	}
	bool Won = true;
	for(int OtherPlane = 0; OtherPlane < 4; OtherPlane++)
		Won = Won && Game.m_aPlaneProgress[Slot][OtherPlane] >= FLIGHT_FINISHED;
	if(Won)
	{
		ClearFlightChessGame(Team, Slot, "全部飞机抵达终点");
		return;
	}
	AdvanceFlightChessTurn(Team, Game.m_DieValue == 6);
}

void CGameContext::TickFlightChess()
{
	RestoreFlightChessHotReloadState();
	for(SFlightChessInvite &Invite : m_aFlightChessInvites)
		if(Invite.m_Active && Server()->Tick() >= Invite.m_ExpireTick)
		{
			if(Invite.m_Inviter >= 0 && m_apPlayers[Invite.m_Inviter])
				SendChatTarget(Invite.m_Inviter, "飞行棋邀请已过期。 ");
			Invite = SFlightChessInvite{};
		}
	for(int Team = 1; Team < TEAM_SUPER; Team++)
	{
		SFlightChessGame &Game = m_aFlightChessGames[Team];
		if(Game.m_aPlayers[0] < 0)
			continue;
		int ParticipantCount = 0;
		for(int Slot = 0; Slot < 4; Slot++)
			ParticipantCount += Game.m_aPlayers[Slot] >= 0;
		if(ParticipantCount < 2 || m_pController->Teams().Count(Team) != ParticipantCount || !m_pController->Teams().TeamLocked(Team))
		{
			ClearFlightChessGame(Team, -1, "队伍成员发生变化");
			continue;
		}
		for(int Slot = 0; Slot < 4; Slot++)
			if(Game.m_aPlayers[Slot] >= 0)
			{
				const int ClientId = Game.m_aPlayers[Slot];
				CPlayer *pPlayer = ClientId >= 0 && ClientId < MAX_CLIENTS ? m_apPlayers[ClientId] : nullptr;
				const bool Missing = !pPlayer || !Server()->ClientIngame(ClientId);
				const bool LeftTeam = !Missing && GetDDRaceTeam(ClientId) != Team;
				// Pause/spectator pause may temporarily have no active character while the
				// DDRace team is retained. A normal /kill has no character and is not paused.
				const bool Dead = !Missing && !GetPlayerChar(ClientId) && !pPlayer->IsPaused();
				if(Missing || LeftTeam || Dead)
				{
					ClearFlightChessGame(Team, -1, Missing ? "玩家离开" : LeftTeam ? "玩家退出队伍" : "玩家死亡");
					break;
				}
			}
		if(Game.m_aPlayers[0] < 0)
			continue;
		if(Game.m_Phase < 0)
			continue;
		UpdateFlightChessTurnIndicator(Team);
		if(Game.m_Phase == 3 && Server()->Tick() >= Game.m_NextMoveTick)
		{
			if(Game.m_MovePathIndex < (int)Game.m_vMovePath.size())
			{
				const int Plane = Game.m_MovingPlane;
				if(Plane < 0 || Plane >= 4 || Game.m_TurnSlot < 0 || Game.m_TurnSlot >= 4)
				{
					AdvanceFlightChessTurn(Team, false);
					continue;
				}
				int &Progress = Game.m_aPlaneProgress[Game.m_TurnSlot][Plane];
				const int PathIndex = Game.m_MovePathIndex++;
				const bool FastMove = PathIndex == Game.m_FastMovePathIndex;
				Progress = Game.m_vMovePath[PathIndex];
				const int PieceIndex = Game.m_TurnSlot * 4 + Plane;
				if(PieceIndex < (int)Game.m_vPlanes.size() && Game.m_vPlanes[PieceIndex])
					Game.m_vPlanes[PieceIndex]->MoveTo(PlaneBasePosition(Game.m_TurnSlot, Plane, Progress), FastMove ? FLIGHT_FAST_MOVE_TICKS : FLIGHT_MOVE_TICKS, FastMove ? 0.0f : FLIGHT_STEP_ARC);
				Game.m_NextMoveTick = Server()->Tick() + (FastMove ? FLIGHT_FAST_MOVE_TICKS : FLIGHT_MOVE_TICKS);
			}
			else
			{
				FinishFlightChessMove(Team);
				if(m_aFlightChessGames[Team].m_aPlayers[0] < 0)
					continue;
			}
		}
		if(Game.m_DeadlineTick > 0 && Server()->Tick() >= Game.m_DeadlineTick)
		{
			const int TimedOutClient = Game.m_aPlayers[Game.m_TurnSlot];
			AdvanceFlightChessTurn(Team, false);
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "%s 超时。", Server()->ClientName(TimedOutClient));
			SendChatTeam(Team, aBuf);
		}
	}
}

void CGameContext::ConChatFlightChess(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int ClientId = pResult->m_ClientId;
	const char *pArg = pResult->NumArguments() ? pResult->GetString(0) : "";
	if(!pArg[0])
	{
		pSelf->SendChatTarget(ClientId, "用法：/fly <玩家1> [玩家2] [玩家3]；受邀者使用 /fly accept 或 /fly decline。 ");
		return;
	}
	if(str_comp_nocase(pArg, "cancel") == 0)
	{
		pSelf->AbortFlightChessFor(ClientId, "对局取消");
		return;
	}
	int InviteIndex = -1, InviteSlot = -1;
	for(int i = 0; i < MAX_CLIENTS; i++) if(pSelf->m_aFlightChessInvites[i].m_Active)
		for(int Slot = 1; Slot < 4; Slot++) if(pSelf->m_aFlightChessInvites[i].m_aPlayers[Slot] == ClientId) { InviteIndex = i; InviteSlot = Slot; break; }
	if(str_comp_nocase(pArg, "decline") == 0)
	{
		pSelf->CloseGameInviteVote(ClientId);
		if(InviteIndex < 0) pSelf->SendChatTarget(ClientId, "没有待处理的飞行棋邀请。 ");
		else { pSelf->SendChatTarget(pSelf->m_aFlightChessInvites[InviteIndex].m_Inviter, "有玩家拒绝了飞行棋邀请。 "); pSelf->m_aFlightChessInvites[InviteIndex] = SFlightChessInvite{}; }
		return;
	}
	if(str_comp_nocase(pArg, "accept") == 0)
	{
		pSelf->CloseGameInviteVote(ClientId);
		if(InviteIndex < 0 || !pSelf->IsFlightChessReadyPlayer(ClientId) || pSelf->GetDDRaceTeam(ClientId) != TEAM_FLOCK)
		{
			pSelf->SendChatTarget(ClientId, "当前没有可接受的飞行棋邀请。 ");
			return;
		}
		SFlightChessInvite &Invite = pSelf->m_aFlightChessInvites[InviteIndex];
		Invite.m_aAccepted[InviteSlot] = true;
		bool AllAccepted = true;
		int Count = 0;
		for(int Slot = 0; Slot < 4 && Invite.m_aPlayers[Slot] >= 0; Slot++) { AllAccepted = AllAccepted && Invite.m_aAccepted[Slot]; Count++; }
		if(!AllAccepted)
		{
			pSelf->SendChatTarget(ClientId, "已接受，等待其他玩家。 ");
			return;
		}
		int Team = -1;
		for(int Candidate = 1; Candidate < TEAM_SUPER; Candidate++)
			if(pSelf->m_pController->Teams().Count(Candidate) == 0 && pSelf->m_aGomokuGames[Candidate].m_Black < 0 && pSelf->m_aConnect4Games[Candidate].m_Black < 0 && pSelf->m_aFlightChessGames[Candidate].m_aPlayers[0] < 0) { Team = Candidate; break; }
		const vec2 SpawnA = pSelf->GomokuSpawn(166), SpawnB = pSelf->GomokuSpawn(167);
		if(Team < 0 || SpawnA.x < 0 || SpawnB.x < 0)
		{
			pSelf->SendChatTarget(Invite.m_Inviter, "地图缺少 166/167 飞行棋出生点，或没有空闲队伍。 ");
			Invite = SFlightChessInvite{};
			return;
		}
		std::array<int, 4> Slots = {-1, -1, -1, -1};
		for(int i = 0; i < Count; i++) Slots[i] = Invite.m_aPlayers[i];
		for(int i = Count - 1; i > 0; i--) std::swap(Slots[i], Slots[(int)(secure_rand() % (i + 1))]);
		SFlightChessGame &Game = pSelf->m_aFlightChessGames[Team];
		Game = SFlightChessGame{};
		std::fill(&Game.m_aPlaneProgress[0][0], &Game.m_aPlaneProgress[0][0] + 16, -1);
		for(int Slot = 0; Slot < Count; Slot++)
		{
			Game.m_aPlayers[Slot] = Slots[Slot];
			pSelf->m_pController->Teams().SetForceCharacterTeam(Slots[Slot], Team);
			pSelf->GetPlayerChar(Slots[Slot])->SetPosition((Slot & 1 ? SpawnB : SpawnA) + vec2((Slot / 2) * 20.0f, 0.0f));
		}
		Game.m_TurnSlot = -1;
		Game.m_Phase = -1;
		Game.m_DeadlineTick = 0;
		pSelf->m_pController->Teams().SetTeamLock(Team, true);
		pSelf->SendChatTeam(Team, "飞行棋已就绪；所有参与者输入 /ready 后开始。 ");
		Invite = SFlightChessInvite{};
		return;
	}
	if(!pSelf->IsFlightChessReadyPlayer(ClientId) || pSelf->GetDDRaceTeam(ClientId) != TEAM_FLOCK || pSelf->FindFlightChessTeam(ClientId) >= 0 || pSelf->FindGomokuTeam(ClientId) >= 0 || pSelf->FindConnect4Team(ClientId) >= 0)
	{
		pSelf->SendChatTarget(ClientId, "你当前不能发起飞行棋。 ");
		return;
	}
	std::istringstream Input(pArg);
	std::string Name;
	std::vector<int> vPlayers = {ClientId};
	while(Input >> Name)
	{
		int Target = -1;
		for(int i = 0; i < MAX_CLIENTS; i++)
			if(i != ClientId && pSelf->m_apPlayers[i] && str_comp(Name.c_str(), pSelf->Server()->ClientName(i)) == 0) { Target = i; break; }
		if(Target < 0 || !pSelf->IsFlightChessReadyPlayer(Target) || pSelf->GetDDRaceTeam(Target) != TEAM_FLOCK || pSelf->FindFlightChessTeam(Target) >= 0 || pSelf->FindGomokuTeam(Target) >= 0 || pSelf->FindConnect4Team(Target) >= 0 || pSelf->HasGameInviteVote(Target) || std::find(vPlayers.begin(), vPlayers.end(), Target) != vPlayers.end())
		{
			pSelf->SendChatTarget(ClientId, "邀请名单含无效玩家；昵称中不能含空格。 ");
			return;
		}
		vPlayers.push_back(Target);
		if(vPlayers.size() > 4)
		{
			pSelf->SendChatTarget(ClientId, "飞行棋最多四名玩家。 ");
			return;
		}
	}
	if(vPlayers.size() < 2 || pSelf->m_aFlightChessInvites[ClientId].m_Active)
	{
		pSelf->SendChatTarget(ClientId, "请邀请 1 至 3 名玩家，且你当前没有待处理邀请。 ");
		return;
	}
	SFlightChessInvite &Invite = pSelf->m_aFlightChessInvites[ClientId];
	Invite = SFlightChessInvite{};
	Invite.m_Active = true;
	Invite.m_Inviter = ClientId;
	Invite.m_ExpireTick = pSelf->Server()->Tick() + FLIGHT_INVITE_SECONDS * pSelf->Server()->TickSpeed();
	for(int i = 0; i < (int)vPlayers.size(); i++) { Invite.m_aPlayers[i] = vPlayers[i]; Invite.m_aAccepted[i] = i == 0; }
	for(int i = 1; i < (int)vPlayers.size(); i++)
	{
		pSelf->ShowGameInviteVote(vPlayers[i], ClientId, GAME_INVITE_VOTE_FLIGHT_CHESS, Invite.m_ExpireTick);
		char aBuf[160];
		str_format(aBuf, sizeof(aBuf), "%s 邀请你玩飞行棋：F3 同意、F4 拒绝，也可使用 /fly accept 或 /fly decline。", pSelf->Server()->ClientName(ClientId));
		pSelf->SendChatTarget(vPlayers[i], aBuf);
	}
	pSelf->SendChatTarget(ClientId, "飞行棋邀请已发送。 ");
}
