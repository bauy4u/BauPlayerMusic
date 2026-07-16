#ifndef GAME_SERVER_ENTITIES_FLIGHT_CHESS_INDICATOR_H
#define GAME_SERVER_ENTITIES_FLIGHT_CHESS_INDICATOR_H

#include <game/server/entity.h>

class CFlightChessTurnIndicator : public CEntity
{
public:
	CFlightChessTurnIndicator(CGameWorld *pGameWorld, int Team, int TargetClientId);
	~CFlightChessTurnIndicator() override;

	void Tick() override;
	void Snap(int SnappingClient) override;
	void SetTargetClientId(int ClientId);

private:
	bool CanSee(int SnappingClient);

	int m_Team;
	int m_TargetClientId;
	vec2 m_TransitionStart{};
	int m_TransitionStartTick = 0;
	bool m_Transitioning = false;
};

#endif
