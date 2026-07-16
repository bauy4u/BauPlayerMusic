/* Static projectile-like dots used by the /chi test command. */
#ifndef GAME_SERVER_ENTITIES_CHI_DOT_H
#define GAME_SERVER_ENTITIES_CHI_DOT_H

#include <game/server/entity.h>

class CChiDot : public CEntity
{
public:
	enum
	{
		EXIT_HOLD = 0,
		EXIT_SNOW,
		EXIT_DROP,
		EXIT_SCATTER,
		NUM_EXIT_EFFECTS,
	};

	CChiDot(CGameWorld *pGameWorld, int Owner, vec2 Pos, int SnapPhase = 0, int SnapInterval = 1, int LifeSpanTicks = -1);
	CChiDot(CGameWorld *pGameWorld, int Owner, vec2 From, vec2 To, int AppearTick, int ArriveTick, int ExpireTick, int SnapPhase = 0, int SnapInterval = 1, int ExitEffect = EXIT_HOLD, int ExitStartTick = -1, int Seed = 0);

	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;

	int Owner() const { return m_Owner; }
	bool IsOwnedBy(int ClientId) const { return m_Owner == ClientId; }

private:
	vec2 CurrentPos();

	int m_Owner;
	int m_StartTick;
	int m_ExpireTick;
	int m_SnapPhase;
	int m_SnapInterval;
	vec2 m_From;
	vec2 m_To;
	int m_AppearTick;
	int m_ArriveTick;
	int m_ExitEffect;
	int m_ExitStartTick;
	int m_Seed;
	bool m_Animated;
};

#endif
