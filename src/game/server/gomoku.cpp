#include "gamecontext.h"
#include "gamecontroller.h"

#include <game/server/entities/character.h>
#include <game/server/player.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
constexpr int GOMOKU_BOARD_TILE = 160;
constexpr int GOMOKU_BLACK_SPAWN_TILE = 161;
constexpr int GOMOKU_WHITE_SPAWN_TILE = 162;
constexpr int GOMOKU_REQUEST_SECONDS = 30;
constexpr int GOMOKU_TURN_SECONDS = 30;
constexpr int GOMOKU_VICTORY_LINE_TICKS = 35;
constexpr int GOMOKU_VICTORY_FLY_TICKS = 45;
constexpr int GOMOKU_VICTORY_ORBIT_TICKS = 95;

bool HasCell(const std::vector<std::pair<int, int>> &vCells, int X, int Y)
{
	return std::find(vCells.begin(), vCells.end(), std::make_pair(X, Y)) != vCells.end();
}
}

void CGameContext::ToggleFlightChessTestHeads()
{
	if(!m_vFlightChessTestHeads.empty())
	{
		for(CGomokuPiece *pHead : m_vFlightChessTestHeads)
			if(pHead)
				pHead->Destroy();
		m_vFlightChessTestHeads.clear();
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "flightchess", "Flight-chess position markers cleared");
		return;
	}

	// Source image: 892x899 pixels mapped by the editor from world (4928,352)
	// through (5344,768). These are the 64 circular cells marked by the mapper.
	static constexpr std::array<vec2, 64> s_aCellPositions = {
		vec2(5086.10f, 376.06f), vec2(5110.82f, 376.06f), vec2(5159.78f, 376.06f), vec2(5184.50f, 376.06f),
		vec2(5211.09f, 385.78f), vec2(5059.98f, 386.24f), vec2(5049.26f, 412.62f), vec2(5135.53f, 412.62f),
		vec2(5221.35f, 412.62f), vec2(5049.72f, 437.61f), vec2(5135.53f, 437.61f), vec2(5220.88f, 437.61f),
		vec2(5135.53f, 463.52f), vec2(4988.63f, 473.70f), vec2(5012.88f, 473.70f), vec2(5259.12f, 473.70f),
		vec2(5281.97f, 473.70f), vec2(5308.09f, 483.42f), vec2(4962.04f, 483.88f), vec2(5135.53f, 488.51f),
		vec2(4951.78f, 510.72f), vec2(5318.35f, 510.72f), vec2(5135.53f, 513.49f), vec2(4951.78f, 534.78f),
		vec2(5318.82f, 534.78f), vec2(5135.53f, 538.48f), vec2(5013.81f, 559.31f), vec2(5039.00f, 559.31f),
		vec2(5089.36f, 559.31f), vec2(5114.55f, 559.31f), vec2(5206.42f, 559.31f), vec2(5232.07f, 559.31f),
		vec2(4988.63f, 559.77f), vec2(5064.18f, 559.77f), vec2(5156.52f, 559.77f), vec2(5181.70f, 559.77f),
		vec2(5257.26f, 559.77f), vec2(5282.44f, 559.77f), vec2(5135.53f, 581.05f), vec2(4952.25f, 584.29f),
		vec2(5318.35f, 584.29f), vec2(5135.53f, 607.89f), vec2(4951.78f, 608.82f), vec2(5318.35f, 608.82f),
		vec2(5135.53f, 632.88f), vec2(4962.04f, 635.66f), vec2(5309.02f, 635.66f), vec2(4988.63f, 645.37f),
		vec2(5013.35f, 645.37f), vec2(5257.26f, 645.37f), vec2(5281.97f, 645.84f), vec2(5135.53f, 657.87f),
		vec2(5049.72f, 681.93f), vec2(5220.88f, 682.86f), vec2(5135.53f, 683.32f), vec2(5220.88f, 706.46f),
		vec2(5049.26f, 706.92f), vec2(5135.53f, 708.31f), vec2(5059.98f, 733.29f), vec2(5210.15f, 733.76f),
		vec2(5183.10f, 742.55f), vec2(5110.82f, 743.01f), vec2(5086.10f, 743.94f), vec2(5159.78f, 743.94f),
	};
	for(const vec2 &Pos : s_aCellPositions)
		m_vFlightChessTestHeads.push_back(new CGomokuPiece(&m_World, TEAM_SUPER, -1, Pos, CGomokuPiece::FLIGHT_CHESS_TEST));
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "flightchess", "Spawned 64 flight-chess position markers");
}

void CGameContext::ConFlightChessTest(IConsole::IResult *pResult, void *pUserData)
{
	((CGameContext *)pUserData)->ToggleFlightChessTestHeads();
}

void CGameContext::OnGomokuPieceDestroyed(CGomokuPiece *pPiece)
{
	if(!pPiece)
		return;
	auto ClearPointer = [pPiece](std::vector<CGomokuPiece *> &vPieces) {
		for(CGomokuPiece *&pVisual : vPieces)
			if(pVisual == pPiece)
				pVisual = nullptr;
	};
	for(int Team = 1; Team < TEAM_SUPER; Team++)
	{
		SGomokuGame &Gomoku = m_aGomokuGames[Team];
		ClearPointer(Gomoku.m_vStones);
		ClearPointer(Gomoku.m_vCountdown);
		ClearPointer(Gomoku.m_vVictoryVisuals);
		ClearPointer(Gomoku.m_vVictoryLines);
		if(Gomoku.m_pPreview == pPiece)
			Gomoku.m_pPreview = nullptr;

		SConnect4Game &Connect4 = m_aConnect4Games[Team];
		ClearPointer(Connect4.m_vStones);
		ClearPointer(Connect4.m_vCountdown);
		ClearPointer(Connect4.m_vBorders);
		if(Connect4.m_pPreview == pPiece)
			Connect4.m_pPreview = nullptr;
		if(Connect4.m_pFallingStone == pPiece)
			Connect4.m_pFallingStone = nullptr;
	}
	ClearPointer(m_vFlightChessTestHeads);
}

int CGameContext::FindGomokuTeam(int ClientId)
{
	for(int Team = 1; Team < TEAM_SUPER; Team++)
		if(m_aGomokuGames[Team].m_Black == ClientId || m_aGomokuGames[Team].m_White == ClientId)
			return Team;
	return -1;
}

int CGameContext::FindOutgoingGomokuRequest(int ClientId)
{
	for(int i = 0; i < MAX_CLIENTS; i++)
		if(m_aGomokuRequester[i] == ClientId)
			return i;
	return -1;
}

bool CGameContext::IsGomokuReadyPlayer(int ClientId)
{
	return ClientId >= 0 && ClientId < MAX_CLIENTS && m_apPlayers[ClientId] && Server()->ClientIngame(ClientId) &&
		m_apPlayers[ClientId]->GetTeam() != TEAM_SPECTATORS && !m_apPlayers[ClientId]->IsPaused() && GetPlayerChar(ClientId);
}

bool CGameContext::IsGomokuBoardCell(int GridX, int GridY)
{
	const int Index = Collision()->GetPureMapIndex(vec2(GridX * 32.0f + 16.0f, GridY * 32.0f + 16.0f));
	return Index >= 0 && Collision()->GetTileIndex(Index) == GOMOKU_BOARD_TILE;
}

bool CGameContext::GomokuAimCell(int ClientId, int *pGridX, int *pGridY)
{
	if(!pGridX || !pGridY || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	CCharacter *pChar = GetPlayerChar(ClientId);
	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(!pChar || !pPlayer)
		return false;
	// The aim vector is relative to the client's camera. Undo the received
	// camera zoom/deadzone/follow settings before converting it to a board cell.
	const vec2 Target = pPlayer->m_CameraInfo.ConvertTargetToWorld(pChar->GetPos(), pChar->AimTarget());
	// The board is made from raw game tiles; each tile centre is one legal
	// position. Keep preview and the confirmed stone on this exact same cell.
	*pGridX = (int)std::floor(Target.x / 32.0f);
	*pGridY = (int)std::floor(Target.y / 32.0f);
	return true;
}

vec2 CGameContext::GomokuSpawn(int Tile)
{
	for(int Y = 0; Y < Collision()->GetHeight(); Y++)
		for(int X = 0; X < Collision()->GetWidth(); X++)
		{
			const int Index = Y * Collision()->GetWidth() + X;
			if(Collision()->GetTileIndex(Index) == Tile)
				return Collision()->GetPos(Index);
		}
	return vec2(-1, -1);
}

void CGameContext::CancelGomokuRequest(int TargetId, const char *pReason)
{
	if(TargetId < 0 || TargetId >= MAX_CLIENTS)
		return;
	const int Requester = m_aGomokuRequester[TargetId];
	if(Requester < 0)
		return;
	m_aGomokuRequester[TargetId] = -1;
	m_aGomokuRequestTick[TargetId] = 0;
	if(m_apPlayers[Requester])
		SendChatTarget(Requester, pReason);
	if(m_apPlayers[TargetId])
		SendChatTarget(TargetId, pReason);
}

void CGameContext::RebuildGomokuCountdown(int Team)
{
	SGomokuGame &Game = m_aGomokuGames[Team];
	for(CGomokuPiece *pVisual : Game.m_vCountdown)
		if(pVisual)
			pVisual->Destroy();
	Game.m_vCountdown.clear();
	if(Game.m_Turn < 0)
		return;
	const int Remaining = std::clamp((Game.m_DeadlineTick - Server()->Tick() + Server()->TickSpeed() - 1) / Server()->TickSpeed(), 0, GOMOKU_TURN_SECONDS);
	const vec2 Center = GomokuSpawn(Game.m_Turn == Game.m_Black ? GOMOKU_BLACK_SPAWN_TILE : GOMOKU_WHITE_SPAWN_TILE);
	for(int i = 0; i < GOMOKU_TURN_SECONDS; i++)
	{
		const float Angle = 2.0f * pi * i / GOMOKU_TURN_SECONDS - pi / 2.0f;
		CGomokuPiece *pDot = new CGomokuPiece(&m_World, Team, -1, Center + vec2(std::cos(Angle), std::sin(Angle)) * 42.0f, CGomokuPiece::COUNTDOWN);
		pDot->SetVisible(i < Remaining);
		Game.m_vCountdown.push_back(pDot);
	}
}

void CGameContext::UpdateGomokuPreview(int Team)
{
	SGomokuGame &Game = m_aGomokuGames[Team];
	if(Game.m_Turn < 0)
		return;
	int X, Y;
	if(!GomokuAimCell(Game.m_Turn, &X, &Y))
		return;
	const bool Valid = IsGomokuBoardCell(X, Y) && !HasCell(Game.m_vBlack, X, Y) && !HasCell(Game.m_vWhite, X, Y);
	if(Game.m_pPreview)
	{
		Game.m_pPreview->SetPos(vec2(X * 32.0f + 16.0f, Y * 32.0f + 16.0f));
		Game.m_pPreview->SetVisible(Valid);
	}
}

void CGameContext::UpdateGomokuCountdown(int Team)
{
	if(Team <= TEAM_FLOCK || Team >= TEAM_SUPER)
		return;
	SGomokuGame &Game = m_aGomokuGames[Team];
	if(Game.m_Turn < 0)
		return;
	const int Remaining = std::clamp((Game.m_DeadlineTick - Server()->Tick() + Server()->TickSpeed() - 1) / Server()->TickSpeed(), 0, GOMOKU_TURN_SECONDS);
	for(int i = 0; i < (int)Game.m_vCountdown.size(); i++)
		if(Game.m_vCountdown[i])
			Game.m_vCountdown[i]->SetVisible(i < Remaining);
}

void CGameContext::StartGomokuVictory(int Team, int Winner, const std::vector<std::pair<int, int>> &vWinningLine)
{
	SGomokuGame &Game = m_aGomokuGames[Team];
	Game.m_VictoryWinner = Winner;
	Game.m_VictoryLoser = Winner == Game.m_Black ? Game.m_White : Game.m_Black;
	Game.m_VictoryStartTick = Server()->Tick();
	Game.m_vWinningLine = vWinningLine;
	Game.m_Turn = -2; // Lock the board while the victory animation owns its visuals.

	for(CGomokuPiece *pVisual : Game.m_vStones)
		if(pVisual)
			pVisual->Destroy();
	Game.m_vStones.clear();
	for(CGomokuPiece *pVisual : Game.m_vCountdown)
		if(pVisual)
			pVisual->Destroy();
	Game.m_vCountdown.clear();
	if(Game.m_pPreview)
		Game.m_pPreview->Destroy();
	Game.m_pPreview = nullptr;

	const std::vector<std::pair<int, int>> &vWinnerCells = Winner == Game.m_Black ? Game.m_vBlack : Game.m_vWhite;
	const CGomokuPiece::EKind StoneKind = Winner == Game.m_Black ? CGomokuPiece::BLACK : CGomokuPiece::WHITE;
	for(const auto &Cell : vWinnerCells)
	{
		const vec2 Pos(Cell.first * 32.0f + 16.0f, Cell.second * 32.0f + 16.0f);
		Game.m_vVictoryStarts.push_back(Pos);
		Game.m_vVictoryVisuals.push_back(new CGomokuPiece(&m_World, Team, -1, Pos, StoneKind));
	}

	if(vWinningLine.size() >= 2)
	{
		const vec2 First(vWinningLine.front().first * 32.0f + 16.0f, vWinningLine.front().second * 32.0f + 16.0f);
		const vec2 Last(vWinningLine.back().first * 32.0f + 16.0f, vWinningLine.back().second * 32.0f + 16.0f);
		const CGomokuPiece::EKind LineKind = Winner == Game.m_Black ? CGomokuPiece::VICTORY_LINE_BLACK : CGomokuPiece::VICTORY_LINE_WHITE;
		CGomokuPiece *pLeft = new CGomokuPiece(&m_World, Team, -1, First, LineKind);
		CGomokuPiece *pRight = new CGomokuPiece(&m_World, Team, -1, Last, LineKind);
		pLeft->SetLaser(First, First);
		pRight->SetLaser(Last, Last);
		Game.m_vVictoryLines.push_back(pLeft);
		Game.m_vVictoryLines.push_back(pRight);
	}
	if(CCharacter *pLoser = GetPlayerChar(Game.m_VictoryLoser))
		pLoser->Freeze(5);
}

void CGameContext::FinishGomokuVictory(int Team)
{
	SGomokuGame &Game = m_aGomokuGames[Team];
	const int Winner = Game.m_VictoryWinner;
	const int Loser = Game.m_VictoryLoser;
	for(CGomokuPiece *pVisual : Game.m_vVictoryVisuals)
		if(pVisual)
			pVisual->Destroy();
	for(CGomokuPiece *pVisual : Game.m_vVictoryLines)
		if(pVisual)
			pVisual->Destroy();
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s 获胜。", Winner >= 0 && m_apPlayers[Winner] ? Server()->ClientName(Winner) : "胜方");
	SendChatTeam(Team, aBuf);
	for(const int ClientId : {Winner, Loser})
	{
		if(ClientId < 0 || !m_apPlayers[ClientId])
			continue;
		m_pController->Teams().SetForceCharacterTeam(ClientId, TEAM_FLOCK);
		if(CCharacter *pChar = GetPlayerChar(ClientId))
			pChar->Die(WEAPON_GAME, WEAPON_GAME);
	}
	Game = SGomokuGame{};
	m_pController->Teams().SetTeamLock(Team, false);
}

void CGameContext::SaveGomokuHotReloadState()
{
	for(int Team = 1; Team < TEAM_SUPER; Team++)
	{
		SGomokuSavedGame &Saved = *m_aSavedGomokuGames[Team];
		Saved = SGomokuSavedGame{};
		const SGomokuGame &Game = m_aGomokuGames[Team];
		if(Game.m_Black < 0 || Game.m_White < 0)
			continue;
		Saved.m_Active = true;
		Saved.m_Black = Game.m_Black;
		Saved.m_White = Game.m_White;
		Saved.m_ReadyMask = Game.m_ReadyMask;
		Saved.m_Turn = Game.m_Turn;
		Saved.m_RemainingTurnTicks = Game.m_Turn >= 0 ? maximum(Game.m_DeadlineTick - Server()->Tick(), 0) : 0;
		Saved.m_vBlack = Game.m_vBlack;
		Saved.m_vWhite = Game.m_vWhite;
		Saved.m_vWinningLine = Game.m_vWinningLine;
		Saved.m_VictoryWinner = Game.m_VictoryWinner;
		Saved.m_VictoryElapsedTicks = Game.m_VictoryWinner >= 0 ? maximum(Server()->Tick() - Game.m_VictoryStartTick, 0) : 0;
	}
}

void CGameContext::RestoreGomokuHotReloadState()
{
	for(int Team = 1; Team < TEAM_SUPER; Team++)
	{
		SGomokuSavedGame &Saved = *m_aSavedGomokuGames[Team];
		if(!Saved.m_Active)
			continue;
		// The saved tees and their DDRace teams are restored asynchronously.
		if(!GetPlayerChar(Saved.m_Black) || !GetPlayerChar(Saved.m_White))
			continue;
		SGomokuGame &Game = m_aGomokuGames[Team];
		if(Game.m_Black >= 0)
		{
			Saved.m_Active = false;
			continue;
		}
		Game.m_Black = Saved.m_Black;
		Game.m_White = Saved.m_White;
		Game.m_ReadyMask = Saved.m_ReadyMask;
		Game.m_Turn = Saved.m_Turn;
		Game.m_DeadlineTick = Server()->Tick() + Saved.m_RemainingTurnTicks;
		m_pController->Teams().SetTeamLock(Team, true);
		Game.m_vBlack = Saved.m_vBlack;
		Game.m_vWhite = Saved.m_vWhite;
		if(Saved.m_VictoryWinner >= 0)
		{
			StartGomokuVictory(Team, Saved.m_VictoryWinner, Saved.m_vWinningLine);
			Game.m_VictoryStartTick = Server()->Tick() - Saved.m_VictoryElapsedTicks;
		}
		else
		{
			for(const auto &Cell : Game.m_vBlack)
				Game.m_vStones.push_back(new CGomokuPiece(&m_World, Team, -1, vec2(Cell.first * 32.0f + 16.0f, Cell.second * 32.0f + 16.0f), CGomokuPiece::BLACK));
			for(const auto &Cell : Game.m_vWhite)
				Game.m_vStones.push_back(new CGomokuPiece(&m_World, Team, -1, vec2(Cell.first * 32.0f + 16.0f, Cell.second * 32.0f + 16.0f), CGomokuPiece::WHITE));
			if(Game.m_Turn >= 0)
			{
				RebuildGomokuCountdown(Team);
				Game.m_pPreview = new CGomokuPiece(&m_World, Team, Game.m_Turn, vec2(), Game.m_Turn == Game.m_Black ? CGomokuPiece::PREVIEW_BLACK : CGomokuPiece::PREVIEW_WHITE);
				UpdateGomokuPreview(Team);
			}
		}
		Saved.m_Active = false;
	}
}

void CGameContext::TickGomokuVictory(int Team)
{
	SGomokuGame &Game = m_aGomokuGames[Team];
	const int Elapsed = Server()->Tick() - Game.m_VictoryStartTick;
	if(Elapsed < GOMOKU_VICTORY_LINE_TICKS && Game.m_vWinningLine.size() >= 2 && Game.m_vVictoryLines.size() == 2)
	{
		const float Progress = std::clamp(Elapsed / (float)GOMOKU_VICTORY_LINE_TICKS, 0.0f, 1.0f);
		const vec2 First(Game.m_vWinningLine.front().first * 32.0f + 16.0f, Game.m_vWinningLine.front().second * 32.0f + 16.0f);
		const vec2 Last(Game.m_vWinningLine.back().first * 32.0f + 16.0f, Game.m_vWinningLine.back().second * 32.0f + 16.0f);
		const vec2 Middle = (First + Last) * 0.5f;
		if(Game.m_vVictoryLines[0])
			Game.m_vVictoryLines[0]->SetLaser(First, First + (Middle - First) * Progress);
		if(Game.m_vVictoryLines[1])
			Game.m_vVictoryLines[1]->SetLaser(Last, Last + (Middle - Last) * Progress);
	}
	else if(!Game.m_vVictoryLines.empty())
	{
		for(CGomokuPiece *pVisual : Game.m_vVictoryLines)
			if(pVisual)
				pVisual->Destroy();
		Game.m_vVictoryLines.clear();
	}

	CCharacter *pLoser = GetPlayerChar(Game.m_VictoryLoser);
	if(pLoser && Elapsed >= GOMOKU_VICTORY_LINE_TICKS)
	{
		const float Fly = std::clamp((Elapsed - GOMOKU_VICTORY_LINE_TICKS) / (float)GOMOKU_VICTORY_FLY_TICKS, 0.0f, 1.0f);
		const float Closing = std::clamp((Elapsed - GOMOKU_VICTORY_LINE_TICKS - GOMOKU_VICTORY_FLY_TICKS) / (float)GOMOKU_VICTORY_ORBIT_TICKS, 0.0f, 1.0f);
		const float SmoothFly = Fly * Fly * (3.0f - 2.0f * Fly);
		const float Radius = 152.0f * (1.0f - Closing) + 10.0f;
		const float Rotation = Elapsed * 2.0f * pi / Server()->TickSpeed() * 0.7f;
		const int VisualCount = minimum((int)Game.m_vVictoryVisuals.size(), (int)Game.m_vVictoryStarts.size());
		for(int i = 0; i < VisualCount; i++)
		{
			if(!Game.m_vVictoryVisuals[i])
				continue;
			const float Angle = Rotation + 2.0f * pi * i / maximum((int)Game.m_vVictoryVisuals.size(), 1);
			const float StarRadius = Radius * (i % 2 == 0 ? 1.0f : 0.58f);
			const vec2 OrbitPos = pLoser->GetPos() + vec2(std::cos(Angle), std::sin(Angle)) * StarRadius;
			Game.m_vVictoryVisuals[i]->SetPos(Game.m_vVictoryStarts[i] + (OrbitPos - Game.m_vVictoryStarts[i]) * SmoothFly);
		}
	}
	if(Elapsed >= GOMOKU_VICTORY_LINE_TICKS + GOMOKU_VICTORY_FLY_TICKS + GOMOKU_VICTORY_ORBIT_TICKS)
		FinishGomokuVictory(Team);
}

void CGameContext::ClearGomokuGame(int Team, int Winner, const char *pReason)
{
	if(Team <= TEAM_FLOCK || Team >= TEAM_SUPER)
		return;
	SGomokuGame &Game = m_aGomokuGames[Team];
	if(Game.m_Black < 0 || Game.m_White < 0)
		return;
	const int Black = Game.m_Black, White = Game.m_White;
	for(CGomokuPiece *pVisual : Game.m_vStones)
		if(pVisual)
			pVisual->Destroy();
	for(CGomokuPiece *pVisual : Game.m_vCountdown)
		if(pVisual)
			pVisual->Destroy();
	for(CGomokuPiece *pVisual : Game.m_vVictoryVisuals)
		if(pVisual)
			pVisual->Destroy();
	for(CGomokuPiece *pVisual : Game.m_vVictoryLines)
		if(pVisual)
			pVisual->Destroy();
	if(Game.m_pPreview)
		Game.m_pPreview->Destroy();
	Game = SGomokuGame{};
	if(m_aSavedGomokuGames[Team])
		*m_aSavedGomokuGames[Team] = SGomokuSavedGame{};
	m_pController->Teams().SetTeamLock(Team, false);
	char aBuf[256];
	if(Winner >= 0 && m_apPlayers[Winner])
		str_format(aBuf, sizeof(aBuf), "%s 获胜：%s。", Server()->ClientName(Winner), pReason);
	else
		str_format(aBuf, sizeof(aBuf), "五子棋结束：%s。", pReason);
	SendChatTeam(Team, aBuf);
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		const bool Participant = ClientId == Black || ClientId == White;
		if(!m_apPlayers[ClientId] || (!Participant && GetDDRaceTeam(ClientId) != Team))
			continue;
		m_pController->Teams().SetForceCharacterTeam(ClientId, TEAM_FLOCK);
		if(CCharacter *pChar = GetPlayerChar(ClientId))
			pChar->Die(WEAPON_GAME, WEAPON_GAME);
	}
}

void CGameContext::AbortGomokuFor(int ClientId, const char *pReason)
{
	const int Team = FindGomokuTeam(ClientId);
	if(Team >= 0)
	{
		const SGomokuGame &Game = m_aGomokuGames[Team];
		const int Winner = Game.m_Black == ClientId ? Game.m_White : Game.m_Black;
		ClearGomokuGame(Team, Winner, pReason);
	}
	if(m_aGomokuRequester[ClientId] >= 0)
		CancelGomokuRequest(ClientId, pReason);
	const int Target = FindOutgoingGomokuRequest(ClientId);
	if(Target >= 0)
		CancelGomokuRequest(Target, pReason);
}

bool CGameContext::HandleGomokuStart(int ClientId)
{
	const int Team = FindGomokuTeam(ClientId);
	if(Team < 0)
		return false;
	SGomokuGame &Game = m_aGomokuGames[Team];
	if(Game.m_Turn >= 0 || Game.m_Turn == -2)
	{
		SendChatTarget(ClientId, "五子棋已开始。");
		return true;
	}
	Game.m_ReadyMask |= ClientId == Game.m_Black ? 1 : 2;
	if(Game.m_ReadyMask != 3)
	{
		SendChatTeam(Team, "等待另一方 /ready。");
		return true;
	}
	Game.m_Turn = Game.m_Black;
	Game.m_DeadlineTick = Server()->Tick() + GOMOKU_TURN_SECONDS * Server()->TickSpeed();
	RebuildGomokuCountdown(Team);
	Game.m_pPreview = new CGomokuPiece(&m_World, Team, Game.m_Turn, vec2(), CGomokuPiece::PREVIEW_BLACK);
	UpdateGomokuPreview(Team);
	SendChatTeam(Team, "五子棋开始，黑方先手。");
	return true;
}

void CGameContext::ConChatGomokuReady(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	if(!pSelf->HandleGomokuStart(pResult->m_ClientId))
		if(!pSelf->HandleConnect4Ready(pResult->m_ClientId))
			pSelf->HandleFlightChessReady(pResult->m_ClientId);
}

bool CGameContext::TryGomokuPlace(int ClientId)
{
	const int Team = FindGomokuTeam(ClientId);
	if(Team < 0)
		return false;
	SGomokuGame &Game = m_aGomokuGames[Team];
	if(Game.m_Black < 0 || Game.m_White < 0 || (ClientId != Game.m_Black && ClientId != Game.m_White))
		return true;
	if(Game.m_Turn != ClientId)
		return true;
	int X, Y;
	if(!GomokuAimCell(ClientId, &X, &Y))
		return true;
	if(!IsGomokuBoardCell(X, Y) || HasCell(Game.m_vBlack, X, Y) || HasCell(Game.m_vWhite, X, Y))
		return true;
	std::vector<std::pair<int, int>> &vCells = ClientId == Game.m_Black ? Game.m_vBlack : Game.m_vWhite;
	vCells.emplace_back(X, Y);
	Game.m_vStones.push_back(new CGomokuPiece(&m_World, Team, -1, vec2(X * 32.0f + 16.0f, Y * 32.0f + 16.0f), ClientId == Game.m_Black ? CGomokuPiece::BLACK : CGomokuPiece::WHITE));
	for(const vec2 Dir : {vec2(1, 0), vec2(0, 1), vec2(1, 1), vec2(1, -1)})
	{
		int Before = 0, After = 0;
		while(HasCell(vCells, X - (int)Dir.x * (Before + 1), Y - (int)Dir.y * (Before + 1)))
			Before++;
		while(HasCell(vCells, X + (int)Dir.x * (After + 1), Y + (int)Dir.y * (After + 1)))
			After++;
		const int Count = Before + 1 + After;
		if(Count >= 5)
		{
			// Pick one contiguous run of five containing the final stone. The
			// visual line uses this exact run, even when the player made six+.
			const int Start = std::clamp(Before - 4, 0, Count - 5);
			std::vector<std::pair<int, int>> vWinningLine;
			for(int i = 0; i < 5; i++)
			{
				const int Offset = Start + i - Before;
				vWinningLine.emplace_back(X + (int)Dir.x * Offset, Y + (int)Dir.y * Offset);
			}
			StartGomokuVictory(Team, ClientId, vWinningLine);
			return true;
		}
	}
	Game.m_Turn = ClientId == Game.m_Black ? Game.m_White : Game.m_Black;
	Game.m_DeadlineTick = Server()->Tick() + GOMOKU_TURN_SECONDS * Server()->TickSpeed();
	RebuildGomokuCountdown(Team);
	if(Game.m_pPreview)
		Game.m_pPreview->Destroy();
	Game.m_pPreview = new CGomokuPiece(&m_World, Team, Game.m_Turn, vec2(), Game.m_Turn == Game.m_Black ? CGomokuPiece::PREVIEW_BLACK : CGomokuPiece::PREVIEW_WHITE);
	UpdateGomokuPreview(Team);
	return true;
}

void CGameContext::TickGomoku()
{
	RestoreGomokuHotReloadState();
	for(int Target = 0; Target < MAX_CLIENTS; Target++)
		if(m_aGomokuRequester[Target] >= 0 && Server()->Tick() > m_aGomokuRequestTick[Target] + GOMOKU_REQUEST_SECONDS * Server()->TickSpeed())
			CancelGomokuRequest(Target, "五子棋邀请已过期。");
	for(int Team = 1; Team < TEAM_SUPER; Team++)
	{
		SGomokuGame &Game = m_aGomokuGames[Team];
		if(Game.m_Black < 0)
			continue;
		if(Game.m_VictoryWinner >= 0)
		{
			TickGomokuVictory(Team);
			continue;
		}
		const bool BlackReady = IsGomokuReadyPlayer(Game.m_Black);
		const bool WhiteReady = IsGomokuReadyPlayer(Game.m_White);
		if(!BlackReady || !WhiteReady || m_pController->Teams().Count(Team) != 2 || !m_pController->Teams().TeamLocked(Team) || GetDDRaceTeam(Game.m_Black) != Team || GetDDRaceTeam(Game.m_White) != Team)
		{
			const int Loser = !BlackReady ? Game.m_Black : Game.m_White;
			ClearGomokuGame(Team, Loser == Game.m_Black ? Game.m_White : Game.m_Black, "对方离开或死亡");
			continue;
		}
		if(Game.m_Turn >= 0)
		{
			if(Server()->Tick() >= Game.m_DeadlineTick)
			{
				const int Winner = Game.m_Turn == Game.m_Black ? Game.m_White : Game.m_Black;
				ClearGomokuGame(Team, Winner, "对方超时");
				continue;
			}
			UpdateGomokuCountdown(Team);
			UpdateGomokuPreview(Team);
		}
	}
}

void CGameContext::ConChatGomoku(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int ClientId = pResult->m_ClientId;
	const char *pArg = pResult->NumArguments() ? pResult->GetString(0) : "";
	if(!pArg[0]) { pSelf->SendChatTarget(ClientId, "用法：/gomoku <昵称>，/gomoku accept|decline|cancel。"); return; }
	if(str_comp_nocase(pArg, "accept") == 0)
	{
		pSelf->CloseGameInviteVote(ClientId);
		const int Requester = pSelf->m_aGomokuRequester[ClientId];
		if(Requester < 0) { pSelf->SendChatTarget(ClientId, "没有待处理的五子棋邀请。"); return; }
		if(!pSelf->IsGomokuReadyPlayer(ClientId) || !pSelf->IsGomokuReadyPlayer(Requester) || pSelf->GetDDRaceTeam(ClientId) != TEAM_FLOCK || pSelf->GetDDRaceTeam(Requester) != TEAM_FLOCK || pSelf->FindConnect4Team(ClientId) >= 0 || pSelf->FindConnect4Team(Requester) >= 0 || pSelf->FindFlightChessTeam(ClientId) >= 0 || pSelf->FindFlightChessTeam(Requester) >= 0) { pSelf->CancelGomokuRequest(ClientId, "无法开始五子棋。"); return; }
		int Team = -1;
		for(int i = 1; i < TEAM_SUPER; i++) if(pSelf->m_pController->Teams().Count(i) == 0 && pSelf->m_aGomokuGames[i].m_Black < 0 && pSelf->m_aConnect4Games[i].m_Black < 0 && pSelf->m_aFlightChessGames[i].m_aPlayers[0] < 0) { Team = i; break; }
		const vec2 BlackSpawn = pSelf->GomokuSpawn(GOMOKU_BLACK_SPAWN_TILE), WhiteSpawn = pSelf->GomokuSpawn(GOMOKU_WHITE_SPAWN_TILE);
		if(Team < 0 || BlackSpawn.x < 0 || WhiteSpawn.x < 0) { pSelf->CancelGomokuRequest(ClientId, "没有空闲队伍或地图缺少 161/162 出生点。"); return; }
		pSelf->m_aGomokuRequester[ClientId] = -1;
		pSelf->m_aGomokuRequestTick[ClientId] = 0;
		SGomokuGame &Game = pSelf->m_aGomokuGames[Team];
		Game.m_Black = Requester; Game.m_White = ClientId;
		pSelf->m_pController->Teams().SetForceCharacterTeam(Requester, Team);
		pSelf->m_pController->Teams().SetForceCharacterTeam(ClientId, Team);
		pSelf->m_pController->Teams().SetTeamLock(Team, true);
		pSelf->GetPlayerChar(Requester)->SetPosition(BlackSpawn);
		pSelf->GetPlayerChar(ClientId)->SetPosition(WhiteSpawn);
		pSelf->SendChatTeam(Team, "五子棋已就绪，双方输入 /ready 开始。");
		return;
	}
	if(str_comp_nocase(pArg, "decline") == 0) { pSelf->CloseGameInviteVote(ClientId); pSelf->CancelGomokuRequest(ClientId, "五子棋邀请被拒绝。"); return; }
	if(str_comp_nocase(pArg, "cancel") == 0) { pSelf->AbortGomokuFor(ClientId, "对局取消"); return; }
	if(!pSelf->IsGomokuReadyPlayer(ClientId) || pSelf->GetDDRaceTeam(ClientId) != TEAM_FLOCK || pSelf->FindGomokuTeam(ClientId) >= 0 || pSelf->FindConnect4Team(ClientId) >= 0 || pSelf->FindFlightChessTeam(ClientId) >= 0 || pSelf->FindOutgoingGomokuRequest(ClientId) >= 0 || pSelf->FindOutgoingConnect4Request(ClientId) >= 0) { pSelf->SendChatTarget(ClientId, "你当前不能发起五子棋。"); return; }
	int Target = -1;
	for(int i = 0; i < MAX_CLIENTS; i++) if(i != ClientId && pSelf->m_apPlayers[i] && str_comp(pArg, pSelf->Server()->ClientName(i)) == 0) { Target = i; break; }
	if(Target < 0 || !pSelf->IsGomokuReadyPlayer(Target) || pSelf->GetDDRaceTeam(Target) != TEAM_FLOCK || pSelf->FindGomokuTeam(Target) >= 0 || pSelf->FindConnect4Team(Target) >= 0 || pSelf->FindFlightChessTeam(Target) >= 0 || pSelf->m_aGomokuRequester[Target] >= 0 || pSelf->m_aConnect4Requester[Target] >= 0 || pSelf->HasGameInviteVote(Target)) { pSelf->SendChatTarget(ClientId, "对方当前不能接受五子棋。"); return; }
	pSelf->m_aGomokuRequester[Target] = ClientId;
	pSelf->m_aGomokuRequestTick[Target] = pSelf->Server()->Tick();
	pSelf->ShowGameInviteVote(Target, ClientId, GAME_INVITE_VOTE_GOMOKU, pSelf->m_aGomokuRequestTick[Target] + GOMOKU_REQUEST_SECONDS * pSelf->Server()->TickSpeed());
	char aBuf[160]; str_format(aBuf, sizeof(aBuf), "%s 邀请你下五子棋：F3 同意、F4 拒绝，也可使用 /gomoku accept 或 /gomoku decline。", pSelf->Server()->ClientName(ClientId));
	pSelf->SendChatTarget(Target, aBuf); pSelf->SendChatTarget(ClientId, "五子棋邀请已发送。");
}
