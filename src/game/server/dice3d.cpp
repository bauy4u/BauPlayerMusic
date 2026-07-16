#include "gamecontext.h"

#include <base/system.h>
#include <game/server/entities/character.h>
#include <game/server/entities/dice_3d.h>

void CGameContext::ConDice3D(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int ClientId = pResult->GetInteger(0);
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientId])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "dice3d", "Invalid client ID");
		return;
	}
	CCharacter *pCharacter = pSelf->GetPlayerChar(ClientId);
	if(!pCharacter)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "dice3d", "The player has no active character");
		return;
	}
	new CDice3D(&pSelf->m_World, pCharacter->GetPos() + vec2(0.0f, -96.0f));
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Spawned a 3D dice above client %d", ClientId);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "dice3d", aBuf);
}

void CGameContext::ConDice3DClear(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	int Removed = 0;
	for(CEntity *pEntity = pSelf->m_World.FindFirst(CGameWorld::ENTTYPE_DICE3D); pEntity;)
	{
		CEntity *pNext = pEntity->TypeNext();
		auto *pDice = static_cast<CDice3D *>(pEntity);
		if(!pDice->IsFlightChessDice())
		{
			pDice->Destroy();
			Removed++;
		}
		pEntity = pNext;
	}
	char aBuf[96];
	str_format(aBuf, sizeof(aBuf), "Removed %d 3D dice test entities", Removed);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "dice3d", aBuf);
}

int CGameContext::HammerDice3D(int HammererClientId, vec2 HammerPos, vec2 HammererPos, CClientMask Mask)
{
	CEntity *apEntities[16];
	const int Num = m_World.FindEntities(HammerPos, 18.0f, apEntities, 16, CGameWorld::ENTTYPE_DICE3D);
	int Hits = 0;
	for(int i = 0; i < Num; i++)
	{
		auto *pDice = static_cast<CDice3D *>(apEntities[i]);
		if(pDice->IsFlightChessDice() && !CanHammerFlightChessDice(pDice, HammererClientId))
			continue;
		vec2 Direction = pDice->GetPos() - HammererPos;
		Direction = length(Direction) > 0.001f ? normalize(Direction) : vec2(0.0f, -1.0f);
		if(!pDice->HammerHit(Direction * 7.0f + vec2(0.0f, -8.0f)))
			continue;
		if(pDice->IsFlightChessDice())
			OnFlightChessDiceThrown(pDice, HammererClientId);
		CreateHammerHit(pDice->GetPos() - Direction * 18.0f, Mask);
		Hits++;
	}
	return Hits;
}
