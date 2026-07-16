/* Static projectile-like dots used by the /chi test command. */
#include "chi_dot.h"

#include <base/math.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include <algorithm>
#include <cmath>

namespace
{
float ChiHash01(int Seed, int Salt)
{
	unsigned int x = (unsigned int)Seed ^ ((unsigned int)Salt * 0x9e3779b9u);
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return (x & 0xffffu) / 65535.0f;
}

float SmoothStep(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}
}

CChiDot::CChiDot(CGameWorld *pGameWorld, int Owner, vec2 Pos, int SnapPhase, int SnapInterval, int LifeSpanTicks) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE, Pos),
	m_Owner(Owner),
	m_StartTick(Server()->Tick()),
	m_ExpireTick(LifeSpanTicks > 0 ? Server()->Tick() + LifeSpanTicks : -1),
	m_SnapPhase(SnapPhase),
	m_SnapInterval(maximum(1, SnapInterval)),
	m_From(Pos),
	m_To(Pos),
	m_AppearTick(Server()->Tick()),
	m_ArriveTick(Server()->Tick()),
	m_ExitEffect(EXIT_HOLD),
	m_ExitStartTick(-1),
	m_Seed(0),
	m_Animated(false)
{
	GameWorld()->InsertEntity(this);
}

CChiDot::CChiDot(CGameWorld *pGameWorld, int Owner, vec2 From, vec2 To, int AppearTick, int ArriveTick, int ExpireTick, int SnapPhase, int SnapInterval, int ExitEffect, int ExitStartTick, int Seed) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE, To),
	m_Owner(Owner),
	m_StartTick(AppearTick),
	m_ExpireTick(ExpireTick),
	m_SnapPhase(SnapPhase),
	m_SnapInterval(maximum(1, SnapInterval)),
	m_From(From),
	m_To(To),
	m_AppearTick(AppearTick),
	m_ArriveTick(maximum(AppearTick + 1, ArriveTick)),
	m_ExitEffect(std::clamp(ExitEffect, (int)EXIT_HOLD, (int)NUM_EXIT_EFFECTS - 1)),
	m_ExitStartTick(ExitStartTick),
	m_Seed(Seed),
	m_Animated(true)
{
	GameWorld()->InsertEntity(this);
}

void CChiDot::Tick()
{
	if(m_ExpireTick >= 0 && Server()->Tick() >= m_ExpireTick)
		Destroy();
}

void CChiDot::TickPaused()
{
	++m_StartTick;
	++m_AppearTick;
	++m_ArriveTick;
	if(m_ExitStartTick >= 0)
		++m_ExitStartTick;
	if(m_ExpireTick >= 0)
		++m_ExpireTick;
}

void CChiDot::Snap(int SnappingClient)
{
	if(m_SnapInterval > 1 && Server()->Tick() % m_SnapInterval != m_SnapPhase)
		return;
	if(m_Animated && Server()->Tick() < m_AppearTick)
		return;

	const vec2 Pos = m_Animated ? CurrentPos() : m_Pos;
	if(NetworkClipped(SnappingClient, Pos))
		return;

	CNetObj_Projectile *pProj = Server()->SnapNewItem<CNetObj_Projectile>(GetId());
	if(!pProj)
		return;

	pProj->m_X = (int)Pos.x;
	pProj->m_Y = (int)Pos.y;
	pProj->m_VelX = 0;
	pProj->m_VelY = 0;
	pProj->m_StartTick = m_StartTick;
	pProj->m_Type = WEAPON_HAMMER;
}

vec2 CChiDot::CurrentPos()
{
	if(!m_Animated)
		return m_To;

	if(Server()->Tick() >= m_ArriveTick && (m_ExitStartTick < 0 || Server()->Tick() < m_ExitStartTick || m_ExpireTick <= m_ExitStartTick))
		return m_To;

	if(m_ExitStartTick >= 0 && Server()->Tick() >= m_ExitStartTick && m_ExpireTick > m_ExitStartTick)
	{
		const float Raw = std::clamp((Server()->Tick() - m_ExitStartTick) / (float)(m_ExpireTick - m_ExitStartTick), 0.0f, 1.0f);
		const float t = SmoothStep(Raw);
		if(m_ExitEffect == EXIT_SNOW)
		{
			const float Phase = ChiHash01(m_Seed, 1) * 2.0f * pi;
			const float Wave = std::sin(Raw * 5.2f + Phase) * (10.0f + ChiHash01(m_Seed, 2) * 22.0f);
			const float Drift = (ChiHash01(m_Seed, 3) - 0.5f) * 42.0f * t;
			const float Fall = (54.0f + ChiHash01(m_Seed, 4) * 72.0f) * t;
			return m_To + vec2(Wave + Drift, Fall);
		}
		if(m_ExitEffect == EXIT_DROP)
		{
			const float Side = (ChiHash01(m_Seed, 5) - 0.5f) * 28.0f * t;
			const float Fall = (22.0f + 190.0f * t * t) * Raw;
			const float Bounce = std::sin(Raw * pi * 2.0f) * 12.0f * (1.0f - Raw);
			return m_To + vec2(Side, Fall - maximum(0.0f, Bounce));
		}
		if(m_ExitEffect == EXIT_SCATTER)
		{
			const float Angle = ChiHash01(m_Seed, 6) * 2.0f * pi;
			const float Dist = (36.0f + ChiHash01(m_Seed, 7) * 106.0f) * t;
			const float Lift = std::sin(Raw * pi) * (18.0f + ChiHash01(m_Seed, 8) * 28.0f);
			return m_To + vec2(std::cos(Angle) * Dist, std::sin(Angle) * Dist - Lift);
		}
		return m_To;
	}

	const float t = std::clamp((Server()->Tick() - m_AppearTick) / (float)(m_ArriveTick - m_AppearTick), 0.0f, 1.0f);
	const float Smooth = 1.0f - (1.0f - t) * (1.0f - t);
	vec2 Pos = m_From + (m_To - m_From) * Smooth;
	const float Hop = std::sin(t * pi) * 34.0f;
	Pos.y -= Hop;
	return Pos;
}
