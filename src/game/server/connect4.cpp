#include "gamecontext.h"
#include "gamecontroller.h"

#include <game/server/entities/character.h>
#include <game/server/entities/gomoku_piece.h>
#include <game/server/player.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int CONNECT4_BOARD_TILE = 160;
constexpr int CONNECT4_BLACK_SPAWN_TILE = 161;
constexpr int CONNECT4_WHITE_SPAWN_TILE = 162;
constexpr int CONNECT4_COLS = 7;
constexpr int CONNECT4_ROWS = 6;
constexpr int CONNECT4_REQUEST_SECONDS = 30;
constexpr int CONNECT4_TURN_SECONDS = 30;

vec2 Connect4CellPos(int LeftGridX, int BottomGridY, int Col, int Row)
{
	return vec2((LeftGridX + Col) * 32.0f + 16.0f, (BottomGridY - Row) * 32.0f + 16.0f);
}
}

int CGameContext::FindConnect4Team(int ClientId)
{
	for(int Team = 1; Team < TEAM_SUPER; Team++)
		if(m_aConnect4Games[Team].m_Black == ClientId || m_aConnect4Games[Team].m_White == ClientId)
			return Team;
	return -1;
}

int CGameContext::FindOutgoingConnect4Request(int ClientId)
{
	for(int i = 0; i < MAX_CLIENTS; i++)
		if(m_aConnect4Requester[i] == ClientId)
			return i;
	return -1;
}

bool CGameContext::GetConnect4BoardBounds(int *pLeftGridX, int *pBottomGridY)
{
	int MinX = Collision()->GetWidth(), MaxX = -1, MinY = Collision()->GetHeight(), MaxY = -1;
	for(int Y = 0; Y < Collision()->GetHeight(); Y++)
		for(int X = 0; X < Collision()->GetWidth(); X++)
		{
			const int Index = Y * Collision()->GetWidth() + X;
			if(Collision()->GetTileIndex(Index) != CONNECT4_BOARD_TILE)
				continue;
			MinX = minimum(MinX, X);
			MaxX = maximum(MaxX, X);
			MinY = minimum(MinY, Y);
			MaxY = maximum(MaxY, Y);
		}
	if(MaxX - MinX + 1 < CONNECT4_COLS || MaxY - MinY + 1 < CONNECT4_ROWS)
		return false;
	*pLeftGridX = MinX + (MaxX - MinX + 1 - CONNECT4_COLS) / 2;
	*pBottomGridY = MaxY;
	return true;
}

void CGameContext::CancelConnect4Request(int TargetId, const char *pReason)
{
	if(TargetId < 0 || TargetId >= MAX_CLIENTS)
		return;
	const int Requester = m_aConnect4Requester[TargetId];
	if(Requester < 0)
		return;
	m_aConnect4Requester[TargetId] = -1;
	m_aConnect4RequestTick[TargetId] = 0;
	if(m_apPlayers[Requester])
		SendChatTarget(Requester, pReason);
	if(m_apPlayers[TargetId])
		SendChatTarget(TargetId, pReason);
}

void CGameContext::CreateConnect4Borders(int Team)
{
	SConnect4Game &Game = m_aConnect4Games[Team];
	for(CGomokuPiece *pBorder : Game.m_vBorders)
		if(pBorder)
			pBorder->Destroy();
	Game.m_vBorders.clear();
	const float Left = Game.m_LeftGridX * 32.0f;
	const float Right = (Game.m_LeftGridX + CONNECT4_COLS) * 32.0f;
	const float Top = (Game.m_BottomGridY - CONNECT4_ROWS + 1) * 32.0f;
	const float Bottom = (Game.m_BottomGridY + 1) * 32.0f;
	for(const auto &Line : {std::pair<vec2, vec2>{vec2(Left, Top), vec2(Right, Top)}, {vec2(Right, Top), vec2(Right, Bottom)}, {vec2(Right, Bottom), vec2(Left, Bottom)}, {vec2(Left, Bottom), vec2(Left, Top)}})
	{
		CGomokuPiece *pBorder = new CGomokuPiece(&m_World, Team, -1, Line.second, CGomokuPiece::CONNECT4_BORDER);
		pBorder->SetLaser(Line.first, Line.second);
		Game.m_vBorders.push_back(pBorder);
	}
}

void CGameContext::RebuildConnect4Countdown(int Team)
{
	SConnect4Game &Game = m_aConnect4Games[Team];
	for(CGomokuPiece *pVisual : Game.m_vCountdown)
		if(pVisual)
			pVisual->Destroy();
	Game.m_vCountdown.clear();
	if(Game.m_Turn < 0)
		return;
	const vec2 Center = GomokuSpawn(Game.m_Turn == Game.m_Black ? CONNECT4_BLACK_SPAWN_TILE : CONNECT4_WHITE_SPAWN_TILE);
	const int Remaining = std::clamp((Game.m_DeadlineTick - Server()->Tick() + Server()->TickSpeed() - 1) / Server()->TickSpeed(), 0, CONNECT4_TURN_SECONDS);
	for(int i = 0; i < CONNECT4_TURN_SECONDS; i++)
	{
		const float Angle = 2.0f * pi * i / CONNECT4_TURN_SECONDS - pi / 2.0f;
		CGomokuPiece *pDot = new CGomokuPiece(&m_World, Team, -1, Center + vec2(std::cos(Angle), std::sin(Angle)) * 42.0f, CGomokuPiece::COUNTDOWN);
		pDot->SetVisible(i < Remaining);
		Game.m_vCountdown.push_back(pDot);
	}
}

void CGameContext::UpdateConnect4Countdown(int Team)
{
	if(Team <= TEAM_FLOCK || Team >= TEAM_SUPER)
		return;
	SConnect4Game &Game = m_aConnect4Games[Team];
	const int Remaining = std::clamp((Game.m_DeadlineTick - Server()->Tick() + Server()->TickSpeed() - 1) / Server()->TickSpeed(), 0, CONNECT4_TURN_SECONDS);
	for(int i = 0; i < (int)Game.m_vCountdown.size(); i++)
		if(Game.m_vCountdown[i])
			Game.m_vCountdown[i]->SetVisible(i < Remaining);
}

bool CGameContext::Connect4AimTarget(int ClientId, vec2 *pTarget)
{
	CCharacter *pChar = GetPlayerChar(ClientId);
	CPlayer *pPlayer = ClientId >= 0 && ClientId < MAX_CLIENTS ? m_apPlayers[ClientId] : nullptr;
	if(!pChar || !pPlayer)
		return false;
	*pTarget = pPlayer->m_CameraInfo.ConvertTargetToWorld(pChar->GetPos(), pChar->AimTarget());
	return true;
}

void CGameContext::UpdateConnect4Preview(int Team)
{
	SConnect4Game &Game = m_aConnect4Games[Team];
	if(!Game.m_pPreview || Game.m_Turn < 0)
		return;
	vec2 Target;
	if(!Connect4AimTarget(Game.m_Turn, &Target))
		return;
	const int GridX = (int)std::floor(Target.x / 32.0f);
	const int Col = GridX - Game.m_LeftGridX;
	if(Col < 0 || Col >= CONNECT4_COLS)
	{
		Game.m_pPreview->SetVisible(false);
		return;
	}
	int Row = 0;
	while(Row < CONNECT4_ROWS && Game.m_aBoard[Row][Col] != 0)
		Row++;
	const bool Valid = Row < CONNECT4_ROWS;
	if(Valid)
		Game.m_pPreview->SetPos(Target);
	Game.m_pPreview->SetVisible(Valid);
}

void CGameContext::ClearConnect4Game(int Team, int Winner, const char *pReason)
{
	if(Team <= TEAM_FLOCK || Team >= TEAM_SUPER)
		return;
	SConnect4Game &Game = m_aConnect4Games[Team];
	if(Game.m_Black < 0 || Game.m_White < 0)
		return;
	const int Black = Game.m_Black, White = Game.m_White;
	for(CGomokuPiece *pVisual : Game.m_vStones)
		if(pVisual)
			pVisual->Destroy();
	for(CGomokuPiece *pVisual : Game.m_vCountdown)
		if(pVisual)
			pVisual->Destroy();
	for(CGomokuPiece *pVisual : Game.m_vBorders)
		if(pVisual)
			pVisual->Destroy();
	if(Game.m_pPreview)
		Game.m_pPreview->Destroy();
	if(Game.m_pFallingStone)
		Game.m_pFallingStone->Destroy();
	Game = SConnect4Game{};
	if(m_aSavedConnect4Games[Team])
		*m_aSavedConnect4Games[Team] = SConnect4SavedGame{};
	m_pController->Teams().SetTeamLock(Team, false);
	char aBuf[128];
	if(Winner >= 0 && m_apPlayers[Winner])
		str_format(aBuf, sizeof(aBuf), "%s 获胜。", Server()->ClientName(Winner));
	else
		str_format(aBuf, sizeof(aBuf), "四子棋结束：%s。", pReason);
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

void CGameContext::AbortConnect4For(int ClientId, const char *pReason)
{
	const int Team = FindConnect4Team(ClientId);
	if(Team >= 0)
	{
		const SConnect4Game &Game = m_aConnect4Games[Team];
		ClearConnect4Game(Team, Game.m_Black == ClientId ? Game.m_White : Game.m_Black, pReason);
	}
	if(m_aConnect4Requester[ClientId] >= 0)
		CancelConnect4Request(ClientId, pReason);
	const int Target = FindOutgoingConnect4Request(ClientId);
	if(Target >= 0)
		CancelConnect4Request(Target, pReason);
}

bool CGameContext::HandleConnect4Ready(int ClientId)
{
	const int Team = FindConnect4Team(ClientId);
	if(Team < 0)
		return false;
	SConnect4Game &Game = m_aConnect4Games[Team];
	if(Game.m_Turn >= 0 || Game.m_Turn == -2)
		return true;
	Game.m_ReadyMask |= ClientId == Game.m_Black ? 1 : 2;
	if(Game.m_ReadyMask != 3)
	{
		SendChatTeam(Team, "等待另一方 /ready。");
		return true;
	}
	Game.m_Turn = Game.m_Black;
	Game.m_DeadlineTick = Server()->Tick() + CONNECT4_TURN_SECONDS * Server()->TickSpeed();
	CreateConnect4Borders(Team);
	RebuildConnect4Countdown(Team);
	Game.m_pPreview = new CGomokuPiece(&m_World, Team, Game.m_Turn, vec2(), CGomokuPiece::PREVIEW_BLACK);
	UpdateConnect4Preview(Team);
	SendChatTeam(Team, "四子棋开始，黑方先手。");
	return true;
}

bool CGameContext::TryConnect4Place(int ClientId)
{
	const int Team = FindConnect4Team(ClientId);
	if(Team < 0)
		return false;
	SConnect4Game &Game = m_aConnect4Games[Team];
	if(Game.m_Black < 0 || Game.m_White < 0 || (ClientId != Game.m_Black && ClientId != Game.m_White) || Game.m_pFallingStone)
		return true;
	if(Game.m_Turn != ClientId)
		return true;
	vec2 AimTarget;
	if(!Connect4AimTarget(ClientId, &AimTarget))
		return true;
	const int GridX = (int)std::floor(AimTarget.x / 32.0f);
	const int Col = GridX - Game.m_LeftGridX;
	if(Col < 0 || Col >= CONNECT4_COLS)
		return true;
	int Row = 0;
	while(Row < CONNECT4_ROWS && Game.m_aBoard[Row][Col] != 0)
		Row++;
	if(Row >= CONNECT4_ROWS)
		return true;
	const int Color = ClientId == Game.m_Black ? 1 : 2;
	Game.m_aBoard[Row][Col] = Color;
	if(Game.m_pPreview)
		Game.m_pPreview->Destroy();
	Game.m_pPreview = nullptr;
	Game.m_FallingTarget = Connect4CellPos(Game.m_LeftGridX, Game.m_BottomGridY, Col, Row);
	Game.m_FallingPos = AimTarget;
	// A stone can only fall down; aiming below its landing cell starts it at the
	// landing height while keeping the preview itself faithful to the crosshair.
	Game.m_FallingPos.y = minimum(Game.m_FallingPos.y, Game.m_FallingTarget.y);
	Game.m_FallingVelocity = 0.0f;
	Game.m_FallingCol = Col;
	Game.m_FallingRow = Row;
	Game.m_FallingColor = Color;
	Game.m_pFallingStone = new CGomokuPiece(&m_World, Team, -1, Game.m_FallingPos, Color == 1 ? CGomokuPiece::BLACK : CGomokuPiece::WHITE);
	Game.m_Turn = -2;
	return true;
}

void CGameContext::FinishConnect4Drop(int Team)
{
	if(Team <= TEAM_FLOCK || Team >= TEAM_SUPER)
		return;
	SConnect4Game &Game = m_aConnect4Games[Team];
	CGomokuPiece *pStone = Game.m_pFallingStone;
	const int Col = Game.m_FallingCol;
	const int Row = Game.m_FallingRow;
	const int Color = Game.m_FallingColor;
	const int ClientId = Color == 1 ? Game.m_Black : Game.m_White;
	if(!pStone || Col < 0 || Col >= CONNECT4_COLS || Row < 0 || Row >= CONNECT4_ROWS || (Color != 1 && Color != 2) || Game.m_aBoard[Row][Col] != Color || ClientId < 0 || ClientId >= MAX_CLIENTS || !m_apPlayers[ClientId])
	{
		ClearConnect4Game(Team, -1, "落子状态异常");
		return;
	}
	pStone->SetPos(Game.m_FallingTarget);
	Game.m_vStones.push_back(pStone);
	Game.m_pFallingStone = nullptr;
	for(const auto &Dir : {std::pair<int, int>{1, 0}, {0, 1}, {1, 1}, {1, -1}})
	{
		int Count = 1;
		for(const int Sign : {-1, 1})
			for(int Step = 1;; Step++)
			{
				const int TestCol = Col + Dir.first * Step * Sign;
				const int TestRow = Row + Dir.second * Step * Sign;
				if(TestCol < 0 || TestCol >= CONNECT4_COLS || TestRow < 0 || TestRow >= CONNECT4_ROWS || Game.m_aBoard[TestRow][TestCol] != Color)
					break;
				Count++;
			}
		if(Count >= 4)
		{
			ClearConnect4Game(Team, ClientId, "四连");
			return;
		}
	}
	bool Full = true;
	for(int TestCol = 0; TestCol < CONNECT4_COLS; TestCol++)
		Full = Full && Game.m_aBoard[CONNECT4_ROWS - 1][TestCol] != 0;
	if(Full)
	{
		ClearConnect4Game(Team, -1, "平局");
		return;
	}
	Game.m_Turn = ClientId == Game.m_Black ? Game.m_White : Game.m_Black;
	Game.m_DeadlineTick = Server()->Tick() + CONNECT4_TURN_SECONDS * Server()->TickSpeed();
	RebuildConnect4Countdown(Team);
	Game.m_pPreview = new CGomokuPiece(&m_World, Team, Game.m_Turn, vec2(), Game.m_Turn == Game.m_Black ? CGomokuPiece::PREVIEW_BLACK : CGomokuPiece::PREVIEW_WHITE);
	UpdateConnect4Preview(Team);
}

void CGameContext::SaveConnect4HotReloadState()
{
	for(int Team = 1; Team < TEAM_SUPER; Team++)
	{
		SConnect4SavedGame &Saved = *m_aSavedConnect4Games[Team];
		Saved = SConnect4SavedGame{};
		const SConnect4Game &Game = m_aConnect4Games[Team];
		if(Game.m_Black < 0 || Game.m_White < 0)
			continue;
		Saved.m_Active = true;
		Saved.m_Black = Game.m_Black;
		Saved.m_White = Game.m_White;
		Saved.m_ReadyMask = Game.m_ReadyMask;
		Saved.m_Turn = Game.m_Turn;
		Saved.m_RemainingTurnTicks = Game.m_Turn >= 0 ? maximum(Game.m_DeadlineTick - Server()->Tick(), 0) : 0;
		Saved.m_LeftGridX = Game.m_LeftGridX;
		Saved.m_BottomGridY = Game.m_BottomGridY;
		std::copy(&Game.m_aBoard[0][0], &Game.m_aBoard[0][0] + CONNECT4_ROWS * CONNECT4_COLS, &Saved.m_aBoard[0][0]);
		Saved.m_StoneFalling = Game.m_pFallingStone != nullptr;
		Saved.m_FallingPos = Game.m_FallingPos;
		Saved.m_FallingVelocity = Game.m_FallingVelocity;
		Saved.m_FallingCol = Game.m_FallingCol;
		Saved.m_FallingRow = Game.m_FallingRow;
		Saved.m_FallingColor = Game.m_FallingColor;
	}
}

void CGameContext::RestoreConnect4HotReloadState()
{
	for(int Team = 1; Team < TEAM_SUPER; Team++)
	{
		SConnect4SavedGame &Saved = *m_aSavedConnect4Games[Team];
		if(!Saved.m_Active || !GetPlayerChar(Saved.m_Black) || !GetPlayerChar(Saved.m_White))
			continue;
		SConnect4Game &Game = m_aConnect4Games[Team];
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
		Game.m_LeftGridX = Saved.m_LeftGridX;
		Game.m_BottomGridY = Saved.m_BottomGridY;
		std::copy(&Saved.m_aBoard[0][0], &Saved.m_aBoard[0][0] + CONNECT4_ROWS * CONNECT4_COLS, &Game.m_aBoard[0][0]);
		for(int Row = 0; Row < CONNECT4_ROWS; Row++)
			for(int Col = 0; Col < CONNECT4_COLS; Col++)
				if(Game.m_aBoard[Row][Col] && !(Saved.m_StoneFalling && Col == Saved.m_FallingCol && Row == Saved.m_FallingRow))
					Game.m_vStones.push_back(new CGomokuPiece(&m_World, Team, -1, Connect4CellPos(Game.m_LeftGridX, Game.m_BottomGridY, Col, Row), Game.m_aBoard[Row][Col] == 1 ? CGomokuPiece::BLACK : CGomokuPiece::WHITE));
		if(Saved.m_StoneFalling)
		{
			Game.m_FallingPos = Saved.m_FallingPos;
			Game.m_FallingVelocity = Saved.m_FallingVelocity;
			Game.m_FallingCol = Saved.m_FallingCol;
			Game.m_FallingRow = Saved.m_FallingRow;
			Game.m_FallingColor = Saved.m_FallingColor;
			Game.m_FallingTarget = Connect4CellPos(Game.m_LeftGridX, Game.m_BottomGridY, Game.m_FallingCol, Game.m_FallingRow);
			Game.m_pFallingStone = new CGomokuPiece(&m_World, Team, -1, Game.m_FallingPos, Game.m_FallingColor == 1 ? CGomokuPiece::BLACK : CGomokuPiece::WHITE);
		}
		if(Game.m_Turn >= 0)
		{
			CreateConnect4Borders(Team);
			RebuildConnect4Countdown(Team);
			Game.m_pPreview = new CGomokuPiece(&m_World, Team, Game.m_Turn, vec2(), Game.m_Turn == Game.m_Black ? CGomokuPiece::PREVIEW_BLACK : CGomokuPiece::PREVIEW_WHITE);
			UpdateConnect4Preview(Team);
		}
		Saved.m_Active = false;
	}
}

void CGameContext::TickConnect4()
{
	RestoreConnect4HotReloadState();
	for(int Target = 0; Target < MAX_CLIENTS; Target++)
		if(m_aConnect4Requester[Target] >= 0 && Server()->Tick() > m_aConnect4RequestTick[Target] + CONNECT4_REQUEST_SECONDS * Server()->TickSpeed())
			CancelConnect4Request(Target, "四子棋邀请已过期。");
	for(int Team = 1; Team < TEAM_SUPER; Team++)
	{
		SConnect4Game &Game = m_aConnect4Games[Team];
		if(Game.m_Black < 0)
			continue;
		const bool BlackReady = IsGomokuReadyPlayer(Game.m_Black);
		const bool WhiteReady = IsGomokuReadyPlayer(Game.m_White);
		if(!BlackReady || !WhiteReady || m_pController->Teams().Count(Team) != 2 || !m_pController->Teams().TeamLocked(Team) || GetDDRaceTeam(Game.m_Black) != Team || GetDDRaceTeam(Game.m_White) != Team)
		{
			const int Loser = !BlackReady ? Game.m_Black : Game.m_White;
			ClearConnect4Game(Team, Loser == Game.m_Black ? Game.m_White : Game.m_Black, "对方离开或死亡");
			continue;
		}
		if(Game.m_pFallingStone)
		{
			Game.m_FallingVelocity += 0.85f;
			Game.m_FallingPos.x += (Game.m_FallingTarget.x - Game.m_FallingPos.x) * 0.16f;
			Game.m_FallingPos.y = minimum(Game.m_FallingPos.y + Game.m_FallingVelocity, Game.m_FallingTarget.y);
			Game.m_pFallingStone->SetPos(Game.m_FallingPos);
			if(Game.m_FallingPos.y >= Game.m_FallingTarget.y)
				FinishConnect4Drop(Team);
			continue;
		}
		if(Game.m_Turn >= 0)
		{
			if(Server()->Tick() >= Game.m_DeadlineTick)
			{
				ClearConnect4Game(Team, Game.m_Turn == Game.m_Black ? Game.m_White : Game.m_Black, "超时");
				continue;
			}
			UpdateConnect4Countdown(Team);
			UpdateConnect4Preview(Team);
		}
	}
}

void CGameContext::ConChatConnect4(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int ClientId = pResult->m_ClientId;
	const char *pArg = pResult->NumArguments() ? pResult->GetString(0) : "";
	if(!pArg[0]) { pSelf->SendChatTarget(ClientId, "用法：/connect4 <昵称>，/connect4 accept|decline|cancel。"); return; }
	if(str_comp_nocase(pArg, "accept") == 0)
	{
		pSelf->CloseGameInviteVote(ClientId);
		const int Requester = pSelf->m_aConnect4Requester[ClientId];
		if(Requester < 0) { pSelf->SendChatTarget(ClientId, "没有待处理的四子棋邀请。"); return; }
		if(!pSelf->IsGomokuReadyPlayer(ClientId) || !pSelf->IsGomokuReadyPlayer(Requester) || pSelf->GetDDRaceTeam(ClientId) != TEAM_FLOCK || pSelf->GetDDRaceTeam(Requester) != TEAM_FLOCK || pSelf->FindFlightChessTeam(ClientId) >= 0 || pSelf->FindFlightChessTeam(Requester) >= 0) { pSelf->CancelConnect4Request(ClientId, "无法开始四子棋。"); return; }
		int Team = -1;
		for(int i = 1; i < TEAM_SUPER; i++)
			if(pSelf->m_pController->Teams().Count(i) == 0 && pSelf->m_aGomokuGames[i].m_Black < 0 && pSelf->m_aConnect4Games[i].m_Black < 0 && pSelf->m_aFlightChessGames[i].m_aPlayers[0] < 0) { Team = i; break; }
		int Left, Bottom;
		const vec2 BlackSpawn = pSelf->GomokuSpawn(CONNECT4_BLACK_SPAWN_TILE), WhiteSpawn = pSelf->GomokuSpawn(CONNECT4_WHITE_SPAWN_TILE);
		if(Team < 0 || !pSelf->GetConnect4BoardBounds(&Left, &Bottom) || BlackSpawn.x < 0 || WhiteSpawn.x < 0) { pSelf->CancelConnect4Request(ClientId, "地图缺少可用棋盘、出生点或空闲队伍。"); return; }
		pSelf->m_aConnect4Requester[ClientId] = -1;
		pSelf->m_aConnect4RequestTick[ClientId] = 0;
		SConnect4Game &Game = pSelf->m_aConnect4Games[Team];
		Game.m_Black = Requester;
		Game.m_White = ClientId;
		Game.m_LeftGridX = Left;
		Game.m_BottomGridY = Bottom;
		pSelf->m_pController->Teams().SetForceCharacterTeam(Requester, Team);
		pSelf->m_pController->Teams().SetForceCharacterTeam(ClientId, Team);
		pSelf->m_pController->Teams().SetTeamLock(Team, true);
		pSelf->GetPlayerChar(Requester)->SetPosition(BlackSpawn);
		pSelf->GetPlayerChar(ClientId)->SetPosition(WhiteSpawn);
		pSelf->SendChatTeam(Team, "四子棋已就绪，双方输入 /ready 开始。");
		return;
	}
	if(str_comp_nocase(pArg, "decline") == 0) { pSelf->CloseGameInviteVote(ClientId); pSelf->CancelConnect4Request(ClientId, "四子棋邀请被拒绝。"); return; }
	if(str_comp_nocase(pArg, "cancel") == 0) { pSelf->AbortConnect4For(ClientId, "对局取消"); return; }
	if(!pSelf->IsGomokuReadyPlayer(ClientId) || pSelf->GetDDRaceTeam(ClientId) != TEAM_FLOCK || pSelf->FindGomokuTeam(ClientId) >= 0 || pSelf->FindConnect4Team(ClientId) >= 0 || pSelf->FindFlightChessTeam(ClientId) >= 0 || pSelf->FindOutgoingGomokuRequest(ClientId) >= 0 || pSelf->FindOutgoingConnect4Request(ClientId) >= 0) { pSelf->SendChatTarget(ClientId, "你当前不能发起四子棋。 "); return; }
	int Target = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
		if(i != ClientId && pSelf->m_apPlayers[i] && str_comp(pArg, pSelf->Server()->ClientName(i)) == 0) { Target = i; break; }
	if(Target < 0 || !pSelf->IsGomokuReadyPlayer(Target) || pSelf->GetDDRaceTeam(Target) != TEAM_FLOCK || pSelf->FindGomokuTeam(Target) >= 0 || pSelf->FindConnect4Team(Target) >= 0 || pSelf->FindFlightChessTeam(Target) >= 0 || pSelf->m_aGomokuRequester[Target] >= 0 || pSelf->m_aConnect4Requester[Target] >= 0 || pSelf->HasGameInviteVote(Target)) { pSelf->SendChatTarget(ClientId, "对方当前不能接受四子棋。 "); return; }
	pSelf->m_aConnect4Requester[Target] = ClientId;
	pSelf->m_aConnect4RequestTick[Target] = pSelf->Server()->Tick();
	pSelf->ShowGameInviteVote(Target, ClientId, GAME_INVITE_VOTE_CONNECT4, pSelf->m_aConnect4RequestTick[Target] + CONNECT4_REQUEST_SECONDS * pSelf->Server()->TickSpeed());
	char aBuf[160];
	str_format(aBuf, sizeof(aBuf), "%s 邀请你下四子棋：F3 同意、F4 拒绝，也可使用 /connect4 accept 或 /connect4 decline。", pSelf->Server()->ClientName(ClientId));
	pSelf->SendChatTarget(Target, aBuf);
	pSelf->SendChatTarget(ClientId, "四子棋邀请已发送。");
}
