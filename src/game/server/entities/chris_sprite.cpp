#include "chris_sprite.h"

#include "character.h"

#include <base/math.h>
#include <engine/server.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include <algorithm>
#include <cmath>

namespace
{
float Hash01(int Seed)
{
	unsigned int Value = (unsigned int)Seed * 0x45d9f3bu + 0x9e3779b9u;
	Value ^= Value >> 16;
	Value *= 0x45d9f3bu;
	Value ^= Value >> 16;
	return (Value & 0xffffu) / 65535.0f;
}

vec2 FromAngle(float Angle, float Radius)
{
	return vec2(std::cos(Angle) * Radius, std::sin(Angle) * Radius);
}
}

CChrisSprite::CChrisSprite(CGameWorld *pGameWorld, int Owner, vec2 Pos) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE, Pos),
	m_Owner(Owner),
	m_StartTick(Server()->Tick()),
	m_PauseTicks(0),
	m_Action(ACTION_FOLLOW),
	m_ActionEndTick(0),
	m_Mood(MOOD_HAPPY),
	m_MoodEndTick(Server()->Tick() + Server()->TickSpeed() * 4),
	m_CastEndTick(0),
	m_ScoutTarget(Pos)
{
	for(int &Id : m_aExtraIds)
		Id = Server()->SnapNewId();
	GameWorld()->InsertEntity(this);
}

CChrisSprite::~CChrisSprite()
{
	for(int Id : m_aExtraIds)
		Server()->SnapFreeId(Id);
}

void CChrisSprite::Reset()
{
	m_MarkedForDestroy = true;
}

void CChrisSprite::Wake(vec2 Pos, int Owner)
{
	const bool WasHidden = Hidden();
	const float Dist = distance(m_Pos, Pos);
	m_Owner = Owner;
	if(WasHidden || Dist < 64.0f)
	{
		m_Pos = Pos;
		m_Action = ACTION_FOLLOW;
		m_ActionEndTick = 0;
	}
	else
	{
		m_ScoutTarget = Pos;
		m_Action = ACTION_SCOUT;
		m_ActionEndTick = Server()->Tick() + std::clamp((int)(Dist / 18.0f) + Server()->TickSpeed(), Server()->TickSpeed() * 2, Server()->TickSpeed() * 6);
	}
	SetMood(MOOD_HAPPY, Server()->TickSpeed() * 4);
}

void CChrisSprite::Hide()
{
	m_Action = ACTION_HIDE;
	m_ActionEndTick = 0;
	SetMood(MOOD_SLEEPY, Server()->TickSpeed() * 2);
}

void CChrisSprite::CastPower(int StrengthTicks)
{
	m_CastEndTick = maximum(m_CastEndTick, Server()->Tick() + StrengthTicks);
}

void CChrisSprite::SetMood(int Mood, int DurationTicks)
{
	m_Mood = std::clamp(Mood, (int)MOOD_NEUTRAL, (int)MOOD_SLEEPY);
	m_MoodEndTick = Server()->Tick() + DurationTicks;
}

void CChrisSprite::StartScout(vec2 Target, int DurationTicks)
{
	m_ScoutTarget = Target;
	m_Action = ACTION_SCOUT;
	m_ActionEndTick = Server()->Tick() + DurationTicks;
	SetMood(MOOD_HAPPY, DurationTicks);
}

void CChrisSprite::StartOrbit(int DurationTicks)
{
	m_Action = ACTION_ORBIT;
	m_ActionEndTick = Server()->Tick() + DurationTicks;
	SetMood(MOOD_LOVE, DurationTicks);
}

vec2 CChrisSprite::FollowTarget()
{
	CCharacter *pChr = GameServer()->GetPlayerChar(m_Owner);
	if(!pChr)
		return m_Pos;
	const float Time = (Server()->Tick() - m_StartTick - m_PauseTicks) / (float)Server()->TickSpeed();
	return pChr->m_Pos + vec2(78.0f + std::sin(Time * 1.6f) * 18.0f, -92.0f + std::cos(Time * 2.1f) * 16.0f);
}

float CChrisSprite::EnergyScale()
{
	if(Server()->Tick() >= m_CastEndTick)
		return 1.0f;
	const float Left = (m_CastEndTick - Server()->Tick()) / (float)maximum(1, Server()->TickSpeed() * 2);
	return 0.70f + (1.0f - std::clamp(Left, 0.0f, 1.0f)) * 0.30f;
}

void CChrisSprite::Tick()
{
	if(Hidden())
		return;
	if(m_Mood != MOOD_NEUTRAL && Server()->Tick() > m_MoodEndTick)
		m_Mood = MOOD_NEUTRAL;
	if((m_Action == ACTION_SCOUT || m_Action == ACTION_ORBIT) && Server()->Tick() > m_ActionEndTick)
		m_Action = ACTION_FOLLOW;

	vec2 Target = FollowTarget();
	if(m_Action == ACTION_SCOUT)
		Target = m_ScoutTarget;
	else if(m_Action == ACTION_ORBIT)
	{
		if(CCharacter *pChr = GameServer()->GetPlayerChar(m_Owner))
		{
			const float Time = (Server()->Tick() - m_StartTick - m_PauseTicks) / (float)Server()->TickSpeed();
			Target = pChr->m_Pos + FromAngle(Time * 2.8f, 112.0f) + vec2(0.0f, -72.0f);
		}
	}

	const vec2 Delta = Target - m_Pos;
	const float Length = length(Delta);
	const float Speed = m_Action == ACTION_SCOUT ? 18.0f : 11.0f;
	if(Length > Speed)
		m_Pos += normalize(Delta) * Speed;
	else
		m_Pos = Target;
}

void CChrisSprite::TickPaused()
{
	++m_StartTick;
	++m_PauseTicks;
	if(m_ActionEndTick > 0)
		++m_ActionEndTick;
	if(m_MoodEndTick > 0)
		++m_MoodEndTick;
	if(m_CastEndTick > 0)
		++m_CastEndTick;
}

vec2 CChrisSprite::DotOffset(int Index, float Time)
{
	const float Radius = 21.0f * EnergyScale();
	if(Index == 0)
		return vec2(std::sin(Time * 2.0f) * 1.8f, std::cos(Time * 1.7f) * 1.4f);
	const float Angle = Index * 2.3999632f + Time * (0.22f + Hash01(Index + 7) * 0.18f);
	vec2 Offset = FromAngle(Angle, Radius * (0.55f + Hash01(Index + 31) * 0.45f));
	Offset.y *= 0.82f + Hash01(Index + 91) * 0.14f;
	Offset += FromAngle(Time * 1.1f + Index * 0.41f, std::sin(Time * 1.2f + Index) * 0.9f);
	if(m_Mood == MOOD_HAPPY && Index < 6)
		Offset += vec2((Index % 2 ? 1.0f : -1.0f) * 7.0f, -4.0f);
	else if(m_Mood == MOOD_SAD && Index < 6)
		Offset += vec2(0.0f, 7.0f + Index);
	else if(m_Mood == MOOD_LOVE && Index < 8)
		Offset += FromAngle(Index / 8.0f * 2.0f * pi, 8.0f) + vec2(0.0f, -6.0f);
	else if(m_Mood == MOOD_SLEEPY && Index < 5)
		Offset += vec2(Index * 4.0f, -10.0f - Index * 2.0f);
	return Offset;
}

void CChrisSprite::Snap(int SnappingClient)
{
	if(Hidden() || NetworkClipped(SnappingClient, m_Pos))
		return;
	const float Time = (Server()->Tick() - m_StartTick - m_PauseTicks) / (float)Server()->TickSpeed();
	for(int i = 0; i < IDLE_DOTS; ++i)
	{
		const int Id = i == 0 ? GetId() : m_aExtraIds[i - 1];
		const vec2 Pos = m_Pos + DotOffset(i, Time);
		CNetObj_Projectile *pProj = Server()->SnapNewItem<CNetObj_Projectile>(Id);
		if(!pProj)
			continue;
		pProj->m_X = (int)Pos.x;
		pProj->m_Y = (int)Pos.y;
		pProj->m_VelX = 0;
		pProj->m_VelY = 0;
		pProj->m_StartTick = m_StartTick + (i % 5);
		pProj->m_Type = WEAPON_HAMMER;
	}
}
