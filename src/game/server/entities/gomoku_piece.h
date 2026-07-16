#ifndef GAME_SERVER_ENTITIES_GOMOKU_PIECE_H
#define GAME_SERVER_ENTITIES_GOMOKU_PIECE_H

#include <game/server/entity.h>

class CGomokuPiece : public CEntity
{
public:
	enum EKind
	{
		BLACK,
		WHITE,
		PREVIEW_BLACK,
		PREVIEW_WHITE,
		COUNTDOWN,
		VICTORY_LINE_BLACK,
		VICTORY_LINE_WHITE,
		CONNECT4_BORDER,
		FLIGHT_CHESS_TEST,
	};

	CGomokuPiece(CGameWorld *pGameWorld, int Team, int Owner, vec2 Pos, EKind Kind);
	~CGomokuPiece() override;
	void SetPos(vec2 Pos) { m_Pos = Pos; }
	void SetLaser(vec2 From, vec2 To) { m_From = From; m_Pos = To; }
	void SetVisible(bool Visible) { m_Visible = Visible; }
	void Reset() override { Destroy(); }
	void Snap(int SnappingClient) override;

private:
	bool CanSee(int SnappingClient);
	int m_Team;
	int m_Owner;
	EKind m_Kind;
	vec2 m_From;
	bool m_Visible = true;
};

#endif
