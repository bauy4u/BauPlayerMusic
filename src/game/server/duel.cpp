#include "gamecontext.h"

#include <game/server/entities/character.h>
#include <game/server/player.h>

#include <sstream>

namespace
{
constexpr int DUEL_REQUEST_SECONDS = 30;
constexpr int DUEL_DEFAULT_WEAPONS = (1 << WEAPON_HAMMER) | (1 << WEAPON_GUN);

bool IsDuelWeapon(int Weapon)
{
	return Weapon == WEAPON_HAMMER || Weapon == WEAPON_GUN || Weapon == WEAPON_SHOTGUN ||
		Weapon == WEAPON_GRENADE || Weapon == WEAPON_LASER;
}

bool ParseDuelWeapons(const char *pWeaponList, int *pWeaponMask)
{
	int WeaponMask = DUEL_DEFAULT_WEAPONS;
	if(pWeaponList && pWeaponList[0])
	{
		std::istringstream Stream(pWeaponList);
		std::string Token;
		while(Stream >> Token)
		{
			if(Token == "霰弹枪" || str_comp_nocase(Token.c_str(), "shotgun") == 0)
				WeaponMask |= 1 << WEAPON_SHOTGUN;
			else if(Token == "榴弹枪" || str_comp_nocase(Token.c_str(), "grenade") == 0)
				WeaponMask |= 1 << WEAPON_GRENADE;
			else if(Token == "激光枪" || str_comp_nocase(Token.c_str(), "laser") == 0)
				WeaponMask |= 1 << WEAPON_LASER;
			else
				return false;
		}
	}
	*pWeaponMask = WeaponMask;
	return true;
}

void FormatDuelWeapons(int WeaponMask, char *pBuf, int BufSize)
{
	pBuf[0] = '\0';
	const struct
	{
		int m_Weapon;
		const char *m_pName;
	} aWeapons[] = {
		{WEAPON_HAMMER, "锤子"},
		{WEAPON_GUN, "手枪"},
		{WEAPON_SHOTGUN, "霰弹枪"},
		{WEAPON_GRENADE, "榴弹枪"},
		{WEAPON_LASER, "激光枪"},
	};
	for(const auto &Weapon : aWeapons)
	{
		if(!(WeaponMask & (1 << Weapon.m_Weapon)))
			continue;
		if(pBuf[0])
			str_append(pBuf, " + ", BufSize);
		str_append(pBuf, Weapon.m_pName, BufSize);
	}
}

void PrepareDuelCharacter(CCharacter *pCharacter, int WeaponMask)
{
	if(!pCharacter)
		return;

	// A duel always starts from the same small DM loadout. The player remains
	// in the current DDRace team and at the current map position.
	pCharacter->SetHealth(10);
	pCharacter->SetArmor(0);
	pCharacter->ResetPickups();
	pCharacter->GiveWeapon(WEAPON_NINJA, true);
	pCharacter->GiveWeapon(WEAPON_HAMMER, true);
	pCharacter->GiveWeapon(WEAPON_GUN, true);
	for(int Weapon = WEAPON_HAMMER; Weapon <= WEAPON_LASER; Weapon++)
	{
		if(WeaponMask & (1 << Weapon))
			pCharacter->GiveWeapon(Weapon);
	}
	for(int Weapon = WEAPON_HAMMER; Weapon <= WEAPON_LASER; Weapon++)
	{
		if(WeaponMask & (1 << Weapon))
		{
			pCharacter->SetActiveWeapon(Weapon);
			break;
		}
	}
}

void RestoreDuelHealth(CGameContext *pGameServer, int ClientId)
{
	CCharacter *pCharacter = pGameServer->GetPlayerChar(ClientId);
	if(!pCharacter)
		return;
	pCharacter->SetHealth(10);
	pCharacter->SetArmor(0);
}

void RemoveDuelWeaponsAndKill(CGameContext *pGameServer, int ClientId)
{
	CCharacter *pCharacter = pGameServer->GetPlayerChar(ClientId);
	if(!pCharacter)
		return;
	pCharacter->GiveWeapon(WEAPON_NINJA, true);
	pCharacter->GiveWeapon(WEAPON_HAMMER, true);
	pCharacter->GiveWeapon(WEAPON_GUN, true);
	pCharacter->ResetPickups();
	pCharacter->Die(WEAPON_GAME, WEAPON_GAME);
}

}

bool CGameContext::IsDuelParticipant(int ClientId) const
{
	return ClientId >= 0 && ClientId < MAX_CLIENTS && m_aDuelOpponent[ClientId] >= 0;
}

bool CGameContext::AreDuelOpponents(int FirstClientId, int SecondClientId) const
{
	return FirstClientId >= 0 && FirstClientId < MAX_CLIENTS && SecondClientId >= 0 && SecondClientId < MAX_CLIENTS &&
		m_aDuelOpponent[FirstClientId] == SecondClientId && m_aDuelOpponent[SecondClientId] == FirstClientId;
}

bool CGameContext::IsDuelWeaponAllowed(int ClientId, int Weapon) const
{
	return !IsDuelParticipant(ClientId) || (IsDuelWeapon(Weapon) && (m_aDuelWeapons[ClientId] & (1 << Weapon)));
}

void CGameContext::QueueDuelTuningRestore(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !m_apPlayers[ClientId])
		return;

	const CCharacter *pCharacter = m_apPlayers[ClientId]->GetCharacter();
	const int TuneZone = pCharacter ? pCharacter->m_TuneZone : m_apPlayers[ClientId]->m_TuneZone;
	// Send now and once more next tick. The second packet wins if the single
	// tick that ended the duel also emitted a stale player-tuning update.
	SendTuningParams(ClientId, TuneZone);
	m_aDuelTuningRestoreTick[ClientId] = Server()->Tick() + 1;
}

bool CGameContext::IsDuelReadyPlayer(int ClientId) const
{
	return ClientId >= 0 && ClientId < MAX_CLIENTS && m_apPlayers[ClientId] &&
		Server()->ClientIngame(ClientId) && m_apPlayers[ClientId]->GetTeam() != TEAM_SPECTATORS &&
		!m_apPlayers[ClientId]->IsPaused() && m_apPlayers[ClientId]->GetCharacter();
}

int CGameContext::FindOutgoingDuelRequest(int ClientId) const
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aDuelRequester[i] == ClientId)
			return i;
	}
	return -1;
}

void CGameContext::CancelDuelRequest(int TargetId, const char *pReason)
{
	if(TargetId < 0 || TargetId >= MAX_CLIENTS)
		return;

	const int RequesterId = m_aDuelRequester[TargetId];
	if(RequesterId < 0)
		return;

	m_aDuelRequester[TargetId] = -1;
	m_aDuelRequestTick[TargetId] = 0;
	m_aDuelRequestWeapons[TargetId] = 0;

	if(pReason && pReason[0])
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "与 %s 的单挑邀请已取消：%s", Server()->ClientName(TargetId), pReason);
		if(RequesterId >= 0 && RequesterId < MAX_CLIENTS && m_apPlayers[RequesterId])
			SendChatTarget(RequesterId, aBuf);
		str_format(aBuf, sizeof(aBuf), "来自 %s 的单挑邀请已取消：%s", Server()->ClientName(RequesterId), pReason);
		if(m_apPlayers[TargetId])
			SendChatTarget(TargetId, aBuf);
	}
}

void CGameContext::EndDuel(int WinnerId, int LoserId, const char *pReason)
{
	if(WinnerId < 0 || WinnerId >= MAX_CLIENTS || LoserId < 0 || LoserId >= MAX_CLIENTS ||
		m_aDuelOpponent[WinnerId] != LoserId || m_aDuelOpponent[LoserId] != WinnerId)
		return;

	m_aDuelOpponent[WinnerId] = -1;
	m_aDuelOpponent[LoserId] = -1;
	m_aDuelWeapons[WinnerId] = 0;
	m_aDuelWeapons[LoserId] = 0;
	QueueDuelTuningRestore(WinnerId);
	QueueDuelTuningRestore(LoserId);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s %s %s，单挑结束。", Server()->ClientName(WinnerId), pReason, Server()->ClientName(LoserId));
	SendChat(-1, TEAM_ALL, aBuf);
}

void CGameContext::AbortDuelFor(int ClientId, const char *pReason)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	if(IsDuelParticipant(ClientId))
	{
		const int OtherId = m_aDuelOpponent[ClientId];
		m_aDuelOpponent[ClientId] = -1;
		m_aDuelWeapons[ClientId] = 0;
		if(OtherId >= 0 && OtherId < MAX_CLIENTS)
		{
			m_aDuelOpponent[OtherId] = -1;
			m_aDuelWeapons[OtherId] = 0;
		}

		RestoreDuelHealth(this, ClientId);
		RestoreDuelHealth(this, OtherId);
		QueueDuelTuningRestore(ClientId);
		QueueDuelTuningRestore(OtherId);

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s 与 %s 的单挑已取消：%s。", Server()->ClientName(ClientId), Server()->ClientName(OtherId), pReason);
		SendChat(-1, TEAM_ALL, aBuf);
	}

	if(m_aDuelRequester[ClientId] >= 0)
		CancelDuelRequest(ClientId, pReason);

	const int TargetId = FindOutgoingDuelRequest(ClientId);
	if(TargetId >= 0)
		CancelDuelRequest(TargetId, pReason);
}

void CGameContext::TickDuels()
{
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(m_aDuelTuningRestoreTick[ClientId] && Server()->Tick() >= m_aDuelTuningRestoreTick[ClientId])
		{
			m_aDuelTuningRestoreTick[ClientId] = 0;
			if(!IsDuelParticipant(ClientId))
			{
				CPlayer *pPlayer = m_apPlayers[ClientId];
				if(!pPlayer)
					continue;
				const CCharacter *pCharacter = pPlayer->GetCharacter();
				const int TuneZone = pCharacter ? pCharacter->m_TuneZone : pPlayer->m_TuneZone;
				// The first map-tuning packet was sent when the duel ended. Send
				// exactly one more on the next tick to win against stale packets
				// emitted during that end tick.
				SendTuningParams(ClientId, TuneZone);
			}
		}
	}

	for(int TargetId = 0; TargetId < MAX_CLIENTS; TargetId++)
	{
		if(m_aDuelRequester[TargetId] >= 0 && Server()->Tick() > m_aDuelRequestTick[TargetId] + DUEL_REQUEST_SECONDS * Server()->TickSpeed())
			CancelDuelRequest(TargetId, "邀请超时");
	}

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		const int OtherId = m_aDuelOpponent[ClientId];
		if(OtherId <= ClientId)
			continue;
		if(!IsDuelReadyPlayer(ClientId) || !IsDuelReadyPlayer(OtherId))
		{
			AbortDuelFor(ClientId, "玩家死亡、观战或暂离");
		}
	}
}

bool CGameContext::HandleDuelDamage(CCharacter *pVictim, vec2 Force, int Dmg, int From, int Weapon)
{
	if(!pVictim || !pVictim->GetPlayer())
		return false;

	const int VictimId = pVictim->GetPlayer()->GetCid();
	const bool VictimIsDueling = IsDuelParticipant(VictimId);
	const bool AttackerIsDueling = IsDuelParticipant(From);
	if(!VictimIsDueling && !AttackerIsDueling)
		return false;

	// A duelist can only affect their assigned opponent, and a duelist ignores
	// all hits from outsiders. This keeps the rest of the listening server's
	// no-damage behaviour intact even when players stand together.
	if(!VictimIsDueling || From != m_aDuelOpponent[VictimId])
		return true;

	if(Weapon == WEAPON_GUN || Weapon == WEAPON_SHOTGUN)
		Dmg = maximum(Dmg, 1);
	else if(Weapon == WEAPON_LASER)
		Dmg = maximum(Dmg, 5);

	if(Dmg > 0)
	{
		pVictim->SetEmote(EMOTE_PAIN, Server()->Tick() + 500 * Server()->TickSpeed() / 1000);
		CreateDamageInd(pVictim->GetPos(), 0.0f, Dmg, pVictim->TeamMask());
		pVictim->SetHealth(pVictim->GetHealth() - Dmg);
	}
	pVictim->AddVelocity(Force);

	if(Dmg > 0 && pVictim->GetHealth() <= 0)
	{
		EndDuel(From, VictimId, "击败了");
		// A finished duel clears its temporary loadout and removes both tees.
		// They respawn through the normal DDRace path with the normal weapons.
		RemoveDuelWeaponsAndKill(this, From);
		RemoveDuelWeaponsAndKill(this, VictimId);
	}
	return true;
}

void CGameContext::ConChatDuel(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int ClientId = pResult->m_ClientId;
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientId])
		return;

	const char *pArgument = pResult->NumArguments() > 0 ? pResult->GetString(0) : "";
	const char *pWeaponList = pResult->NumArguments() > 1 ? pResult->GetString(1) : "";
	if(!pArgument[0])
	{
		pSelf->SendChatTarget(ClientId, "用法：/duel <玩家昵称> [霰弹枪] [榴弹枪] [激光枪]；昵称含空格时加英文双引号。");
		return;
	}

	if(str_comp_nocase(pArgument, "accept") == 0)
	{
		pSelf->CloseGameInviteVote(ClientId);
		const int RequesterId = pSelf->m_aDuelRequester[ClientId];
		if(RequesterId < 0)
		{
			pSelf->SendChatTarget(ClientId, "你没有待处理的单挑邀请。");
			return;
		}
		if(!pSelf->IsDuelReadyPlayer(ClientId) || !pSelf->IsDuelReadyPlayer(RequesterId) || pSelf->IsDuelParticipant(ClientId) || pSelf->IsDuelParticipant(RequesterId))
		{
			pSelf->CancelDuelRequest(ClientId, "一方当前无法开始单挑");
			return;
		}

		const int WeaponMask = pSelf->m_aDuelRequestWeapons[ClientId];
		pSelf->m_aDuelRequester[ClientId] = -1;
		pSelf->m_aDuelRequestTick[ClientId] = 0;
		pSelf->m_aDuelRequestWeapons[ClientId] = 0;
		pSelf->m_aDuelOpponent[ClientId] = RequesterId;
		pSelf->m_aDuelOpponent[RequesterId] = ClientId;
		pSelf->m_aDuelTuningRestoreTick[ClientId] = 0;
		pSelf->m_aDuelTuningRestoreTick[RequesterId] = 0;
		pSelf->m_aDuelWeapons[ClientId] = WeaponMask;
		pSelf->m_aDuelWeapons[RequesterId] = WeaponMask;
		PrepareDuelCharacter(pSelf->GetPlayerChar(ClientId), WeaponMask);
		PrepareDuelCharacter(pSelf->GetPlayerChar(RequesterId), WeaponMask);
		pSelf->SendTuningParams(ClientId, pSelf->GetPlayerChar(ClientId)->m_TuneZone);
		pSelf->SendTuningParams(RequesterId, pSelf->GetPlayerChar(RequesterId)->m_TuneZone);

		char aBuf[256];
		char aWeapons[128];
		FormatDuelWeapons(WeaponMask, aWeapons, sizeof(aWeapons));
		str_format(aBuf, sizeof(aBuf), "%s 与 %s 开始单挑（%s）。", pSelf->Server()->ClientName(ClientId), pSelf->Server()->ClientName(RequesterId), aWeapons);
		pSelf->SendChat(-1, TEAM_ALL, aBuf);
		return;
	}

	if(str_comp_nocase(pArgument, "decline") == 0)
	{
		pSelf->CloseGameInviteVote(ClientId);
		if(pSelf->m_aDuelRequester[ClientId] < 0)
			pSelf->SendChatTarget(ClientId, "你没有待处理的单挑邀请。");
		else
			pSelf->CancelDuelRequest(ClientId, "对方拒绝");
		return;
	}

	if(str_comp_nocase(pArgument, "cancel") == 0)
	{
		if(pSelf->IsDuelParticipant(ClientId))
			pSelf->AbortDuelFor(ClientId, "玩家取消");
		else if(pSelf->m_aDuelRequester[ClientId] >= 0)
			pSelf->CancelDuelRequest(ClientId, "对方取消");
		else
		{
			const int TargetId = pSelf->FindOutgoingDuelRequest(ClientId);
			if(TargetId >= 0)
				pSelf->CancelDuelRequest(TargetId, "发起者取消");
			else
				pSelf->SendChatTarget(ClientId, "你没有进行中的单挑或待处理邀请。");
		}
		return;
	}

	int WeaponMask;
	if(!ParseDuelWeapons(pWeaponList, &WeaponMask))
	{
		pSelf->SendChatTarget(ClientId, "武器参数只支持：霰弹枪、榴弹枪、激光枪；可组合，例如 /duel 小明 激光枪 霰弹枪。");
		return;
	}

	int TargetId = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i != ClientId && pSelf->m_apPlayers[i] && pSelf->Server()->ClientIngame(i) && str_comp(pArgument, pSelf->Server()->ClientName(i)) == 0)
		{
			TargetId = i;
			break;
		}
	}
	if(TargetId < 0)
	{
		pSelf->SendChatTarget(ClientId, "找不到完全匹配的在线昵称；昵称含空格时请使用英文双引号，例如 /duel \"Alice Bob\" 激光枪。");
		return;
	}
	if(!pSelf->IsDuelReadyPlayer(ClientId) || !pSelf->IsDuelReadyPlayer(TargetId))
	{
		pSelf->SendChatTarget(ClientId, "双方都必须正在游戏中、未暂停且拥有角色，才能发起单挑。");
		return;
	}
	if(pSelf->IsDuelParticipant(ClientId) || pSelf->IsDuelParticipant(TargetId) || pSelf->FindOutgoingDuelRequest(ClientId) >= 0 || pSelf->m_aDuelRequester[TargetId] >= 0 || pSelf->HasGameInviteVote(TargetId))
	{
		pSelf->SendChatTarget(ClientId, "你或对方已有单挑/邀请在处理，请先结束或取消。");
		return;
	}

	pSelf->m_aDuelRequester[TargetId] = ClientId;
	pSelf->m_aDuelRequestTick[TargetId] = pSelf->Server()->Tick();
	pSelf->m_aDuelRequestWeapons[TargetId] = WeaponMask;
	pSelf->ShowGameInviteVote(TargetId, ClientId, GAME_INVITE_VOTE_DUEL, pSelf->m_aDuelRequestTick[TargetId] + DUEL_REQUEST_SECONDS * pSelf->Server()->TickSpeed());
	char aBuf[256];
	char aWeapons[128];
	FormatDuelWeapons(WeaponMask, aWeapons, sizeof(aWeapons));
	str_format(aBuf, sizeof(aBuf), "%s 向你发起单挑（%s）：F3 同意、F4 拒绝，也可使用 /duel accept 或 /duel decline。", pSelf->Server()->ClientName(ClientId), aWeapons);
	pSelf->SendChatTarget(TargetId, aBuf);
	str_format(aBuf, sizeof(aBuf), "已向 %s 发起单挑。", pSelf->Server()->ClientName(TargetId));
	pSelf->SendChatTarget(ClientId, aBuf);
}
