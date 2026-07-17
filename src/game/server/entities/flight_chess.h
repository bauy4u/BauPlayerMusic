#ifndef GAME_SERVER_ENTITIES_FLIGHT_CHESS_H
#define GAME_SERVER_ENTITIES_FLIGHT_CHESS_H

#include <game/server/entity.h>

class CFlightChessPiece : public CEntity
{
public:
	enum EColor
	{
		RED = 0,
		BLUE,
		BROWN,
		GREEN,
	};

	CFlightChessPiece(CGameWorld *pGameWorld, int Team, int Color, int Plane, vec2 Pos);
	~CFlightChessPiece() override;
	void Tick() override;
	void SetPos(vec2 Pos);
	void MoveTo(vec2 Pos, int Ticks, float ArcHeight = 0.0f);
	void Snap(int SnappingClient) override;

private:
	bool CanSee(int SnappingClient);
	int m_Team;
	int m_Color;
	[[maybe_unused]] int m_Plane;
	vec2 m_MoveStart{};
	vec2 m_MoveTarget{};
	int m_MoveStartTick = 0;
	int m_MoveDuration = 0;
	float m_MoveArcHeight = 0.0f;
};

#endif
