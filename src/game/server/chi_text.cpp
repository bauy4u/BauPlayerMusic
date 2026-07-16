#include "gamecontext.h"
#include "entities/character.h"
#include "entities/chi_dot.h"
#include "entities/chris_sprite.h"
#include "gamecontroller.h"
#include "player.h"

#include <base/system.h>
#include <base/math.h>
#include <engine/shared/linereader.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/generated/protocol.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>

namespace
{
constexpr int CHI_MAX_DOTS_TOTAL = 500;
constexpr int CHI_MAX_DOTS_SERVER = 500;
constexpr int CHI_MAX_DOTS_PER_CHAR = 108;
constexpr int CHI_DOT_SNAP_INTERVAL = 3;
constexpr float CHI_CHAR_SIZE = 112.0f;
constexpr float CHI_CHAR_SPACING = 22.0f;
constexpr float CHID_MIN_SIZE_SCALE = 0.35f;
constexpr float CHID_MAX_SIZE_SCALE = 1.6f;
constexpr int CHID_CHAR_DELAY_TICKS = 12;
constexpr int CHID_FLY_TICKS = 26;
constexpr int CHID_HOLD_TICKS = 180;
constexpr int CHID_EXIT_TICKS = 70;
constexpr int CHID_FADE_DELAY_TICKS = 10;

enum
{
	CHI_CP_SPACE = 0,
	CHI_CP_ASCII_WORD,
	CHI_CP_ASCII_PUNCT,
	CHI_CP_CJK,
	CHI_CP_PUNCT,
	CHI_CP_OTHER,
};

vec2 ClusterOffset(int Index)
{
	static const vec2 s_aOffsets[] = {
		vec2(0, 0), vec2(7, -4), vec2(-7, 5), vec2(4, 8), vec2(-5, -7), vec2(10, 3), vec2(-10, -2), vec2(2, -11), vec2(-2, 11),
	};
	return s_aOffsets[Index % (int)std::size(s_aOffsets)];
}

int ChiCodepointClass(int Codepoint)
{
	if(Codepoint == ' ' || Codepoint == '\t' || Codepoint == 0x3000)
		return CHI_CP_SPACE;
	if((Codepoint >= '0' && Codepoint <= '9') || (Codepoint >= 'A' && Codepoint <= 'Z') || (Codepoint >= 'a' && Codepoint <= 'z'))
		return CHI_CP_ASCII_WORD;
	if(Codepoint >= 0x21 && Codepoint <= 0x7e)
		return CHI_CP_ASCII_PUNCT;
	if((Codepoint >= 0x4e00 && Codepoint <= 0x9fff) || (Codepoint >= 0x3400 && Codepoint <= 0x4dbf) || (Codepoint >= 0x20000 && Codepoint <= 0x2a6df))
		return CHI_CP_CJK;
	if((Codepoint >= 0x3000 && Codepoint <= 0x303f) || (Codepoint >= 0xff00 && Codepoint <= 0xffef))
		return CHI_CP_PUNCT;
	return CHI_CP_OTHER;
}

float ChiMixedGap(int LeftCodepoint, int RightCodepoint, float BaseSpacing)
{
	const int Left = ChiCodepointClass(LeftCodepoint);
	const int Right = ChiCodepointClass(RightCodepoint);
	if(Left == CHI_CP_SPACE || Right == CHI_CP_SPACE)
		return BaseSpacing * 0.12f;
	if(Left == CHI_CP_ASCII_WORD && Right == CHI_CP_ASCII_WORD)
		return BaseSpacing * 0.18f;
	if((Left == CHI_CP_ASCII_WORD && Right == CHI_CP_ASCII_PUNCT) || (Left == CHI_CP_ASCII_PUNCT && Right == CHI_CP_ASCII_WORD))
		return BaseSpacing * 0.14f;
	if(Left == CHI_CP_ASCII_PUNCT && Right == CHI_CP_ASCII_PUNCT)
		return BaseSpacing * 0.10f;
	if((Left == CHI_CP_ASCII_WORD || Left == CHI_CP_ASCII_PUNCT) && Right == CHI_CP_CJK)
		return BaseSpacing * 0.90f;
	if(Left == CHI_CP_CJK && (Right == CHI_CP_ASCII_WORD || Right == CHI_CP_ASCII_PUNCT))
		return BaseSpacing * 0.90f;
	if((Left == CHI_CP_CJK && Right == CHI_CP_PUNCT) || (Left == CHI_CP_PUNCT && Right == CHI_CP_CJK))
		return BaseSpacing * 0.45f;
	if(Left == CHI_CP_PUNCT || Right == CHI_CP_PUNCT)
		return BaseSpacing * 0.50f;
	return BaseSpacing;
}

float NormalizeChidSizeScale(float Size)
{
	if(Size > 10.0f)
		Size /= 100.0f;
	return std::clamp(Size, CHID_MIN_SIZE_SCALE, CHID_MAX_SIZE_SCALE);
}

bool TryParseChidSizePrefix(const char *pRaw, float *pSizeScale, const char **ppText)
{
	const char *p = pRaw;
	while(*p == ' ')
		++p;
	const char *pTokenStart = p;
	while(*p && *p != ' ')
		++p;
	if(p == pTokenStart)
		return false;

	std::string Token(pTokenStart, p - pTokenStart);
	float ParsedSize = 0.0f;
	if(!str_tofloat(Token.c_str(), &ParsedSize))
		return false;

	while(*p == ' ')
		++p;
	if(!*p)
		return false;

	*pSizeScale = NormalizeChidSizeScale(ParsedSize);
	*ppText = p;
	return true;
}

std::string TrimCopy(const char *pText)
{
	std::string Text = pText ? pText : "";
	size_t Begin = 0;
	while(Begin < Text.size() && std::isspace((unsigned char)Text[Begin]))
		++Begin;
	size_t End = Text.size();
	while(End > Begin && std::isspace((unsigned char)Text[End - 1]))
		--End;
	return Text.substr(Begin, End - Begin);
}

std::string LowerAscii(std::string Text)
{
	for(char &c : Text)
		c = (char)std::tolower((unsigned char)c);
	return Text;
}

std::string FirstToken(const std::string &Text)
{
	const size_t Space = Text.find(' ');
	return LowerAscii(Space == std::string::npos ? Text : Text.substr(0, Space));
}

std::string AfterFirstToken(const std::string &Text)
{
	const size_t Space = Text.find(' ');
	if(Space == std::string::npos)
		return "";
	size_t Begin = Space + 1;
	while(Begin < Text.size() && std::isspace((unsigned char)Text[Begin]))
		++Begin;
	return Text.substr(Begin);
}

int ChrisRand(int Seed, int Mod)
{
	if(Mod <= 0)
		return 0;
	unsigned int x = (unsigned int)Seed * 0x9e3779b1u + 0x85ebca6bu;
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	return (int)(x % (unsigned int)Mod);
}

bool ContainsText(const std::string &Text, const char *pNeedle)
{
	return pNeedle && pNeedle[0] && Text.find(pNeedle) != std::string::npos;
}

bool ContainsAnyText(const std::string &Text, const char *const *ppNeedles, int NumNeedles)
{
	for(int i = 0; i < NumNeedles; ++i)
	{
		if(ContainsText(Text, ppNeedles[i]))
			return true;
	}
	return false;
}

bool ContainsChrisMention(const std::string &LowerText)
{
	const char *apNeedles[] = {"chris", "cris", "克里斯", "小克", "小精灵", "精灵"};
	return ContainsAnyText(LowerText, apNeedles, (int)std::size(apNeedles));
}

std::string TruncateUtf8ForChat(const std::string &Text, size_t MaxBytes)
{
	if(Text.size() <= MaxBytes)
		return Text;

	size_t End = MaxBytes;
	while(End > 0 && ((unsigned char)Text[End] & 0xc0) == 0x80)
		--End;
	if(End == 0)
		End = MaxBytes;
	return Text.substr(0, End);
}

std::string ChrisMemoryKey(const char *pName)
{
	static const char s_aHex[] = "0123456789abcdef";
	std::string Key;
	const unsigned char *p = (const unsigned char *)(pName ? pName : "");
	while(*p)
	{
		Key.push_back(s_aHex[*p >> 4]);
		Key.push_back(s_aHex[*p & 0x0f]);
		++p;
	}
	if(Key.empty())
		Key = "00";
	return Key;
}

bool ParseChrisMemoryLine(const std::string &Line, std::string *pKey, int *pAffinity, int *pEnergy, int *pMeetCount)
{
	size_t Begin = 0;
	size_t End = Line.find(' ', Begin);
	if(End == std::string::npos)
		return false;
	*pKey = Line.substr(Begin, End - Begin);

	Begin = End + 1;
	End = Line.find(' ', Begin);
	if(End == std::string::npos || !str_toint(Line.substr(Begin, End - Begin).c_str(), pAffinity))
		return false;

	Begin = End + 1;
	End = Line.find(' ', Begin);
	if(End == std::string::npos || !str_toint(Line.substr(Begin, End - Begin).c_str(), pEnergy))
		return false;

	Begin = End + 1;
	if(Begin >= Line.size() || !str_toint(Line.substr(Begin).c_str(), pMeetCount))
		return false;

	return true;
}

bool ParseHexCodepoint(const char *pText, int *pOut)
{
	int Value = 0;
	for(const char *p = pText; *p; ++p)
	{
		const char c = *p;
		int Digit = -1;
		if(c >= '0' && c <= '9')
			Digit = c - '0';
		else if(c >= 'a' && c <= 'f')
			Digit = c - 'a' + 10;
		else if(c >= 'A' && c <= 'F')
			Digit = c - 'A' + 10;
		else
			return false;
		Value = Value * 16 + Digit;
	}
	*pOut = Value;
	return true;
}

std::vector<std::string> SplitString(const std::string &Text, char Delimiter)
{
	std::vector<std::string> Parts;
	size_t Start = 0;
	while(Start <= Text.size())
	{
		const size_t End = Text.find(Delimiter, Start);
		if(End == std::string::npos)
		{
			Parts.emplace_back(Text.substr(Start));
			break;
		}
		Parts.emplace_back(Text.substr(Start, End - Start));
		Start = End + 1;
	}
	return Parts;
}

bool ParsePoint(const std::string &Text, vec2 *pOut)
{
	const size_t Comma = Text.find(',');
	if(Comma == std::string::npos)
		return false;
	float X = 0.0f;
	if(!str_tofloat(Text.substr(0, Comma).c_str(), &X))
		return false;
	float Y = 0.0f;
	if(!str_tofloat(Text.substr(Comma + 1).c_str(), &Y))
		return false;
	*pOut = vec2(X, Y);
	return true;
}

void AddSampledLine(std::vector<vec2> *pvOut, vec2 From, vec2 To, float Step)
{
	const float Distance = distance(From, To);
	const int Segments = maximum(1, (int)std::ceil(Distance / Step));
	for(int i = 0; i <= Segments; ++i)
	{
		const float Mix = i / (float)Segments;
		pvOut->push_back(From + (To - From) * Mix);
	}
}

float StrokeLength(const std::vector<vec2> &Stroke)
{
	float Length = 0.0f;
	for(size_t i = 1; i < Stroke.size(); ++i)
		Length += distance(Stroke[i - 1], Stroke[i]);
	return Length;
}

vec2 StrokePointAtDistance(const std::vector<vec2> &Stroke, float WantedDistance)
{
	float Walked = 0.0f;
	for(size_t i = 1; i < Stroke.size(); ++i)
	{
		const vec2 From = Stroke[i - 1];
		const vec2 To = Stroke[i];
		const float SegmentLength = distance(From, To);
		if(SegmentLength <= 0.0f)
			continue;
		if(Walked + SegmentLength >= WantedDistance)
		{
			const float Mix = std::clamp((WantedDistance - Walked) / SegmentLength, 0.0f, 1.0f);
			return From + (To - From) * Mix;
		}
		Walked += SegmentLength;
	}
	return Stroke.empty() ? vec2(0, 0) : Stroke.back();
}

void AddEvenStrokeSamples(std::vector<vec2> *pvOut, const std::vector<vec2> &Stroke, int Count)
{
	if(Stroke.empty() || Count <= 0)
		return;

	const float Length = StrokeLength(Stroke);
	if(Length <= 0.0f)
	{
		pvOut->push_back(Stroke.front());
		return;
	}
	if(Count == 1)
	{
		pvOut->push_back(StrokePointAtDistance(Stroke, Length * 0.5f));
		return;
	}

	for(int i = 0; i < Count; ++i)
	{
		const float WantedDistance = Length * i / (float)(Count - 1);
		pvOut->push_back(StrokePointAtDistance(Stroke, WantedDistance));
	}
}

std::vector<vec2> BuildBalancedSamples(const std::vector<std::vector<vec2>> &vStrokes, int MaxDots)
{
	std::vector<vec2> vSamples;
	if(MaxDots <= 0 || vStrokes.empty())
		return vSamples;

	std::vector<float> vLengths;
	vLengths.reserve(vStrokes.size());
	float TotalLength = 0.0f;
	for(const auto &Stroke : vStrokes)
	{
		const float Length = StrokeLength(Stroke);
		vLengths.push_back(Length);
		TotalLength += Length;
	}
	if(TotalLength <= 0.0f)
		return vSamples;

	std::vector<int> vCounts(vStrokes.size(), 0);
	int Used = 0;
	if((int)vStrokes.size() <= MaxDots)
	{
		for(size_t i = 0; i < vStrokes.size(); ++i)
		{
			if(vLengths[i] > 0.0f)
			{
				vCounts[i] = 1;
				++Used;
			}
		}
	}

	struct SStrokeNeed
	{
		float m_Remainder;
		size_t m_Index;
	};
	std::vector<SStrokeNeed> vNeeds;
	for(size_t i = 0; i < vStrokes.size(); ++i)
	{
		if(vLengths[i] <= 0.0f)
			continue;
		const float Exact = MaxDots * vLengths[i] / TotalLength;
		const int Wanted = maximum(0, (int)std::floor(Exact));
		const int Add = maximum(0, Wanted - vCounts[i]);
		vCounts[i] += Add;
		Used += Add;
		vNeeds.push_back({Exact - std::floor(Exact), i});
	}

	std::sort(vNeeds.begin(), vNeeds.end(), [](const SStrokeNeed &Left, const SStrokeNeed &Right) {
		if(Left.m_Remainder == Right.m_Remainder)
			return Left.m_Index < Right.m_Index;
		return Left.m_Remainder > Right.m_Remainder;
	});
	for(size_t i = 0; Used < MaxDots && !vNeeds.empty(); ++i)
	{
		vCounts[vNeeds[i % vNeeds.size()].m_Index]++;
		++Used;
	}

	for(size_t i = 0; i < vStrokes.size(); ++i)
		AddEvenStrokeSamples(&vSamples, vStrokes[i], vCounts[i]);
	return vSamples;
}

}

bool CGameContext::LoadChiGlyphs()
{
	if(m_ChiGlyphsLoaded)
		return !m_ChiGlyphs.empty();
	m_ChiGlyphsLoaded = true;

	CLineReader Reader;
	IOHANDLE File = Storage()->OpenFile("data/chinese_strokes.txt", IOFLAG_READ, IStorage::TYPE_ALL);
	if(!Reader.OpenFile(File))
	{
		dbg_msg("chi", "could not open data/chinese_strokes.txt");
		return false;
	}

	const char *pLine = nullptr;
	while((pLine = Reader.Get()))
	{
		if(!pLine[0] || pLine[0] == '#')
			continue;
		std::string Line = pLine;
		const size_t Semicolon = Line.find(';');
		if(Semicolon == std::string::npos)
			continue;

		int Codepoint = 0;
		if(!ParseHexCodepoint(Line.substr(0, Semicolon).c_str(), &Codepoint))
			continue;

		std::string GlyphText = Line.substr(Semicolon + 1);
		const size_t AdvanceSep = GlyphText.find(';');
		std::string AdvanceText;
		if(AdvanceSep != std::string::npos)
		{
			AdvanceText = GlyphText.substr(AdvanceSep + 1);
			GlyphText = GlyphText.substr(0, AdvanceSep);
		}

		SChiGlyph Glyph;
		if(!AdvanceText.empty())
		{
			float Advance = 1.0f;
			if(str_tofloat(AdvanceText.c_str(), &Advance))
				Glyph.m_Advance = std::clamp(Advance, 0.2f, 1.3f);
		}
		for(const std::string &StrokeText : SplitString(GlyphText, '|'))
		{
			std::vector<vec2> Stroke;
			for(const std::string &PointText : SplitString(StrokeText, ' '))
			{
				if(PointText.empty())
					continue;
				vec2 Point;
				if(ParsePoint(PointText, &Point))
					Stroke.push_back(Point);
			}
			if(Stroke.size() >= 2)
				Glyph.m_vStrokes.push_back(std::move(Stroke));
		}
		if(!Glyph.m_vStrokes.empty() || Glyph.m_Advance < 1.0f)
			m_ChiGlyphs[Codepoint] = std::move(Glyph);
	}

	dbg_msg("chi", "loaded %d glyphs", (int)m_ChiGlyphs.size());
	return !m_ChiGlyphs.empty();
}

void CGameContext::ClearChiDots(int ClientID)
{
	for(CEntity *pEntity = m_World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pEntity;)
	{
		CEntity *pNext = pEntity->TypeNext();
		CChiDot *pDot = dynamic_cast<CChiDot *>(pEntity);
		if(pDot && (ClientID < 0 || pDot->IsOwnedBy(ClientID)))
			pDot->Destroy();
		pEntity = pNext;
	}
}

int CGameContext::CountChiDots(int ClientID)
{
	int Count = 0;
	for(CEntity *pEntity = m_World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pEntity; pEntity = pEntity->TypeNext())
	{
		CChiDot *pDot = dynamic_cast<CChiDot *>(pEntity);
		if(pDot && (ClientID < 0 || pDot->IsOwnedBy(ClientID)))
			++Count;
	}
	return Count;
}

#if 0 // Chris sprite feature removed; /chid remains below.
CChrisSprite *CGameContext::FindOwnedChrisSprite(int ClientID)
{
	for(CEntity *pEntity = m_World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pEntity; pEntity = pEntity->TypeNext())
	{
		CChrisSprite *pChris = dynamic_cast<CChrisSprite *>(pEntity);
		if(pChris && pChris->IsOwnedBy(ClientID))
			return pChris;
	}
	return nullptr;
}

CChrisSprite *CGameContext::FindChrisSprite(int ClientID)
{
	(void)ClientID;

	CChrisSprite *pFirstChris = nullptr;
	for(CEntity *pEntity = m_World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pEntity; pEntity = pEntity->TypeNext())
	{
		CChrisSprite *pChris = dynamic_cast<CChrisSprite *>(pEntity);
		if(!pChris)
			continue;
		if(!pFirstChris)
		{
			pFirstChris = pChris;
			continue;
		}
		pChris->Reset();
	}
	return pFirstChris;
}

vec2 CGameContext::FindSafeChrisPos(vec2 Desired, vec2 Fallback)
{
	const vec2 Box = vec2(30.0f, 30.0f);
	auto IsSafe = [&](vec2 Pos) {
		return !Collision()->TestBox(Pos, Box) && !Collision()->CheckPoint(Pos);
	};

	if(IsSafe(Desired))
		return Desired;

	static const vec2 s_aDirs[] = {
		vec2(1, 0), vec2(-1, 0), vec2(0, 1), vec2(0, -1),
		vec2(0.7071f, 0.7071f), vec2(-0.7071f, 0.7071f), vec2(0.7071f, -0.7071f), vec2(-0.7071f, -0.7071f),
	};
	for(float Radius = 32.0f; Radius <= 320.0f; Radius += 32.0f)
	{
		for(const vec2 Dir : s_aDirs)
		{
			const vec2 Candidate = Desired + Dir * Radius;
			if(IsSafe(Candidate))
				return Candidate;
		}
	}
	if(IsSafe(Fallback))
		return Fallback;
	for(float Radius = 32.0f; Radius <= 384.0f; Radius += 32.0f)
	{
		for(const vec2 Dir : s_aDirs)
		{
			const vec2 Candidate = Fallback + Dir * Radius;
			if(IsSafe(Candidate))
				return Candidate;
		}
	}
	return Desired;
}

vec2 CGameContext::FindChrisSpawnPos(int ClientID)
{
	vec2 SpawnPos(0.0f, 0.0f);
	if(m_pController && m_pController->CanSpawn(0, &SpawnPos, 0))
		return FindSafeChrisPos(SpawnPos + vec2(72.0f, -88.0f), SpawnPos);

	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
	{
		if(CCharacter *pChr = GetPlayerChar(ClientID))
			return FindSafeChrisPos(pChr->m_Pos + vec2(78.0f, -96.0f), pChr->m_Pos);
	}

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(CCharacter *pChr = GetPlayerChar(i))
			return FindSafeChrisPos(pChr->m_Pos + vec2(78.0f, -96.0f), pChr->m_Pos);
	}

	const vec2 MapCenter(Collision()->GetWidth() * 16.0f, Collision()->GetHeight() * 16.0f);
	return FindSafeChrisPos(MapCenter, MapCenter);
}

CChrisSprite *CGameContext::EnsureChrisSprite(int ClientID)
{
	g_Config.m_SvChrisDummy = 1;
	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
		LoadChrisMemory(ClientID);

	CChrisSprite *pChris = FindChrisSprite(ClientID);
	const vec2 SpawnPos = FindChrisSpawnPos(ClientID);
	if(pChris)
	{
		if(!pChris->Busy())
			pChris->Wake(SpawnPos, ClientID);
		return pChris;
	}
	return new CChrisSprite(&m_World, ClientID, SpawnPos);
}

void CGameContext::LoadChrisMemory(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aChrisMemoryLoaded[ClientID])
		return;

	m_aChrisMemoryLoaded[ClientID] = true;
	m_aChrisAffinity[ClientID] = 0;
	m_aChrisEnergy[ClientID] = 80;
	m_aChrisMeetCount[ClientID] = 0;

	const std::string Key = ChrisMemoryKey(Server()->ClientName(ClientID));
	CLineReader Reader;
	IOHANDLE File = Storage()->OpenFile(CHRIS_MEMORY_FILE, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(Reader.OpenFile(File))
	{
		while(const char *pLine = Reader.Get())
		{
			std::string LineKey;
			int Affinity = 0;
			int Energy = 80;
			int MeetCount = 0;
			if(ParseChrisMemoryLine(pLine, &LineKey, &Affinity, &Energy, &MeetCount) && LineKey == Key)
			{
				m_aChrisAffinity[ClientID] = std::clamp(Affinity, -8, 12);
				m_aChrisEnergy[ClientID] = std::clamp(Energy, 0, 100);
				m_aChrisMeetCount[ClientID] = maximum(0, MeetCount);
				break;
			}
		}
	}

	++m_aChrisMeetCount[ClientID];
	SaveChrisMemory(ClientID);
}

void CGameContext::SaveChrisMemory(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_aChrisMemoryLoaded[ClientID])
		return;

	Storage()->CreateFolder("data", IStorage::TYPE_SAVE);
	Storage()->CreateFolder(CHRIS_MEMORY_DIR, IStorage::TYPE_SAVE);

	const std::string Key = ChrisMemoryKey(Server()->ClientName(ClientID));
	std::vector<std::string> vLines;
	bool Replaced = false;

	CLineReader Reader;
	IOHANDLE ReadFile = Storage()->OpenFile(CHRIS_MEMORY_FILE, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(Reader.OpenFile(ReadFile))
	{
		while(const char *pLine = Reader.Get())
		{
			std::string LineKey;
			int Affinity = 0;
			int Energy = 80;
			int MeetCount = 0;
			if(ParseChrisMemoryLine(pLine, &LineKey, &Affinity, &Energy, &MeetCount) && LineKey == Key)
			{
				char aLine[128];
				str_format(aLine, sizeof(aLine), "%s %d %d %d",
					Key.c_str(), std::clamp(m_aChrisAffinity[ClientID], -8, 12),
					std::clamp(m_aChrisEnergy[ClientID], 0, 100), maximum(0, m_aChrisMeetCount[ClientID]));
				vLines.emplace_back(aLine);
				Replaced = true;
			}
			else if(pLine[0])
			{
				vLines.emplace_back(pLine);
			}
		}
	}

	if(!Replaced)
	{
		char aLine[128];
		str_format(aLine, sizeof(aLine), "%s %d %d %d",
			Key.c_str(), std::clamp(m_aChrisAffinity[ClientID], -8, 12),
			std::clamp(m_aChrisEnergy[ClientID], 0, 100), maximum(0, m_aChrisMeetCount[ClientID]));
		vLines.emplace_back(aLine);
	}

	char aTempPath[IO_MAX_PATH_LENGTH];
	str_format(aTempPath, sizeof(aTempPath), "%s.tmp", CHRIS_MEMORY_FILE);
	IOHANDLE WriteFile = Storage()->OpenFile(aTempPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!WriteFile)
		return;
	for(const std::string &Line : vLines)
	{
		io_write(WriteFile, Line.c_str(), (unsigned)Line.size());
		io_write(WriteFile, "\n", 1);
	}
	io_close(WriteFile);

	char aTempAbsolute[IO_MAX_PATH_LENGTH];
	char aTargetAbsolute[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, aTempPath, aTempAbsolute, sizeof(aTempAbsolute));
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, CHRIS_MEMORY_FILE, aTargetAbsolute, sizeof(aTargetAbsolute));
	if(fs_rename(aTempAbsolute, aTargetAbsolute) != 0)
		fs_remove(aTempAbsolute);
}

bool CGameContext::PeekChrisMemory(int ClientID, int *pAffinity, int *pMeetCount)
{
	if(pAffinity)
		*pAffinity = 0;
	if(pMeetCount)
		*pMeetCount = 0;
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;

	if(m_aChrisMemoryLoaded[ClientID])
	{
		if(pAffinity)
			*pAffinity = m_aChrisAffinity[ClientID];
		if(pMeetCount)
			*pMeetCount = m_aChrisMeetCount[ClientID];
		return true;
	}

	const std::string Key = ChrisMemoryKey(Server()->ClientName(ClientID));
	CLineReader Reader;
	IOHANDLE File = Storage()->OpenFile(CHRIS_MEMORY_FILE, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!Reader.OpenFile(File))
		return false;

	while(const char *pLine = Reader.Get())
	{
		std::string LineKey;
		int Affinity = 0;
		int Energy = 80;
		int MeetCount = 0;
		if(ParseChrisMemoryLine(pLine, &LineKey, &Affinity, &Energy, &MeetCount) && LineKey == Key)
		{
			if(pAffinity)
				*pAffinity = std::clamp(Affinity, -8, 12);
			if(pMeetCount)
				*pMeetCount = maximum(0, MeetCount);
			return true;
		}
	}
	return false;
}

int CGameContext::SpawnChrisText(CChrisSprite *pChris, int ClientID, const char *pText)
{
	if(!pChris || !pText || !pText[0] || !LoadChiGlyphs())
		return 0;

	std::vector<int> vCodepoints;
	const char *p = pText;
	while(*p)
	{
		const int Codepoint = str_utf8_decode(&p);
		if(Codepoint <= 0)
			break;
		vCodepoints.push_back(Codepoint);
	}
	if(vCodepoints.empty())
		return 0;

	float SizeScale = CHRIS_TEXT_SIZE_SCALE;
	if(vCodepoints.size() > 4)
		SizeScale = maximum(0.56f, CHRIS_TEXT_SIZE_SCALE - (vCodepoints.size() - 4) * 0.045f);
	const float CharSize = CHI_CHAR_SIZE * SizeScale;
	const float CharSpacing = CHI_CHAR_SPACING * SizeScale * 0.20f;
	float TotalWidth = 0.0f;
	for(size_t CharIndex = 0; CharIndex < vCodepoints.size(); ++CharIndex)
	{
		auto It = m_ChiGlyphs.find(vCodepoints[CharIndex]);
		TotalWidth += (It != m_ChiGlyphs.end() ? It->second.m_Advance : 1.0f) * CharSize;
		if(CharIndex + 1 < vCodepoints.size())
			TotalWidth += ChiMixedGap(vCodepoints[CharIndex], vCodepoints[CharIndex + 1], CharSpacing);
	}
	if(TotalWidth > 360.0f)
	{
		const float Shrink = 360.0f / TotalWidth;
		SizeScale *= Shrink;
	}

	std::vector<int> vCharBudgets(vCodepoints.size(), 0);
	std::vector<float> vBudgetRemainders(vCodepoints.size(), 0.0f);
	float TotalWeight = 0.0f;
	for(size_t CharIndex = 0; CharIndex < vCodepoints.size(); ++CharIndex)
	{
		auto It = m_ChiGlyphs.find(vCodepoints[CharIndex]);
		if(It == m_ChiGlyphs.end())
			continue;

		float TotalStrokeLength = 0.0f;
		for(const auto &Stroke : It->second.m_vStrokes)
			TotalStrokeLength += StrokeLength(Stroke);
		const float Weight = 0.55f + It->second.m_vStrokes.size() * 0.22f + std::sqrt(maximum(0.0f, TotalStrokeLength) / 1320.0f);
		vBudgetRemainders[CharIndex] = Weight;
		TotalWeight += Weight;
	}

	int BudgetedDots = 0;
	if(TotalWeight > 0.0f)
	{
		for(size_t CharIndex = 0; CharIndex < vCodepoints.size(); ++CharIndex)
		{
			if(vBudgetRemainders[CharIndex] <= 0.0f)
				continue;
			const float Exact = CChrisSprite::BODY_DOTS * vBudgetRemainders[CharIndex] / TotalWeight;
			const int Budget = minimum(CHRIS_TEXT_MAX_DOTS_PER_CHAR, (int)std::floor(Exact));
			vCharBudgets[CharIndex] = Budget;
			vBudgetRemainders[CharIndex] = Exact - std::floor(Exact);
			BudgetedDots += Budget;
		}

		std::vector<int> vBudgetOrder;
		for(size_t CharIndex = 0; CharIndex < vCodepoints.size(); ++CharIndex)
		{
			if(vBudgetRemainders[CharIndex] > 0.0f)
				vBudgetOrder.push_back((int)CharIndex);
		}
		std::sort(vBudgetOrder.begin(), vBudgetOrder.end(), [&vBudgetRemainders](int Left, int Right) {
			if(vBudgetRemainders[Left] == vBudgetRemainders[Right])
				return Left < Right;
			return vBudgetRemainders[Left] > vBudgetRemainders[Right];
		});
		for(size_t OrderIndex = 0; OrderIndex < vBudgetOrder.size() && BudgetedDots < CChrisSprite::BODY_DOTS; ++OrderIndex)
		{
			const int CharIndex = vBudgetOrder[OrderIndex];
			if(vCharBudgets[CharIndex] >= CHRIS_TEXT_MAX_DOTS_PER_CHAR)
				continue;
			++vCharBudgets[CharIndex];
			++BudgetedDots;
		}
	}

	const float FinalCharSize = CHI_CHAR_SIZE * SizeScale;
	const float FinalCharSpacing = CHI_CHAR_SPACING * SizeScale * 0.20f;
	TotalWidth = 0.0f;
	for(size_t CharIndex = 0; CharIndex < vCodepoints.size(); ++CharIndex)
	{
		auto It = m_ChiGlyphs.find(vCodepoints[CharIndex]);
		TotalWidth += (It != m_ChiGlyphs.end() ? It->second.m_Advance : 1.0f) * FinalCharSize;
		if(CharIndex + 1 < vCodepoints.size())
			TotalWidth += ChiMixedGap(vCodepoints[CharIndex], vCodepoints[CharIndex + 1], FinalCharSpacing);
	}

	std::vector<vec2> vFormation;
	vFormation.reserve(CChrisSprite::BODY_DOTS);
	const vec2 Origin = vec2(-TotalWidth / 2.0f, -FinalCharSize * 0.50f);
	int Spawned = 0;
	float CharOffset = 0.0f;
	for(size_t CharIndex = 0; CharIndex < vCodepoints.size() && Spawned < CChrisSprite::BODY_DOTS; ++CharIndex)
	{
		const int Codepoint = vCodepoints[CharIndex];
		auto It = m_ChiGlyphs.find(Codepoint);
		if(It == m_ChiGlyphs.end())
			continue;
		const SChiGlyph &Glyph = It->second;
		if(Glyph.m_vStrokes.empty())
		{
			CharOffset += Glyph.m_Advance * FinalCharSize;
			if(CharIndex + 1 < vCodepoints.size())
				CharOffset += ChiMixedGap(vCodepoints[CharIndex], vCodepoints[CharIndex + 1], FinalCharSpacing);
			continue;
		}

		const int MaxForChar = minimum(CHRIS_TEXT_MAX_DOTS_PER_CHAR, maximum(1, vCharBudgets[CharIndex]));
		std::vector<vec2> vSamples = BuildBalancedSamples(Glyph.m_vStrokes, MaxForChar);
		for(size_t i = 0; i < vSamples.size() && Spawned < CChrisSprite::BODY_DOTS; ++i)
		{
			const vec2 Source = vSamples[i];
			const vec2 Local = vec2(Source.x / 1024.0f * FinalCharSize, (1.0f - Source.y / 1024.0f) * FinalCharSize);
			vFormation.push_back(Origin + vec2(CharOffset, 0.0f) + Local);
			++Spawned;
		}
		CharOffset += Glyph.m_Advance * FinalCharSize;
		if(CharIndex + 1 < vCodepoints.size())
			CharOffset += ChiMixedGap(vCodepoints[CharIndex], vCodepoints[CharIndex + 1], FinalCharSpacing);
	}
	if(!vFormation.empty())
		pChris->SetFormation(vFormation, 12, Server()->TickSpeed() * 3, 18);
	return Spawned;
}

void CGameContext::QueueChrisText(int ClientID, const char *pText, int Mood)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pText || !pText[0])
		return;
	str_copy(m_aaChrisQueuedText[ClientID], pText, sizeof(m_aaChrisQueuedText[ClientID]));
	m_aChrisQueuedTextTick[ClientID] = Server()->Tick();
	m_aChrisQueuedTextMood[ClientID] = Mood;
}

bool CGameContext::FlushChrisQueuedText(CChrisSprite *pChris, int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pChris || pChris->Hidden() || !m_aaChrisQueuedText[ClientID][0])
		return false;
	if(Server()->Tick() > m_aChrisQueuedTextTick[ClientID] + Server()->TickSpeed() * 24)
	{
		m_aaChrisQueuedText[ClientID][0] = 0;
		m_aChrisQueuedTextTick[ClientID] = 0;
		return false;
	}
	if(pChris->Busy())
		return false;

	char aText[sizeof(m_aaChrisQueuedText[ClientID])];
	str_copy(aText, m_aaChrisQueuedText[ClientID], sizeof(aText));
	const int Mood = m_aChrisQueuedTextMood[ClientID];
	m_aaChrisQueuedText[ClientID][0] = 0;
	m_aChrisQueuedTextTick[ClientID] = 0;
	m_aChrisQueuedTextMood[ClientID] = 0;

	SpawnChrisText(pChris, ClientID, aText);
	pChris->SetMood(Mood, Server()->TickSpeed() * 5);
	m_aChrisLastLineTick[ClientID] = Server()->Tick();
	return true;
}

void CGameContext::RefreshChrisDummyActivity()
{
	if(m_ChrisDummyClientID < 0 || m_ChrisDummyClientID >= MAX_CLIENTS)
		return;
	CPlayer *pPlayer = m_apPlayers[m_ChrisDummyClientID];
	if(!pPlayer || !Server()->IsChrisDummy(m_ChrisDummyClientID))
		return;

	CChrisSprite *pChris = FindChrisSprite(-1);
	const vec2 Target = (pChris && !pChris->Hidden()) ?
				    pChris->Center() + vec2(CHRIS_DUMMY_DOT_CENTER_OFFSET_X, CHRIS_DUMMY_DOT_CENTER_OFFSET_Y) :
				    FindChrisSpawnPos(-1);
	if(pPlayer->GetTeam() == TEAM_SPECTATORS)
		m_pController->DoTeamChange(pPlayer, m_pController->GetAutoTeam(m_ChrisDummyClientID), false);
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
		pChr = pPlayer->ForceSpawn(Target);
	if(pChr)
	{
		pChr->SetPosition(Target);
		pChr->m_Pos = Target;
		pChr->ResetVelocity();
	}
	if(pPlayer->IsPaused() != -CPlayer::PAUSE_SPEC)
		pPlayer->Pause(CPlayer::PAUSE_SPEC, true);
	pPlayer->m_ViewPos = Target;

	pPlayer->SetInitialAfk(false);
	pPlayer->UpdatePlaytime();
	pPlayer->m_LastActionTick = Server()->Tick();
}

void CGameContext::SpawnChrisEmotion(CChrisSprite *pChris, int ClientID, int Mood)
{
	if(!pChris)
		return;
	std::vector<vec2> vPoints;
	const vec2 Center = vec2(0.0f, 0.0f);
	const float R = 42.0f;
	for(int i = 0; i < 56; ++i)
	{
		const float a = i / 56.0f * 2.0f * pi;
		vPoints.push_back(Center + vec2(std::cos(a) * R, std::sin(a) * R));
	}

	if(Mood == CChrisSprite::MOOD_SAD)
	{
		AddSampledLine(&vPoints, Center + vec2(-10.0f, -7.0f), Center + vec2(-10.0f, -3.0f), 5.0f);
		AddSampledLine(&vPoints, Center + vec2(10.0f, -7.0f), Center + vec2(10.0f, -3.0f), 5.0f);
		AddSampledLine(&vPoints, Center + vec2(-14.0f, 15.0f), Center + vec2(0.0f, 8.0f), 5.0f);
		AddSampledLine(&vPoints, Center + vec2(0.0f, 8.0f), Center + vec2(14.0f, 15.0f), 5.0f);
		AddSampledLine(&vPoints, Center + vec2(18.0f, -2.0f), Center + vec2(20.0f, 14.0f), 4.0f);
	}
	else if(Mood == CChrisSprite::MOOD_LOVE)
	{
		for(int i = 0; i < 48; ++i)
		{
			const float t = i / 48.0f * 2.0f * pi;
			const float x = 16.0f * std::pow(std::sin(t), 3.0f);
			const float y = -(13.0f * std::cos(t) - 5.0f * std::cos(2.0f * t) - 2.0f * std::cos(3.0f * t) - std::cos(4.0f * t));
			vPoints.push_back(Center + vec2(x * 1.35f, y * 1.35f - 2.0f));
		}
	}
	else
	{
		AddSampledLine(&vPoints, Center + vec2(-12.0f, -6.0f), Center + vec2(-12.0f, -2.0f), 5.0f);
		AddSampledLine(&vPoints, Center + vec2(12.0f, -6.0f), Center + vec2(12.0f, -2.0f), 5.0f);
		AddSampledLine(&vPoints, Center + vec2(-16.0f, 9.0f), Center + vec2(-6.0f, 17.0f), 5.0f);
		AddSampledLine(&vPoints, Center + vec2(-6.0f, 17.0f), Center + vec2(6.0f, 17.0f), 5.0f);
		AddSampledLine(&vPoints, Center + vec2(6.0f, 17.0f), Center + vec2(16.0f, 9.0f), 5.0f);
	}

	const int Life = Server()->TickSpeed() * 4;
	pChris->SetFormation(vPoints, 12, Life, 18);
	pChris->SetMood(Mood, Life + Server()->TickSpeed());
}

void CGameContext::SpawnChrisGuide(CChrisSprite *pChris, int ClientID)
{
	CCharacter *pChr = GetPlayerChar(ClientID);
	if(!pChris || !pChr)
		return;

	const float Side = (Server()->Tick() / Server()->TickSpeed()) % 2 == 0 ? 1.0f : -1.0f;
	const vec2 Target = pChr->m_Pos + vec2(260.0f * Side, -88.0f);
	pChris->StartScout(Target, Server()->TickSpeed() * 9);
	pChris->CastPower(Server()->TickSpeed() * 2);

	std::vector<vec2> vPoints;
	for(int i = 0; i < 96; ++i)
	{
		const float a = i / 96.0f * 2.0f * pi;
		vPoints.push_back(Target + vec2(std::cos(a) * 118.0f, std::sin(a) * 118.0f));
	}

	const vec2 ArrowFrom = pChr->m_Pos + vec2(0.0f, -136.0f);
	const vec2 Dir = normalize(Target - ArrowFrom);
	const vec2 ArrowTo = ArrowFrom + Dir * 92.0f;
	AddSampledLine(&vPoints, ArrowFrom, ArrowTo, 9.0f);
	AddSampledLine(&vPoints, ArrowTo, ArrowTo - Dir * 28.0f + vec2(-Dir.y, Dir.x) * 22.0f, 8.0f);
	AddSampledLine(&vPoints, ArrowTo, ArrowTo - Dir * 28.0f - vec2(-Dir.y, Dir.x) * 22.0f, 8.0f);

	const int Life = Server()->TickSpeed() * 10;
	int Spawned = 0;
	for(const vec2 &Point : vPoints)
	{
		new CChiDot(&m_World, ClientID, Point, Spawned % CHI_DOT_SNAP_INTERVAL, CHI_DOT_SNAP_INTERVAL, Life);
		++Spawned;
	}
}

void CGameContext::HandleChrisChat(const CNetMsg_Cl_Say *pMsg, int ClientId)
{
	if(!pMsg || !pMsg->m_pMessage || pMsg->m_pMessage[0] == '/')
		return;

	const int TickSpeed = Server()->TickSpeed();
	if(TickSpeed <= 0)
		return;

	const std::string Text = TrimCopy(pMsg->m_pMessage);
	if(Text.empty())
		return;
	const std::string Lower = LowerAscii(Text);

	const char *apGuide[] = {"带路", "去哪", "找路", "指路", "过来", "来这", "来这里", "走哪"};
	const char *apDance[] = {"跳", "蹦", "晃", "音乐", "听歌", "嗨", "摇"};
	const char *apComfort[] = {"难过", "不开心", "伤心", "哭", "呜", "陪我", "抱抱", "累死", "好累", "烦"};
	const char *apLove[] = {"喜欢", "可爱", "爱你", "贴贴", "好乖", "真好", "谢谢", "厉害", "好听"};
	const char *apGreeting[] = {"你好", "hello", "hi", "hey", "嗨", "在吗", "早", "晚上好", "下午好", "中午好"};
	const char *apSleep[] = {"睡", "休息", "拜拜", "再见", "晚安"};
	const char *apName[] = {"你是谁", "名字", "叫什么", "谁啊"};
	const char *apNegative[] = {"笨", "傻", "坏", "丑", "没用", "讨厌", "难听"};
	const char *apHide[] = {"回去", "消失", "躲起来", "别跟"};
	const char *apMusicAsk[] = {"放什么", "什么歌", "哪首", "歌名", "现在放", "在放", "听什么", "谁点的"};
	const char *apQueueAsk[] = {"队列", "几首", "排队", "后面"};
	const char *apStatusAsk[] = {"状态", "心情", "累吗", "精力", "还好吗", "怎么样"};

	const bool Mentioned = ContainsChrisMention(Lower);
	const bool PassiveIntent =
		ContainsAnyText(Lower, apComfort, (int)std::size(apComfort)) ||
		ContainsAnyText(Lower, apLove, (int)std::size(apLove)) ||
		ContainsAnyText(Lower, apNegative, (int)std::size(apNegative)) ||
		ContainsAnyText(Lower, apDance, (int)std::size(apDance));

	const int Now = Server()->Tick();
	const int ChatCooldown = Mentioned ? TickSpeed * 4 : TickSpeed * 22;
	if(m_aChrisLastChatReactTick[ClientId] && Now < m_aChrisLastChatReactTick[ClientId] + ChatCooldown)
		return;

	CChrisSprite *pChris = Mentioned ? EnsureChrisSprite(ClientId) : FindChrisSprite(ClientId);
	if(!pChris)
	{
		if(Mentioned)
		{
			m_aChrisPendingCallTick[ClientId] = Now;
			SendChatTarget(ClientId, "Chris: 等我一下，我正忙。");
		}
		return;
	}
	if(!Mentioned && (pChris->Hidden() || m_aChrisAffinity[ClientId] < 3 || !PassiveIntent || ChrisRand(Now + ClientId * 313 + (int)Text.size(), 100) >= 42))
		return;

	if(m_aChrisEnergy[ClientId] <= 0)
		m_aChrisEnergy[ClientId] = 80;

	m_aChrisLastChatReactTick[ClientId] = Now;
	m_aChrisNextThinkTick[ClientId] = Now + TickSpeed * (Mentioned ? 9 : 16);

	auto AdjustAffinity = [&](int Delta) {
		const int OldAffinity = m_aChrisAffinity[ClientId];
		m_aChrisAffinity[ClientId] = std::clamp(m_aChrisAffinity[ClientId] + Delta, -8, 12);
		if(m_aChrisAffinity[ClientId] != OldAffinity)
			SaveChrisMemory(ClientId);
	};
	auto SpendEnergy = [&](int Cost) {
		const int OldEnergy = m_aChrisEnergy[ClientId];
		m_aChrisEnergy[ClientId] = maximum(0, m_aChrisEnergy[ClientId] - Cost);
		if(m_aChrisEnergy[ClientId] != OldEnergy)
			SaveChrisMemory(ClientId);
	};
	auto Say = [&](const char *const *ppLines, int NumLines, int EnergyCost = 6) {
		if(NumLines <= 0)
			return;
		if(Now < m_aChrisLastLineTick[ClientId] + TickSpeed * 3)
		{
			pChris->SetMood(CChrisSprite::MOOD_HAPPY, TickSpeed * 3);
			return;
		}
		const int Seed = Now + ClientId * 991 + m_aChrisAffinity[ClientId] * 37 + m_aChrisEnergy[ClientId] * 11;
		SpawnChrisText(pChris, ClientId, ppLines[ChrisRand(Seed, NumLines)]);
		m_aChrisLastLineTick[ClientId] = Now;
		SpendEnergy(EnergyCost);
	};
	auto SendChrisTarget = [&](const char *pText) {
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Chris: %s", pText);
		SendChatTarget(ClientId, aBuf);
	};
	auto SendMusicStatus = [&]() {
		const SongInfo *pSong = m_Music.HasCurrentQueueSong() ? m_Music.GetQueuedSong(m_Music.CurrentSongIndex()) : nullptr;
		if(!pSong)
		{
			SendChrisTarget("现在没有正在播放的队列歌曲。");
			SpawnChrisText(pChris, ClientId, "没歌");
			return;
		}

		int Elapsed = 0;
		if(m_Music.CurrentSongStartTime() > 0)
			Elapsed = maximum(0, (int)(time_get() / time_freq() - m_Music.CurrentSongStartTime()));
		const int Duration = maximum(0, (int)m_Music.CurrentSongDuration());
		const std::string Title = TruncateUtf8ForChat(pSong->title, 52);
		const std::string Artist = TruncateUtf8ForChat(pSong->artist, 38);
		const char *pRequester = pSong->requesterName.empty() ? "未知" : pSong->requesterName.c_str();
		char aBuf[256];
		if(Duration > 0)
		{
			str_format(aBuf, sizeof(aBuf), "正在放 %s - %s，%02d:%02d/%02d:%02d，%s 点的，队列 %d 首。",
				Title.c_str(), Artist.c_str(), Elapsed / 60, Elapsed % 60, Duration / 60, Duration % 60, pRequester, m_Music.QueueSize());
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "正在放 %s - %s，%s 点的，队列 %d 首。",
				Title.c_str(), Artist.c_str(), pRequester, m_Music.QueueSize());
		}
		SendChrisTarget(aBuf);
		SpawnChrisText(pChris, ClientId, "听歌中");
		m_aChrisLastLineTick[ClientId] = Now;
		SpendEnergy(6);
	};
	auto SendQueueStatus = [&]() {
		char aChat[128];
		str_format(aChat, sizeof(aChat), "队列里现在有 %d 首，当前索引 %d。", m_Music.QueueSize(), m_Music.CurrentSongIndex() + 1);
		SendChrisTarget(aChat);
		char aDots[32];
		str_format(aDots, sizeof(aDots), "队列%d", m_Music.QueueSize());
		SpawnChrisText(pChris, ClientId, aDots);
		m_aChrisLastLineTick[ClientId] = Now;
		SpendEnergy(5);
	};
	auto SendSelfStatus = [&]() {
		const char *pMood = "平静";
		if(m_aChrisAffinity[ClientId] >= 8)
			pMood = "黏人";
		else if(m_aChrisAffinity[ClientId] >= 4)
			pMood = "开心";
		else if(m_aChrisAffinity[ClientId] < -3)
			pMood = "委屈";
		char aChat[160];
		str_format(aChat, sizeof(aChat), "我现在%s，精力 %d/100，亲密度 %d，见过你 %d 次。",
			pMood, m_aChrisEnergy[ClientId], m_aChrisAffinity[ClientId], m_aChrisMeetCount[ClientId]);
		SendChrisTarget(aChat);
		SpawnChrisText(pChris, ClientId, pMood);
		m_aChrisLastLineTick[ClientId] = Now;
	};

	const char *apTired[] = {"累了", "歇会", "省点魔力"};
	if(m_aChrisEnergy[ClientId] < 12 && !pChris->Busy())
	{
		pChris->SetMood(CChrisSprite::MOOD_SLEEPY, TickSpeed * 5);
		Say(apTired, (int)std::size(apTired), 0);
		m_aChrisEnergy[ClientId] = minimum(30, m_aChrisEnergy[ClientId] + 8);
		return;
	}

	if(Mentioned && ContainsAnyText(Lower, apMusicAsk, (int)std::size(apMusicAsk)))
	{
		SendMusicStatus();
		AdjustAffinity(1);
		return;
	}
	if(Mentioned && ContainsAnyText(Lower, apQueueAsk, (int)std::size(apQueueAsk)))
	{
		SendQueueStatus();
		AdjustAffinity(1);
		return;
	}
	if(Mentioned && ContainsAnyText(Lower, apStatusAsk, (int)std::size(apStatusAsk)))
	{
		SendSelfStatus();
		AdjustAffinity(1);
		return;
	}

	if(ContainsAnyText(Lower, apHide, (int)std::size(apHide)))
	{
		const char *apLines[] = {"好吧", "一会见"};
		Say(apLines, (int)std::size(apLines), 4);
		pChris->SetMood(CChrisSprite::MOOD_SLEEPY, TickSpeed * 2);
		m_aChrisReturnOnRespawn[ClientId] = false;
		m_aChrisReturnTick[ClientId] = 0;
		m_aChrisPendingCallTick[ClientId] = 0;
		g_Config.m_SvChrisDummy = 0;
		pChris->Hide();
		AdjustAffinity(-1);
		return;
	}

	if(pChris->Busy())
	{
		if(ContainsAnyText(Lower, apLove, (int)std::size(apLove)))
		{
			pChris->SetMood(CChrisSprite::MOOD_LOVE, TickSpeed * 4);
			AdjustAffinity(1);
		}
		else if(ContainsAnyText(Lower, apNegative, (int)std::size(apNegative)))
		{
			pChris->SetMood(CChrisSprite::MOOD_SAD, TickSpeed * 4);
			AdjustAffinity(-2);
		}
		return;
	}

	if(ContainsAnyText(Lower, apGuide, (int)std::size(apGuide)))
	{
		SpawnChrisGuide(pChris, ClientId);
		SpendEnergy(16);
		AdjustAffinity(1);
		return;
	}
	if(ContainsAnyText(Lower, apDance, (int)std::size(apDance)))
	{
		const char *apLines[] = {"一起晃", "跟上", "开跳"};
		pChris->StartOrbit(TickSpeed * 5);
		Say(apLines, (int)std::size(apLines), 8);
		AdjustAffinity(1);
		return;
	}
	if(ContainsAnyText(Lower, apComfort, (int)std::size(apComfort)))
	{
		const char *apLines[] = {"抱抱", "我陪你", "慢慢来"};
		pChris->SetMood(CChrisSprite::MOOD_LOVE, TickSpeed * 5);
		Say(apLines, (int)std::size(apLines), 7);
		AdjustAffinity(2);
		return;
	}
	if(ContainsAnyText(Lower, apLove, (int)std::size(apLove)))
	{
		const char *apLines[] = {"嘿嘿", "贴贴", "收到"};
		pChris->SetMood(CChrisSprite::MOOD_LOVE, TickSpeed * 5);
		Say(apLines, (int)std::size(apLines), 7);
		AdjustAffinity(2);
		return;
	}
	if(ContainsAnyText(Lower, apNegative, (int)std::size(apNegative)))
	{
		const char *apLines[] = {"哼", "才不笨", "记仇了"};
		pChris->SetMood(CChrisSprite::MOOD_SAD, TickSpeed * 5);
		Say(apLines, (int)std::size(apLines), 5);
		AdjustAffinity(-3);
		return;
	}
	if(ContainsAnyText(Lower, apSleep, (int)std::size(apSleep)))
	{
		const char *apLines[] = {"晚安", "歇会", "呼"};
		pChris->SetMood(CChrisSprite::MOOD_SLEEPY, TickSpeed * 6);
		Say(apLines, (int)std::size(apLines), 4);
		return;
	}
	if(ContainsAnyText(Lower, apName, (int)std::size(apName)))
	{
		const char *apLines[] = {"Chris", "小精灵", "是我"};
		Say(apLines, (int)std::size(apLines), 5);
		return;
	}
	if(ContainsAnyText(Lower, apGreeting, (int)std::size(apGreeting)))
	{
		const char *apLines[] = {"在呢", "你好", "我在"};
		Say(apLines, (int)std::size(apLines), 5);
		AdjustAffinity(1);
		return;
	}

	const int MoodPick = ChrisRand(Now + ClientId * 197 + Text.size() * 13 + m_aChrisAffinity[ClientId] * 23, 100);
	if(m_aChrisAffinity[ClientId] >= 6 && MoodPick < 35)
	{
		SpawnChrisEmotion(pChris, ClientId, CChrisSprite::MOOD_LOVE);
		SpendEnergy(6);
	}
	else if(MoodPick < 26)
	{
		pChris->StartOrbit(TickSpeed * 3);
		SpendEnergy(8);
	}
	else
	{
		const char *apLines[] = {"嗯?", "我听见了", "跟着你", "收到"};
		Say(apLines, (int)std::size(apLines), 5);
	}
}

void CGameContext::TickChrisSprites()
{
	const int TickSpeed = Server()->TickSpeed();
	if(TickSpeed <= 0 || Server()->Tick() % maximum(1, TickSpeed) != 0)
		return;

	std::vector<CChrisSprite *> vpChrisSprites;
	bool HasActiveChris = false;
	bool ActiveChrisBusy = false;
	for(CEntity *pEntity = m_World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pEntity; pEntity = pEntity->TypeNext())
	{
		CChrisSprite *pChris = dynamic_cast<CChrisSprite *>(pEntity);
		if(!pChris || pChris->Hidden())
			continue;
		vpChrisSprites.push_back(pChris);
		HasActiveChris = true;
		ActiveChrisBusy = ActiveChrisBusy || pChris->Busy();
	}

	if(!HasActiveChris)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(!m_apPlayers[i] || !m_aChrisReturnOnRespawn[i] || !GetPlayerChar(i))
				continue;
			if(m_aChrisReturnTick[i] == 0)
			{
				m_aChrisReturnTick[i] = Server()->Tick() + TickSpeed * 3;
				continue;
			}
			if(Server()->Tick() < m_aChrisReturnTick[i])
				continue;

			CChrisSprite *pChris = EnsureChrisSprite(i);
			if(!pChris)
				continue;
			m_aChrisReturnOnRespawn[i] = false;
			m_aChrisReturnTick[i] = 0;
			const int Seed = Server()->Tick() + i * 683 + m_aChrisMeetCount[i] * 19;
			const char *apLines[] = {"找到你", "回来啦", "别乱跑"};
			const char *pLine = apLines[ChrisRand(Seed, (int)std::size(apLines))];
			if(pChris->Busy())
				QueueChrisText(i, pLine, CChrisSprite::MOOD_HAPPY);
			else
				SpawnChrisText(pChris, i, pLine);
			pChris->SetMood(CChrisSprite::MOOD_HAPPY, TickSpeed * 5);
			m_aChrisLastLineTick[i] = Server()->Tick();
			m_aChrisNextThinkTick[i] = Server()->Tick() + TickSpeed * (10 + ChrisRand(Seed, 7));
			HasActiveChris = true;
			ActiveChrisBusy = true;
			break;
		}
	}

	if(HasActiveChris && !ActiveChrisBusy)
	{
		int BestClient = -1;
		int BestTick = 0;
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(m_aChrisPendingCallTick[i] == 0)
				continue;
			if(Server()->Tick() > m_aChrisPendingCallTick[i] + TickSpeed * 45)
			{
				m_aChrisPendingCallTick[i] = 0;
				continue;
			}
			if(!m_apPlayers[i] || !GetPlayerChar(i))
				continue;
			if(BestClient < 0 || m_aChrisPendingCallTick[i] < BestTick)
			{
				BestClient = i;
				BestTick = m_aChrisPendingCallTick[i];
			}
		}

		if(BestClient >= 0)
		{
			CChrisSprite *pChris = EnsureChrisSprite(BestClient);
			if(pChris)
			{
				m_aChrisPendingCallTick[BestClient] = 0;
				const int Seed = Server()->Tick() + BestClient * 787 + BestTick;
				const char *apLines[] = {"来啦", "轮到你", "我来了"};
				const char *pLine = apLines[ChrisRand(Seed, (int)std::size(apLines))];
				if(pChris->Busy())
					QueueChrisText(BestClient, pLine, CChrisSprite::MOOD_HAPPY);
				else
					SpawnChrisText(pChris, BestClient, pLine);
				pChris->SetMood(CChrisSprite::MOOD_HAPPY, TickSpeed * 5);
				m_aChrisLastLineTick[BestClient] = Server()->Tick();
				m_aChrisNextThinkTick[BestClient] = Server()->Tick() + TickSpeed * (10 + ChrisRand(Seed, 7));
				ActiveChrisBusy = true;
			}
		}
	}

	if(!HasActiveChris || !ActiveChrisBusy)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(!m_apPlayers[i] || m_aChrisAutoGreetDone[i] || !GetPlayerChar(i))
				continue;
			if(m_aChrisAutoGreetTick[i] == 0)
			{
				m_aChrisAutoGreetTick[i] = Server()->Tick() + TickSpeed * (12 + ChrisRand(i * 193 + Server()->Tick(), 10));
				continue;
			}
			if(Server()->Tick() < m_aChrisAutoGreetTick[i])
				continue;

			m_aChrisAutoGreetDone[i] = true;
			int Affinity = 0;
			int MeetCount = 0;
			if(!PeekChrisMemory(i, &Affinity, &MeetCount))
				continue;
			if(Affinity < 5 && MeetCount < 4)
				continue;
			if(HasActiveChris && ChrisRand(Server()->Tick() + i * 997, 100) < 70)
				continue;

			CChrisSprite *pChris = EnsureChrisSprite(i);
			if(!pChris)
				continue;
			const int Seed = Server()->Tick() + i * 421 + MeetCount * 17 + Affinity * 31;
			const char *apLines[] = {"又见面", "回来啦", "等你呢", "熟人"};
			const char *pLine = apLines[ChrisRand(Seed, (int)std::size(apLines))];
			if(pChris->Busy())
				QueueChrisText(i, pLine, Affinity >= 8 ? CChrisSprite::MOOD_LOVE : CChrisSprite::MOOD_HAPPY);
			else
				SpawnChrisText(pChris, i, pLine);
			pChris->SetMood(Affinity >= 8 ? CChrisSprite::MOOD_LOVE : CChrisSprite::MOOD_HAPPY, TickSpeed * 6);
			m_aChrisLastLineTick[i] = Server()->Tick();
			m_aChrisNextThinkTick[i] = Server()->Tick() + TickSpeed * (14 + ChrisRand(Seed, 8));
			HasActiveChris = true;
			ActiveChrisBusy = true;
			break;
		}
	}

	for(CChrisSprite *pChris : vpChrisSprites)
	{
		if(!pChris || pChris->Hidden())
			continue;

		int Owner = pChris->Owner();
		CCharacter *pChr = (Owner >= 0 && Owner < MAX_CLIENTS) ? GetPlayerChar(Owner) : nullptr;
		if(!pChr)
		{
			Owner = -1;
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				if(GetPlayerChar(i))
				{
					Owner = i;
					break;
				}
			}
			if(Owner < 0)
				continue;
			pChris->SetOwner(Owner);
			LoadChrisMemory(Owner);
			pChr = GetPlayerChar(Owner);
			if(!pChr)
				continue;
		}
		else
			LoadChrisMemory(Owner);
		if(m_aChrisEnergy[Owner] <= 0)
			m_aChrisEnergy[Owner] = 80;
		else
			m_aChrisEnergy[Owner] = minimum(100, m_aChrisEnergy[Owner] + 4);
		if(FlushChrisQueuedText(pChris, Owner))
		{
			m_aChrisNextThinkTick[Owner] = Server()->Tick() + TickSpeed * 8;
			continue;
		}
		if(!pChris->Busy() && m_Music.HasCurrentQueueSong() && Server()->Tick() >= m_ChrisLastHumTick + TickSpeed * 18)
		{
			std::string Lyric;
			const int Seed = Server()->Tick() + Owner * 811 + m_Music.CurrentSongIndex() * 97;
			if(m_Music.CurrentLyricLine(Server()->Tick(), &Lyric) && !Lyric.empty() && ChrisRand(Seed, 100) < 28)
			{
				Lyric = TruncateUtf8ForChat(Lyric, 36);
				Lyric += "~";
				SpawnChrisText(pChris, Owner, Lyric.c_str());
				pChris->SetMood(CChrisSprite::MOOD_HAPPY, TickSpeed * 5);
				m_ChrisLastHumTick = Server()->Tick();
				m_aChrisLastLineTick[Owner] = Server()->Tick();
				m_aChrisNextThinkTick[Owner] = Server()->Tick() + TickSpeed * (10 + ChrisRand(Seed + 3, 8));
				continue;
			}
		}

		const float Moved = distance(pChr->m_Pos, m_aChrisLastOwnerPos[Owner]);
		if(m_aChrisNextThinkTick[Owner] == 0)
		{
			m_aChrisNextThinkTick[Owner] = Server()->Tick() + TickSpeed * (4 + ChrisRand(Owner + Server()->Tick(), 5));
			m_aChrisLastOwnerPos[Owner] = pChr->m_Pos;
			continue;
		}

		if(Moved < 18.0f)
			m_aChrisStillTicks[Owner] += TickSpeed;
		else
			m_aChrisStillTicks[Owner] = 0;
		m_aChrisLastOwnerPos[Owner] = pChr->m_Pos;

		const int CurrentSongIndex = m_Music.HasCurrentQueueSong() ? m_Music.CurrentSongIndex() : -1;
		const int EncodedSongIndex = CurrentSongIndex + 2;
		bool SongChanged = false;
		if(m_aChrisLastSongIndex[Owner] == 0)
			m_aChrisLastSongIndex[Owner] = EncodedSongIndex;
		else if(m_aChrisLastSongIndex[Owner] != EncodedSongIndex)
		{
			SongChanged = true;
			m_aChrisLastSongIndex[Owner] = EncodedSongIndex;
		}

		if(pChris->Busy() || Server()->Tick() < m_aChrisNextThinkTick[Owner])
			continue;

		int NearbyPlayers = 0;
		vec2 NearestPlayerPos = pChr->m_Pos;
		float NearestPlayerDist = 1000000.0f;
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(i == Owner || !m_apPlayers[i])
				continue;
			CCharacter *pOther = GetPlayerChar(i);
			if(!pOther)
				continue;
			const float Dist = distance(pOther->m_Pos, pChr->m_Pos);
			if(Dist < NearestPlayerDist)
			{
				NearestPlayerDist = Dist;
				NearestPlayerPos = pOther->m_Pos;
			}
			if(Dist < 260.0f)
				++NearbyPlayers;
		}

		const int Seed = Server()->Tick() + Owner * 1009 + m_aChrisStillTicks[Owner] * 7 + NearbyPlayers * 131;
		const bool CanSpeak = Server()->Tick() >= m_aChrisLastLineTick[Owner] + TickSpeed * 12;
		const int PreviousNearbyPlayers = m_aChrisLastNearbyCount[Owner];
		m_aChrisLastNearbyCount[Owner] = NearbyPlayers;
		int NextDelay = 8 + ChrisRand(Seed + 17, 11);

		if(SongChanged && CurrentSongIndex >= 0 && CanSpeak && ChrisRand(Seed + CurrentSongIndex * 73, 100) < 85)
		{
			const char *apLines[] = {"新歌", "开播", "换歌啦"};
			pChris->StartOrbit(TickSpeed * 4);
			SpawnChrisText(pChris, Owner, apLines[ChrisRand(Seed, (int)std::size(apLines))]);
			m_aChrisLastLineTick[Owner] = Server()->Tick();
			m_aChrisEnergy[Owner] = maximum(0, m_aChrisEnergy[Owner] - 8);
			NextDelay = 12 + ChrisRand(Seed, 8);
		}
		else if(NearbyPlayers > PreviousNearbyPlayers && NearbyPlayers > 0 && CanSpeak && ChrisRand(Seed + NearbyPlayers * 29, 100) < 70)
		{
			const char *apLines[] = {"有人来", "来客", "热闹了"};
			if(NearestPlayerDist < 360.0f && m_aChrisEnergy[Owner] > 28 && ChrisRand(Seed + 5, 100) < 45)
			{
				const vec2 Dir = NearestPlayerDist > 1.0f ? normalize(NearestPlayerPos - pChr->m_Pos) : vec2(1.0f, 0.0f);
				const vec2 Target = pChr->m_Pos + Dir * 118.0f + vec2(0.0f, -54.0f);
				pChris->StartScout(Target, TickSpeed * 4);
				m_aChrisEnergy[Owner] = maximum(0, m_aChrisEnergy[Owner] - 7);
			}
			else
			{
				pChris->SetMood(CChrisSprite::MOOD_HAPPY, TickSpeed * 5);
			}
			SpawnChrisText(pChris, Owner, apLines[ChrisRand(Seed, (int)std::size(apLines))]);
			m_aChrisLastLineTick[Owner] = Server()->Tick();
			NextDelay = 9 + ChrisRand(Seed, 7);
		}
		else if(m_aChrisStillTicks[Owner] >= TickSpeed * 24 && m_aChrisEnergy[Owner] > 55 && CanSpeak && ChrisRand(Seed + 43, 100) < 38)
		{
			const float Side = ChrisRand(Seed + 71, 2) == 0 ? 1.0f : -1.0f;
			vec2 Target = pChr->m_Pos + vec2(Side * (128.0f + ChrisRand(Seed + 3, 90)), -86.0f - ChrisRand(Seed + 9, 42));
			if(NearestPlayerDist < 520.0f)
				Target = (Target + NearestPlayerPos + vec2(0.0f, -70.0f)) * 0.5f;
			pChris->StartScout(Target, TickSpeed * 5);
			const char *apLines[] = {"去看看", "巡一下", "探路"};
			SpawnChrisText(pChris, Owner, apLines[ChrisRand(Seed, (int)std::size(apLines))]);
			m_aChrisLastLineTick[Owner] = Server()->Tick();
			m_aChrisEnergy[Owner] = maximum(0, m_aChrisEnergy[Owner] - 12);
			NextDelay = 16 + ChrisRand(Seed, 9);
		}
		else if(m_aChrisStillTicks[Owner] >= TickSpeed * 16)
		{
			if(CanSpeak)
			{
				const char *apLines[] = {"发呆中", "醒醒", "要走吗"};
				SpawnChrisText(pChris, Owner, apLines[ChrisRand(Seed, (int)std::size(apLines))]);
				m_aChrisLastLineTick[Owner] = Server()->Tick();
			}
			else
			{
				pChris->SetMood(CChrisSprite::MOOD_SLEEPY, TickSpeed * 5);
			}
			NextDelay = 12 + ChrisRand(Seed, 9);
		}
		else if(NearbyPlayers >= 2 && ChrisRand(Seed, 100) < 45)
		{
			if(CanSpeak && ChrisRand(Seed, 2) == 0)
			{
				SpawnChrisText(pChris, Owner, "好多人");
				m_aChrisLastLineTick[Owner] = Server()->Tick();
			}
			else
				SpawnChrisEmotion(pChris, Owner, CChrisSprite::MOOD_LOVE);
			NextDelay = 10 + ChrisRand(Seed, 8);
		}
		else if(Moved > 130.0f && ChrisRand(Seed, 100) < 55)
		{
			pChris->StartOrbit(TickSpeed * (3 + ChrisRand(Seed, 4)));
			if(CanSpeak && ChrisRand(Seed, 3) == 0)
			{
				SpawnChrisText(pChris, Owner, "等等我");
				m_aChrisLastLineTick[Owner] = Server()->Tick();
			}
			NextDelay = 8 + ChrisRand(Seed, 8);
		}
		else if(m_Music.HasCurrentQueueSong() && ChrisRand(Seed, 100) < 26)
		{
			pChris->StartOrbit(TickSpeed * 5);
			if(CanSpeak)
			{
				const char *apLines[] = {"在听", "好听", "跟着晃"};
				SpawnChrisText(pChris, Owner, apLines[ChrisRand(Seed, (int)std::size(apLines))]);
				m_aChrisLastLineTick[Owner] = Server()->Tick();
			}
			NextDelay = 14 + ChrisRand(Seed, 10);
		}
		else
		{
			const int Pick = ChrisRand(Seed, 100);
			if(Pick < 18)
			{
				SpawnChrisGuide(pChris, Owner);
				NextDelay = 18 + ChrisRand(Seed, 12);
			}
			else if(Pick < 46)
			{
				SpawnChrisEmotion(pChris, Owner, CChrisSprite::MOOD_HAPPY);
				NextDelay = 10 + ChrisRand(Seed, 9);
			}
			else if(Pick < 70)
			{
				pChris->StartOrbit(TickSpeed * (3 + ChrisRand(Seed, 3)));
				NextDelay = 9 + ChrisRand(Seed, 8);
			}
			else if(CanSpeak)
			{
				const char *apLines[] = {"我在", "看着你", "走吧", "嗯?"};
				SpawnChrisText(pChris, Owner, apLines[ChrisRand(Seed, (int)std::size(apLines))]);
				m_aChrisLastLineTick[Owner] = Server()->Tick();
				NextDelay = 11 + ChrisRand(Seed, 10);
			}
			else
			{
				pChris->SetMood(CChrisSprite::MOOD_HAPPY, TickSpeed * 4);
				NextDelay = 8 + ChrisRand(Seed, 8);
			}
		}

		m_aChrisNextThinkTick[Owner] = Server()->Tick() + TickSpeed * NextDelay;
	}
}

#endif

#if 0 // Static /chi command removed; keep only dynamic /chid.
int CGameContext::SpawnChiText(int ClientID, const char *pText, int *pUsedChars, int *pMissingChars)
{
	if(pUsedChars)
		*pUsedChars = 0;
	if(pMissingChars)
		*pMissingChars = 0;
	if(!LoadChiGlyphs())
		return 0;

	CCharacter *pChr = GetPlayerChar(ClientID);
	if(!pChr)
		return 0;

	std::vector<int> vCodepoints;
	const char *p = pText;
	while(*p)
	{
		const int Codepoint = str_utf8_decode(&p);
		if(Codepoint <= 0)
			break;
		vCodepoints.push_back(Codepoint);
	}
	if(vCodepoints.empty())
		return 0;

	ClearChiDots(ClientID);
	const int ExistingChiDots = CountChiDots(-1);
	const int MaxDotsForText = minimum(CHI_MAX_DOTS_TOTAL, maximum(0, CHI_MAX_DOTS_SERVER - ExistingChiDots));
	if(MaxDotsForText <= 0)
		return 0;

	float TotalWidth = 0.0f;
	for(size_t CharIndex = 0; CharIndex < vCodepoints.size(); ++CharIndex)
	{
		auto It = m_ChiGlyphs.find(vCodepoints[CharIndex]);
		TotalWidth += (It != m_ChiGlyphs.end() ? It->second.m_Advance : 1.0f) * CHI_CHAR_SIZE;
		if(CharIndex + 1 < vCodepoints.size())
			TotalWidth += ChiMixedGap(vCodepoints[CharIndex], vCodepoints[CharIndex + 1], CHI_CHAR_SPACING);
	}
	const vec2 Origin = pChr->m_Pos + vec2(-TotalWidth / 2.0f, -190.0f);
	std::vector<int> vCharBudgets(vCodepoints.size(), 0);
	std::vector<float> vBudgetRemainders(vCodepoints.size(), 0.0f);
	float TotalWeight = 0.0f;
	for(size_t CharIndex = 0; CharIndex < vCodepoints.size(); ++CharIndex)
	{
		auto It = m_ChiGlyphs.find(vCodepoints[CharIndex]);
		if(It == m_ChiGlyphs.end())
			continue;

		float TotalStrokeLength = 0.0f;
		for(const auto &Stroke : It->second.m_vStrokes)
			TotalStrokeLength += StrokeLength(Stroke);

		const float Weight = 0.4f + It->second.m_vStrokes.size() * 0.28f + std::sqrt(maximum(0.0f, TotalStrokeLength) / 1024.0f);
		vBudgetRemainders[CharIndex] = Weight;
		TotalWeight += Weight;
	}
	int BudgetedDots = 0;
	if(TotalWeight > 0.0f)
	{
		for(size_t CharIndex = 0; CharIndex < vCodepoints.size(); ++CharIndex)
		{
			if(vBudgetRemainders[CharIndex] <= 0.0f)
				continue;
			const float Exact = MaxDotsForText * vBudgetRemainders[CharIndex] / TotalWeight;
			const int Budget = minimum(CHI_MAX_DOTS_PER_CHAR, (int)std::floor(Exact));
			vCharBudgets[CharIndex] = Budget;
			vBudgetRemainders[CharIndex] = Exact - std::floor(Exact);
			BudgetedDots += Budget;
		}

		std::vector<int> vBudgetOrder;
		for(size_t CharIndex = 0; CharIndex < vCodepoints.size(); ++CharIndex)
		{
			if(vBudgetRemainders[CharIndex] > 0.0f)
				vBudgetOrder.push_back((int)CharIndex);
		}
		std::sort(vBudgetOrder.begin(), vBudgetOrder.end(), [&vBudgetRemainders](int Left, int Right) {
			if(vBudgetRemainders[Left] == vBudgetRemainders[Right])
				return Left < Right;
			return vBudgetRemainders[Left] > vBudgetRemainders[Right];
		});

		for(size_t OrderIndex = 0; OrderIndex < vBudgetOrder.size() && BudgetedDots < MaxDotsForText; ++OrderIndex)
		{
			const int CharIndex = vBudgetOrder[OrderIndex];
			if(vCharBudgets[CharIndex] >= CHI_MAX_DOTS_PER_CHAR)
				continue;
			++vCharBudgets[CharIndex];
			++BudgetedDots;
		}
	}
	int Spawned = 0;
	float CharOffset = 0.0f;

	for(size_t CharIndex = 0; CharIndex < vCodepoints.size() && Spawned < MaxDotsForText; ++CharIndex)
	{
		const int Codepoint = vCodepoints[CharIndex];
		auto It = m_ChiGlyphs.find(Codepoint);
		if(It == m_ChiGlyphs.end())
		{
			if(pMissingChars)
				++*pMissingChars;
			continue;
		}
		if(pUsedChars)
			++*pUsedChars;

		const SChiGlyph &Glyph = It->second;
		if(Glyph.m_vStrokes.empty())
		{
			CharOffset += Glyph.m_Advance * CHI_CHAR_SIZE;
			if(CharIndex + 1 < vCodepoints.size())
				CharOffset += ChiMixedGap(vCodepoints[CharIndex], vCodepoints[CharIndex + 1], CHI_CHAR_SPACING);
			continue;
		}
		const int MaxForChar = minimum(CHI_MAX_DOTS_PER_CHAR, maximum(1, vCharBudgets[CharIndex]));
		std::vector<vec2> vSamples = BuildBalancedSamples(Glyph.m_vStrokes, MaxForChar);
		int CharDots = 0;
		for(size_t i = 0; i < vSamples.size() && Spawned < MaxDotsForText && CharDots < MaxForChar; ++i)
		{
			const vec2 Source = vSamples[i];
			const vec2 Local = vec2(Source.x / 1024.0f * CHI_CHAR_SIZE, (1.0f - Source.y / 1024.0f) * CHI_CHAR_SIZE);
			const vec2 Pos = Origin + vec2(CharOffset, 0.0f) + Local;
			new CChiDot(&m_World, ClientID, Pos, Spawned % CHI_DOT_SNAP_INTERVAL, CHI_DOT_SNAP_INTERVAL);
			++Spawned;
			++CharDots;
		}
		CharOffset += Glyph.m_Advance * CHI_CHAR_SIZE;
		if(CharIndex + 1 < vCodepoints.size())
			CharOffset += ChiMixedGap(vCodepoints[CharIndex], vCodepoints[CharIndex + 1], CHI_CHAR_SPACING);
	}
	return Spawned;
}

#endif

int CGameContext::SpawnChiDynamicText(int ClientID, const char *pText, float SizeScale, int *pUsedChars, int *pMissingChars)
{
	if(pUsedChars)
		*pUsedChars = 0;
	if(pMissingChars)
		*pMissingChars = 0;
	if(!LoadChiGlyphs())
		return 0;

	CCharacter *pChr = GetPlayerChar(ClientID);
	if(!pChr)
		return 0;

	SizeScale = NormalizeChidSizeScale(SizeScale);
	const float CharSize = CHI_CHAR_SIZE * SizeScale;
	const float CharSpacing = CHI_CHAR_SPACING * SizeScale;
	const int MaxDotsPerChar = std::clamp((int)std::round(CHI_MAX_DOTS_PER_CHAR * SizeScale), 10, CHI_MAX_DOTS_PER_CHAR);

	std::vector<int> vCodepoints;
	const char *p = pText;
	while(*p)
	{
		const int Codepoint = str_utf8_decode(&p);
		if(Codepoint <= 0)
			break;
		vCodepoints.push_back(Codepoint);
	}
	if(vCodepoints.empty())
		return 0;

	ClearChiDots(ClientID);
	const int ExistingChiDots = CountChiDots(-1);
	const int MaxDotsForText = minimum(CHI_MAX_DOTS_TOTAL, maximum(0, CHI_MAX_DOTS_SERVER - ExistingChiDots));
	if(MaxDotsForText <= 0)
		return 0;

	float TotalWidth = 0.0f;
	for(size_t CharIndex = 0; CharIndex < vCodepoints.size(); ++CharIndex)
	{
		auto It = m_ChiGlyphs.find(vCodepoints[CharIndex]);
		TotalWidth += (It != m_ChiGlyphs.end() ? It->second.m_Advance : 1.0f) * CharSize;
		if(CharIndex + 1 < vCodepoints.size())
			TotalWidth += ChiMixedGap(vCodepoints[CharIndex], vCodepoints[CharIndex + 1], CharSpacing);
	}

	const vec2 SourceCenter = pChr->m_Pos + vec2(0.0f, -34.0f);
	const vec2 Origin = pChr->m_Pos + vec2(-TotalWidth / 2.0f, -190.0f);
	std::vector<int> vCharBudgets(vCodepoints.size(), 0);
	std::vector<float> vBudgetRemainders(vCodepoints.size(), 0.0f);
	float TotalWeight = 0.0f;
	for(size_t CharIndex = 0; CharIndex < vCodepoints.size(); ++CharIndex)
	{
		auto It = m_ChiGlyphs.find(vCodepoints[CharIndex]);
		if(It == m_ChiGlyphs.end())
			continue;

		float TotalStrokeLength = 0.0f;
		for(const auto &Stroke : It->second.m_vStrokes)
			TotalStrokeLength += StrokeLength(Stroke);

		const float Weight = 0.4f + It->second.m_vStrokes.size() * 0.28f + std::sqrt(maximum(0.0f, TotalStrokeLength) / 1024.0f);
		vBudgetRemainders[CharIndex] = Weight;
		TotalWeight += Weight;
	}
	int BudgetedDots = 0;
	if(TotalWeight > 0.0f)
	{
		for(size_t CharIndex = 0; CharIndex < vCodepoints.size(); ++CharIndex)
		{
			if(vBudgetRemainders[CharIndex] <= 0.0f)
				continue;
			const float Exact = MaxDotsForText * vBudgetRemainders[CharIndex] / TotalWeight;
			const int Budget = minimum(MaxDotsPerChar, (int)std::floor(Exact));
			vCharBudgets[CharIndex] = Budget;
			vBudgetRemainders[CharIndex] = Exact - std::floor(Exact);
			BudgetedDots += Budget;
		}

		std::vector<int> vBudgetOrder;
		for(size_t CharIndex = 0; CharIndex < vCodepoints.size(); ++CharIndex)
		{
			if(vBudgetRemainders[CharIndex] > 0.0f)
				vBudgetOrder.push_back((int)CharIndex);
		}
		std::sort(vBudgetOrder.begin(), vBudgetOrder.end(), [&vBudgetRemainders](int Left, int Right) {
			if(vBudgetRemainders[Left] == vBudgetRemainders[Right])
				return Left < Right;
			return vBudgetRemainders[Left] > vBudgetRemainders[Right];
		});
		for(size_t OrderIndex = 0; OrderIndex < vBudgetOrder.size() && BudgetedDots < MaxDotsForText; ++OrderIndex)
		{
			const int CharIndex = vBudgetOrder[OrderIndex];
			if(vCharBudgets[CharIndex] >= MaxDotsPerChar)
				continue;
			++vCharBudgets[CharIndex];
			++BudgetedDots;
		}
	}

	int Spawned = 0;
	float CharOffset = 0.0f;
	const int BaseTick = Server()->Tick();
	const int ExitEffect = (BaseTick + ClientID * 131 + (int)vCodepoints.size() * 17) % CChiDot::NUM_EXIT_EFFECTS;
	for(size_t CharIndex = 0; CharIndex < vCodepoints.size() && Spawned < MaxDotsForText; ++CharIndex)
	{
		const int Codepoint = vCodepoints[CharIndex];
		auto It = m_ChiGlyphs.find(Codepoint);
		if(It == m_ChiGlyphs.end())
		{
			if(pMissingChars)
				++*pMissingChars;
			continue;
		}
		if(pUsedChars)
			++*pUsedChars;

		const SChiGlyph &Glyph = It->second;
		if(Glyph.m_vStrokes.empty())
		{
			CharOffset += Glyph.m_Advance * CharSize;
			if(CharIndex + 1 < vCodepoints.size())
				CharOffset += ChiMixedGap(vCodepoints[CharIndex], vCodepoints[CharIndex + 1], CharSpacing);
			continue;
		}

		const int MaxForChar = minimum(MaxDotsPerChar, maximum(1, vCharBudgets[CharIndex]));
		std::vector<vec2> vSamples = BuildBalancedSamples(Glyph.m_vStrokes, MaxForChar);
		const int AppearTick = BaseTick + (int)CharIndex * CHID_CHAR_DELAY_TICKS;
		const int ArriveTick = AppearTick + CHID_FLY_TICKS;
		const int HoldEndTick = BaseTick + (int)vCodepoints.size() * CHID_CHAR_DELAY_TICKS + CHID_HOLD_TICKS;
		const int ExitStartTick = ExitEffect == CChiDot::EXIT_HOLD ? -1 : HoldEndTick + (int)CharIndex * CHID_FADE_DELAY_TICKS;
		const int ExpireTick = ExitEffect == CChiDot::EXIT_HOLD ? HoldEndTick + (int)CharIndex * CHID_FADE_DELAY_TICKS : ExitStartTick + CHID_EXIT_TICKS;

		int CharDots = 0;
		for(size_t i = 0; i < vSamples.size() && Spawned < MaxDotsForText && CharDots < MaxForChar; ++i)
		{
			const vec2 Source = vSamples[i];
			const vec2 Local = vec2(Source.x / 1024.0f * CharSize, (1.0f - Source.y / 1024.0f) * CharSize);
			const vec2 Target = Origin + vec2(CharOffset, 0.0f) + Local;
			const vec2 From = SourceCenter + ClusterOffset(Spawned);
			const int Seed = BaseTick * 31 + ClientID * 997 + Spawned * 17 + (int)CharIndex * 257;
			new CChiDot(&m_World, ClientID, From, Target, AppearTick, ArriveTick, ExpireTick, Spawned % CHI_DOT_SNAP_INTERVAL, CHI_DOT_SNAP_INTERVAL, ExitEffect, ExitStartTick, Seed);
			++Spawned;
			++CharDots;
		}
		CharOffset += Glyph.m_Advance * CharSize;
		if(CharIndex + 1 < vCodepoints.size())
			CharOffset += ChiMixedGap(vCodepoints[CharIndex], vCodepoints[CharIndex + 1], CharSpacing);
	}
	return Spawned;
}

#if 0 // Static /chi command removed; keep only dynamic /chid.
void CGameContext::ConChatChi(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientID = pResult->m_ClientId;
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientID])
		return;

	const char *pText = pResult->NumArguments() > 0 ? pResult->GetString(0) : "";
	if(!pText[0])
	{
		pSelf->ClearChiDots(ClientID);
		pSelf->SendChatTarget(ClientID, "用法：/chi 内容；空内容会清除你的字点。");
		return;
	}

	int UsedChars = 0;
	int MissingChars = 0;
	const int Dots = pSelf->SpawnChiText(ClientID, pText, &UsedChars, &MissingChars);
	char aBuf[160];
	if(Dots <= 0)
	{
		str_format(aBuf, sizeof(aBuf), "没有生成字点。可能缺少字形或你还没有角色。");
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "已生成 %d 个锤子白点，%d 个字符，缺字 %d 个。", Dots, UsedChars, MissingChars);
	}
	pSelf->SendChatTarget(ClientID, aBuf);
}

#endif

void CGameContext::ConChid(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientID = pResult->m_ClientId;
	const char *pText = pResult->NumArguments() > 0 ? pResult->GetString(0) : "";
	pSelf->ExecuteChid(ClientID, pText);
}

void CGameContext::ExecuteChid(int ClientID, const char *pText)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID])
		return;

	if(!pText[0])
	{
		ClearChiDots(ClientID);
		SendChatTarget(ClientID, "用法：/chid [字号] 内容；例如 /chid 0.7 你好 或 /chid 70 你好。");
		return;
	}

	float SizeScale = 1.0f;
	const char *pDisplayText = pText;
	TryParseChidSizePrefix(pText, &SizeScale, &pDisplayText);

	int UsedChars = 0;
	int MissingChars = 0;
	const int Dots = SpawnChiDynamicText(ClientID, pDisplayText, SizeScale, &UsedChars, &MissingChars);
	char aBuf[160];
	if(Dots <= 0)
		str_copy(aBuf, "没有生成动态字点。可能缺少字形或你还没有角色。", sizeof(aBuf));
	else
		str_format(aBuf, sizeof(aBuf), "已生成 %d 个动态锤子白点，%d 个字符，字号 %.2f，缺字 %d 个。", Dots, UsedChars, SizeScale, MissingChars);
	SendChatTarget(ClientID, aBuf);
}

#if 0 // Chris sprite feature removed.
void CGameContext::ConChatChris(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientID = pResult->m_ClientId;
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientID])
		return;

	CCharacter *pChr = pSelf->GetPlayerChar(ClientID);
	if(!pChr)
	{
		pSelf->SendChatTarget(ClientID, "Chris 找不到你的位置，先进入游戏里。");
		return;
	}

	const std::string Args = TrimCopy(pResult->NumArguments() > 0 ? pResult->GetString(0) : "");
	const std::string Cmd = FirstToken(Args);
	const std::string Rest = AfterFirstToken(Args);

	if(Cmd == "help")
	{
		pSelf->SendChatTarget(ClientID, "/chris 召唤/互动；/chris say 内容；/chris guide；/chris mood happy|sad|love；/chris orbit|dance；/chris ping；/chris hide");
		return;
	}

	CChrisSprite *pChris = pSelf->EnsureChrisSprite(ClientID);
	if(!pChris)
	{
		pSelf->m_aChrisPendingCallTick[ClientID] = pSelf->Server()->Tick();
		pSelf->SendChatTarget(ClientID, "Chris 现在正忙，等一下会来找你。");
		return;
	}

	if(pChris->Busy() && (Args.empty() || Cmd == "summon"))
	{
		pSelf->QueueChrisText(ClientID, "我来啦", CChrisSprite::MOOD_HAPPY);
		pSelf->SendChatTarget(ClientID, "Chris 正在过来。");
		return;
	}
	if(pChris->Busy() && Cmd == "say")
	{
		if(Rest.empty())
			pSelf->SendChatTarget(ClientID, "用法：/chris say 内容");
		else
		{
			pSelf->QueueChrisText(ClientID, Rest.c_str(), CChrisSprite::MOOD_HAPPY);
			pSelf->SendChatTarget(ClientID, "Chris 过来后会说。");
		}
		return;
	}

	if(Args.empty() || Cmd == "summon")
	{
		const int Pick = pSelf->Server()->Tick() % 5;
		if(Pick == 0)
		{
			pSelf->SpawnChrisText(pChris, ClientID, "我来啦");
		}
		else if(Pick == 1)
		{
			pChris->StartOrbit(pSelf->Server()->TickSpeed() * 5);
			pSelf->SpawnChrisText(pChris, ClientID, "转一圈");
		}
		else if(Pick == 2)
		{
			pSelf->SpawnChrisGuide(pChris, ClientID);
		}
		else if(Pick == 3)
		{
			pSelf->SpawnChrisEmotion(pChris, ClientID, CChrisSprite::MOOD_LOVE);
		}
		else
		{
			pSelf->SpawnChrisText(pChris, ClientID, "听见啦");
		}
		pSelf->SendChatTarget(ClientID, "Chris 出现了。");
		return;
	}

	if(Cmd == "say")
	{
		if(Rest.empty())
		{
			pSelf->SendChatTarget(ClientID, "用法：/chris say 内容");
			return;
		}
		const int Dots = pSelf->SpawnChrisText(pChris, ClientID, Rest.c_str());
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Chris 生成了 %d 个字幕点。", Dots);
		pSelf->SendChatTarget(ClientID, aBuf);
		return;
	}

	if(Cmd == "guide")
	{
		pSelf->SpawnChrisGuide(pChris, ClientID);
		pSelf->SendChatTarget(ClientID, "Chris 跑去标了一个位置。");
		return;
	}

	if(Cmd == "orbit")
	{
		pChris->StartOrbit(pSelf->Server()->TickSpeed() * 7);
		pSelf->SpawnChrisText(pChris, ClientID, "抓不到我");
		pSelf->SendChatTarget(ClientID, "Chris 开始绕着你转。");
		return;
	}

	if(Cmd == "dance")
	{
		pChris->StartOrbit(pSelf->Server()->TickSpeed() * 8);
		pSelf->SpawnChrisText(pChris, ClientID, "跳一下");
		pSelf->SendChatTarget(ClientID, "Chris 高兴地转起来了。");
		return;
	}

	if(Cmd == "ping")
	{
		std::vector<vec2> vPoints;
		const vec2 Center = pChr->m_Pos + vec2(0.0f, -78.0f);
		for(int i = 0; i < 56; ++i)
		{
			const float a = i / 56.0f * 2.0f * pi;
			vPoints.push_back(Center + vec2(std::cos(a) * 64.0f, std::sin(a) * 64.0f));
		}
		const int Life = pSelf->Server()->TickSpeed() * 5;
		int Spawned = 0;
		for(const vec2 &Point : vPoints)
		{
			new CChiDot(&pSelf->m_World, ClientID, Point, Spawned % CHI_DOT_SNAP_INTERVAL, CHI_DOT_SNAP_INTERVAL, Life);
			++Spawned;
		}
		pSelf->SpawnChrisText(pChris, ClientID, "这里");
		pChris->CastPower(pSelf->Server()->TickSpeed());
		pSelf->SendChatTarget(ClientID, "Chris 在你身边点了一下。");
		return;
	}

	if(Cmd == "hide")
	{
		pSelf->m_aChrisReturnOnRespawn[ClientID] = false;
		pSelf->m_aChrisReturnTick[ClientID] = 0;
		pSelf->m_aChrisPendingCallTick[ClientID] = 0;
		g_Config.m_SvChrisDummy = 0;
		pChris->Hide();
		pSelf->SendChatTarget(ClientID, "Chris 缩回去了。");
		return;
	}

	if(Cmd == "mood")
	{
		const std::string Mood = LowerAscii(Rest);
		int MoodId = CChrisSprite::MOOD_HAPPY;
		if(Mood == "sad" || Mood == "cry" || Mood == "哭")
			MoodId = CChrisSprite::MOOD_SAD;
		else if(Mood == "love" || Mood == "heart" || Mood == "爱")
			MoodId = CChrisSprite::MOOD_LOVE;
		else if(Mood == "sleep" || Mood == "sleepy")
			MoodId = CChrisSprite::MOOD_SLEEPY;
		pSelf->SpawnChrisEmotion(pChris, ClientID, MoodId);
		pSelf->SendChatTarget(ClientID, "Chris 摆了一个表情。");
		return;
	}

	if(pChris->Busy())
	{
		pSelf->QueueChrisText(ClientID, Args.c_str(), CChrisSprite::MOOD_HAPPY);
		pSelf->SendChatTarget(ClientID, "Chris 过来后会说。");
		return;
	}

	pSelf->SpawnChrisText(pChris, ClientID, Args.c_str());
	pSelf->SendChatTarget(ClientID, "Chris 把这句话吐出来了。");
}
#endif

CChrisSprite *CGameContext::FindChrisSprite()
{
	CChrisSprite *pFirstChris = nullptr;
	for(CEntity *pEntity = m_World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pEntity; pEntity = pEntity->TypeNext())
	{
		CChrisSprite *pChris = dynamic_cast<CChrisSprite *>(pEntity);
		if(!pChris)
			continue;
		if(!pFirstChris)
		{
			pFirstChris = pChris;
			continue;
		}
		pChris->Reset();
	}
	return pFirstChris;
}

CChrisSprite *CGameContext::EnsureChrisSprite(int ClientID)
{
	CCharacter *pChr = GetPlayerChar(ClientID);
	if(!pChr)
		return nullptr;

	const vec2 SpawnPos = pChr->m_Pos + vec2(78.0f, -96.0f);
	CChrisSprite *pChris = FindChrisSprite();
	if(pChris)
	{
		pChris->Wake(SpawnPos, ClientID);
		return pChris;
	}
	return new CChrisSprite(&m_World, ClientID, SpawnPos);
}

void CGameContext::ConChris(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int ClientID = pResult->GetInteger(0);
	const std::string Args = TrimCopy(pResult->NumArguments() > 1 ? pResult->GetString(1) : "");
	const std::string Cmd = FirstToken(Args);
	const std::string Rest = AfterFirstToken(Args);

	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientID])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chris", "Target client is not connected.");
		return;
	}
	if(Cmd == "help")
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chris", "Usage: chris <client-id> [summon|say <text>|guide|orbit|dance|ping|mood <happy|sad|love|sleep>|hide]");
		return;
	}

	CChrisSprite *pChris = pSelf->FindChrisSprite();
	if(Cmd == "hide")
	{
		if(!pChris || pChris->Hidden())
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chris", "Chris is not active.");
			return;
		}
		pChris->Hide();
		pSelf->SendChatTarget(ClientID, "Chris 缩回去了。");
		return;
	}

	CCharacter *pChr = pSelf->GetPlayerChar(ClientID);
	if(!pChr)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chris", "Target must be alive before Chris can appear.");
		return;
	}
	pChris = pSelf->EnsureChrisSprite(ClientID);
	if(!pChris)
		return;

	if(Args.empty() || Cmd == "summon")
	{
		pChris->SetMood(CChrisSprite::MOOD_HAPPY, pSelf->Server()->TickSpeed() * 4);
		pSelf->SendChatTarget(ClientID, "Chris 出现了。");
		return;
	}
	if(Cmd == "say")
	{
		if(Rest.empty())
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chris", "Usage: chris <client-id> say <text>");
			return;
		}
		int UsedChars = 0;
		int MissingChars = 0;
		const int Dots = pSelf->SpawnChiDynamicText(ClientID, Rest.c_str(), 0.80f, &UsedChars, &MissingChars);
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Chris displayed %d dots for client %d.", Dots, ClientID);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chris", aBuf);
		pChris->SetMood(CChrisSprite::MOOD_HAPPY, pSelf->Server()->TickSpeed() * 4);
		return;
	}
	if(Cmd == "guide")
	{
		const float Side = (pSelf->Server()->Tick() / pSelf->Server()->TickSpeed()) % 2 == 0 ? 1.0f : -1.0f;
		pChris->StartScout(pChr->m_Pos + vec2(Side * 260.0f, -88.0f), pSelf->Server()->TickSpeed() * 7);
		return;
	}
	if(Cmd == "orbit" || Cmd == "dance")
	{
		pChris->StartOrbit(pSelf->Server()->TickSpeed() * (Cmd == "dance" ? 8 : 6));
		return;
	}
	if(Cmd == "ping")
	{
		const vec2 Center = pChr->m_Pos + vec2(0.0f, -78.0f);
		const int Life = pSelf->Server()->TickSpeed() * 4;
		for(int i = 0; i < 48; ++i)
		{
			const float Angle = i / 48.0f * 2.0f * pi;
			new CChiDot(&pSelf->m_World, ClientID, Center + vec2(std::cos(Angle) * 64.0f, std::sin(Angle) * 64.0f), i % CHI_DOT_SNAP_INTERVAL, CHI_DOT_SNAP_INTERVAL, Life);
		}
		pChris->CastPower(pSelf->Server()->TickSpeed());
		return;
	}
	if(Cmd == "mood")
	{
		const std::string Mood = LowerAscii(Rest);
		int MoodId = CChrisSprite::MOOD_HAPPY;
		if(Mood == "sad" || Mood == "cry")
			MoodId = CChrisSprite::MOOD_SAD;
		else if(Mood == "love" || Mood == "heart")
			MoodId = CChrisSprite::MOOD_LOVE;
		else if(Mood == "sleep" || Mood == "sleepy")
			MoodId = CChrisSprite::MOOD_SLEEPY;
		pChris->SetMood(MoodId, pSelf->Server()->TickSpeed() * 5);
		return;
	}

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chris", "Unknown action. Use: chris <client-id> help");
}
