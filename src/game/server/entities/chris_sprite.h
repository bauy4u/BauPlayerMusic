#ifndef GAME_SERVER_ENTITIES_CHRIS_SPRITE_H
#define GAME_SERVER_ENTITIES_CHRIS_SPRITE_H

#include <game/server/entity.h>

#include <array>

class CChrisSprite : public CEntity
{
public:
	enum
	{
		MOOD_NEUTRAL = 0,
		MOOD_HAPPY,
		MOOD_SAD,
		MOOD_LOVE,
		MOOD_SLEEPY,
	};

	enum
	{
		ACTION_FOLLOW = 0,
		ACTION_SCOUT,
		ACTION_ORBIT,
		ACTION_HIDE,
	};

	static constexpr int BODY_DOTS = 500;
	static constexpr int IDLE_DOTS = 20;

	CChrisSprite(CGameWorld *pGameWorld, int Owner, vec2 Pos);
	~CChrisSprite() override;

	void Reset() override;
	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;

	bool Hidden() const { return m_Action == ACTION_HIDE; }
	void Wake(vec2 Pos, int Owner);
	void Hide();
	void CastPower(int StrengthTicks);
	void SetMood(int Mood, int DurationTicks);
	void StartScout(vec2 Target, int DurationTicks);
	void StartOrbit(int DurationTicks);

private:
	vec2 FollowTarget();
	vec2 DotOffset(int Index, float Time);
	float EnergyScale();

	int m_Owner;
	int m_StartTick;
	int m_PauseTicks;
	int m_Action;
	int m_ActionEndTick;
	int m_Mood;
	int m_MoodEndTick;
	int m_CastEndTick;
	vec2 m_ScoutTarget;
	std::array<int, BODY_DOTS - 1> m_aExtraIds;
};

#endif
