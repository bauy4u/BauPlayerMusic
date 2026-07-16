#ifndef GAME_SERVER_ENTITIES_FLIGHT_CHESS_COLOR_BADGE_H
#define GAME_SERVER_ENTITIES_FLIGHT_CHESS_COLOR_BADGE_H

#include <game/server/entity.h>

class CFlightChessColorBadge : public CEntity
{
public:
	CFlightChessColorBadge(CGameWorld *pGameWorld, int Team, int OwnerClientId, int Color);
	~CFlightChessColorBadge() override;

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	bool CanSee(int SnappingClient);

	int m_Team;
	int m_OwnerClientId;
	int m_Color;
};

#endif
