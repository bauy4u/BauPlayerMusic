#include "dice_3d.h"

#include <base/math.h>
#include <base/system.h>
#include <engine/server.h>
#include <game/collision.h>
#include <game/generated/protocol.h>
#include <game/mapitems.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <algorithm>
#include <cmath>

namespace
{
using TQuaternion = std::array<float, 4>;

constexpr float DICE_HALF_SIZE = 40.0f;
constexpr float DICE_BODY_SIZE = DICE_HALF_SIZE * 2.0f;
constexpr float DICE_PI = 3.14159265358979323846f;

TQuaternion QuaternionNormalize(const TQuaternion &Q)
{
	const float Length = std::sqrt(Q[0] * Q[0] + Q[1] * Q[1] + Q[2] * Q[2] + Q[3] * Q[3]);
	if(Length < 0.000001f)
		return {1.0f, 0.0f, 0.0f, 0.0f};
	return {Q[0] / Length, Q[1] / Length, Q[2] / Length, Q[3] / Length};
}

TQuaternion QuaternionMultiply(const TQuaternion &A, const TQuaternion &B)
{
	return {
		A[0] * B[0] - A[1] * B[1] - A[2] * B[2] - A[3] * B[3],
		A[0] * B[1] + A[1] * B[0] + A[2] * B[3] - A[3] * B[2],
		A[0] * B[2] - A[1] * B[3] + A[2] * B[0] + A[3] * B[1],
		A[0] * B[3] + A[1] * B[2] - A[2] * B[1] + A[3] * B[0]};
}

TQuaternion QuaternionFromAxisAngle(vec3 Axis, float Angle)
{
	if(length(Axis) < 0.000001f || std::abs(Angle) < 0.000001f)
		return {1.0f, 0.0f, 0.0f, 0.0f};
	Axis = normalize(Axis);
	const float HalfAngle = Angle * 0.5f;
	const float Sine = std::sin(HalfAngle);
	return {std::cos(HalfAngle), Axis.x * Sine, Axis.y * Sine, Axis.z * Sine};
}

vec3 QuaternionRotate(const TQuaternion &Q, vec3 V)
{
	const vec3 VectorPart(Q[1], Q[2], Q[3]);
	const vec3 TwiceCross = cross(VectorPart, V) * 2.0f;
	return V + TwiceCross * Q[0] + cross(VectorPart, TwiceCross);
}

TQuaternion QuaternionConjugate(const TQuaternion &Q)
{
	return {Q[0], -Q[1], -Q[2], -Q[3]};
}

float QuaternionDot(const TQuaternion &A, const TQuaternion &B)
{
	return A[0] * B[0] + A[1] * B[1] + A[2] * B[2] + A[3] * B[3];
}

TQuaternion CanonicalResultOrientation(int Result)
{
	switch(Result)
	{
	case 1: return {1.0f, 0.0f, 0.0f, 0.0f};
	case 2: return QuaternionFromAxisAngle(vec3(0.0f, 1.0f, 0.0f), -DICE_PI * 0.5f);
	case 3: return QuaternionFromAxisAngle(vec3(1.0f, 0.0f, 0.0f), DICE_PI * 0.5f);
	case 4: return QuaternionFromAxisAngle(vec3(1.0f, 0.0f, 0.0f), -DICE_PI * 0.5f);
	case 5: return QuaternionFromAxisAngle(vec3(0.0f, 1.0f, 0.0f), DICE_PI * 0.5f);
	default: return QuaternionFromAxisAngle(vec3(0.0f, 1.0f, 0.0f), DICE_PI);
	}
}

float RandomSigned()
{
	return ((int)(secure_rand() % 2001) - 1000) / 1000.0f;
}

struct SFace
{
	vec3 m_Normal;
	vec3 m_Right;
	vec3 m_Down;
	int m_Value;
};

constexpr std::array<vec3, 8> gs_aVertices = {
	vec3(-DICE_HALF_SIZE, -DICE_HALF_SIZE, -DICE_HALF_SIZE),
	vec3(DICE_HALF_SIZE, -DICE_HALF_SIZE, -DICE_HALF_SIZE),
	vec3(DICE_HALF_SIZE, DICE_HALF_SIZE, -DICE_HALF_SIZE),
	vec3(-DICE_HALF_SIZE, DICE_HALF_SIZE, -DICE_HALF_SIZE),
	vec3(-DICE_HALF_SIZE, -DICE_HALF_SIZE, DICE_HALF_SIZE),
	vec3(DICE_HALF_SIZE, -DICE_HALF_SIZE, DICE_HALF_SIZE),
	vec3(DICE_HALF_SIZE, DICE_HALF_SIZE, DICE_HALF_SIZE),
	vec3(-DICE_HALF_SIZE, DICE_HALF_SIZE, DICE_HALF_SIZE)};

// Face order: front, right, bottom, top, left, back.
constexpr std::array<SFace, 6> gs_aFaces = {{
	{vec3(0, 0, 1), vec3(1, 0, 0), vec3(0, 1, 0), 1},
	{vec3(1, 0, 0), vec3(0, 0, -1), vec3(0, 1, 0), 2},
	{vec3(0, 1, 0), vec3(1, 0, 0), vec3(0, 0, -1), 3},
	{vec3(0, -1, 0), vec3(1, 0, 0), vec3(0, 0, 1), 4},
	{vec3(-1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0), 5},
	{vec3(0, 0, -1), vec3(-1, 0, 0), vec3(0, 1, 0), 6},
}};

struct SEdge
{
	int m_VertexA;
	int m_VertexB;
	int m_FaceA;
	int m_FaceB;
};

constexpr std::array<SEdge, 12> gs_aEdges = {{
	{0, 1, 3, 5}, {1, 2, 1, 5}, {2, 3, 2, 5}, {3, 0, 4, 5},
	{4, 5, 0, 3}, {5, 6, 0, 1}, {6, 7, 0, 2}, {7, 4, 0, 4},
	{0, 4, 3, 4}, {1, 5, 3, 1}, {2, 6, 1, 2}, {3, 7, 2, 4},
}};
}

CDice3D::CDice3D(CGameWorld *pGameWorld, vec2 Pos, int FlightChessTeam, int OwnerClientId) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_DICE3D, Pos, (int)DICE_HALF_SIZE),
	m_FlightChessTeam(FlightChessTeam),
	m_OwnerClientId(OwnerClientId)
{
	for(int &Id : m_aSnapIds)
		Id = Server()->SnapNewId();
	GameWorld()->InsertEntity(this);
}

CDice3D::~CDice3D()
{
	if(GameServer() && IsFlightChessDice())
		GameServer()->OnFlightChessDiceDestroyed(this);
	for(int Id : m_aSnapIds)
		Server()->SnapFreeId(Id);
}

void CDice3D::SavePersistentState(SPersistentState *pState) const
{
	if(!pState)
		return;
	*pState = SPersistentState{};
	pState->m_Valid = true;
	pState->m_Pos = m_Pos;
	pState->m_Vel = m_Vel;
	pState->m_Orientation = m_Orientation;
	pState->m_aFaceValues = m_aFaceValues;
	pState->m_vRecordedFrames = m_vRecordedFrames;
	pState->m_ReplayFrame = m_ReplayFrame;
	pState->m_State = m_State;
	pState->m_Result = m_Result;
	pState->m_StillTicks = m_StillTicks;
	pState->m_HasPendingResult = m_HasPendingResult;
}

void CDice3D::LoadPersistentState(const SPersistentState &State)
{
	if(!State.m_Valid)
		return;
	m_Pos = State.m_Pos;
	m_Vel = State.m_Vel;
	m_Orientation = QuaternionNormalize(State.m_Orientation);
	m_aFaceValues = State.m_aFaceValues;
	m_vRecordedFrames = State.m_vRecordedFrames;
	m_ReplayFrame = std::min(State.m_ReplayFrame, m_vRecordedFrames.size());
	m_State = std::clamp(State.m_State, (int)STATE_FALLING, (int)STATE_REPLAYING);
	m_Result = std::clamp(State.m_Result, 1, 6);
	m_StillTicks = maximum(State.m_StillTicks, 0);
	m_HasPendingResult = State.m_HasPendingResult;

	// A replaying die always has at least one unplayed frame. Treat malformed or
	// obsolete saved data as a settled die instead of leaving the game in phase 1.
	if(m_State == STATE_REPLAYING && (m_vRecordedFrames.empty() || m_ReplayFrame >= m_vRecordedFrames.size()))
	{
		m_vRecordedFrames.clear();
		m_ReplayFrame = 0;
		m_Vel = vec2();
		m_State = STATE_RESTING;
		m_HasPendingResult = false;
	}
}

bool CDice3D::HammerHit(vec2 Impulse)
{
	if(m_State != STATE_RESTING)
		return false;
	// The result is selected independently from the animation. The recorded
	// motion ends at a random cube orientation and the face labels are mapped
	// afterwards, so no result is favoured by the settling motion.
	m_Result = secure_rand_below(6) + 1;
	BuildRollTrajectory(Impulse);
	if(m_vRecordedFrames.empty())
		return false;
	// Start playback immediately so the face remapping is never shown for one
	// static frame before the die begins to move.
	m_Pos = m_vRecordedFrames.front().m_Pos;
	m_Orientation = m_vRecordedFrames.front().m_Orientation;
	m_ReplayFrame = 1;
	m_State = STATE_REPLAYING;
	m_HasPendingResult = true;
	return true;
}

bool CDice3D::CanSee(int SnappingClient)
{
	if(!IsFlightChessDice())
		return true;
	if(!GameServer()->IsFlightChessTeamActive(m_FlightChessTeam))
		return false;
	if(SnappingClient == SERVER_DEMO_CLIENT || SnappingClient < 0 || SnappingClient >= MAX_CLIENTS)
		return false;
	int ViewerTeam = GameServer()->GetDDRaceTeam(SnappingClient);
	CPlayer *pViewer = GameServer()->m_apPlayers[SnappingClient];
	if(pViewer && (pViewer->GetTeam() == TEAM_SPECTATORS || pViewer->IsPaused()) && pViewer->SpectatorId() >= 0 && pViewer->SpectatorId() < MAX_CLIENTS)
		ViewerTeam = GameServer()->GetDDRaceTeam(pViewer->SpectatorId());
	return ViewerTeam == m_FlightChessTeam;
}

bool CDice3D::TestPhysicsBox(vec2 Pos)
{
	const vec2 Size(DICE_BODY_SIZE, DICE_BODY_SIZE);
	if(Collision()->TestBox(Pos, Size))
		return true;

	// CCollision::MoveBox only treats solid/nohook tiles as walls. The die is a
	// physical board-game prop, so the all-direction stopper (tile 62) must act
	// as a wall as well. Scan every tile touched by the large 80x80 body instead
	// of only its four corners, otherwise an isolated stopper can sit completely
	// between the corner samples and be missed.
	const int Width = Collision()->GetWidth();
	const int Height = Collision()->GetHeight();
	if(Width <= 0 || Height <= 0)
		return false;

	const int MinX = std::clamp((int)std::floor((Pos.x - DICE_HALF_SIZE) / 32.0f), 0, Width - 1);
	const int MaxX = std::clamp((int)std::floor((Pos.x + DICE_HALF_SIZE) / 32.0f), 0, Width - 1);
	const int MinY = std::clamp((int)std::floor((Pos.y - DICE_HALF_SIZE) / 32.0f), 0, Height - 1);
	const int MaxY = std::clamp((int)std::floor((Pos.y + DICE_HALF_SIZE) / 32.0f), 0, Height - 1);
	for(int Y = MinY; Y <= MaxY; Y++)
	{
		for(int X = MinX; X <= MaxX; X++)
		{
			const int Index = Y * Width + X;
			if(Collision()->GetTileIndex(Index) == TILE_STOPA || Collision()->GetFrontTileIndex(Index) == TILE_STOPA)
				return true;
		}
	}
	return false;
}

void CDice3D::MovePhysicsBox(vec2 *pInoutPos, vec2 *pInoutVel, vec2 Elasticity, bool *pGrounded)
{
	vec2 Pos = *pInoutPos;
	vec2 Vel = *pInoutVel;
	const float Distance = length(Vel);
	const int MaxSteps = (int)Distance;

	if(Distance > 0.00001f)
	{
		const float Fraction = 1.0f / (MaxSteps + 1.0f);
		const float ElasticityX = std::clamp(Elasticity.x, -1.0f, 1.0f);
		const float ElasticityY = std::clamp(Elasticity.y, -1.0f, 1.0f);
		for(int Step = 0; Step <= MaxSteps; Step++)
		{
			if(Vel == vec2())
				break;
			vec2 NewPos = Pos + Vel * Fraction;
			if(NewPos == Pos)
				break;

			if(TestPhysicsBox(NewPos))
			{
				int Hits = 0;
				if(TestPhysicsBox(vec2(Pos.x, NewPos.y)))
				{
					if(pGrounded && ElasticityY > 0.0f && Vel.y > 0.0f)
						*pGrounded = true;
					NewPos.y = Pos.y;
					Vel.y *= -ElasticityY;
					Hits++;
				}
				if(TestPhysicsBox(vec2(NewPos.x, Pos.y)))
				{
					NewPos.x = Pos.x;
					Vel.x *= -ElasticityX;
					Hits++;
				}
				if(Hits == 0)
				{
					if(pGrounded && ElasticityY > 0.0f && Vel.y > 0.0f)
						*pGrounded = true;
					NewPos = Pos;
					Vel.x *= -ElasticityX;
					Vel.y *= -ElasticityY;
				}
			}
			Pos = NewPos;
		}
	}

	*pInoutPos = Pos;
	*pInoutVel = Vel;
}

void CDice3D::AssignFaceValues(int TargetFaceValue)
{
	// Rotate the labelled cube locally so the preselected result occupies the
	// face that the recorded blank cube naturally leaves towards the camera.
	// CanonicalResultOrientation consists only of legal 90-degree cube turns,
	// therefore adjacency and opposite-face relationships stay valid.
	const TQuaternion ResultToFront = CanonicalResultOrientation(m_Result);
	const TQuaternion TargetToFront = CanonicalResultOrientation(TargetFaceValue);
	const TQuaternion LabelRotation = QuaternionNormalize(QuaternionMultiply(QuaternionConjugate(TargetToFront), ResultToFront));

	m_aFaceValues.fill(0);
	for(int SourceFace = 0; SourceFace < (int)gs_aFaces.size(); SourceFace++)
	{
		const vec3 RotatedNormal = QuaternionRotate(LabelRotation, gs_aFaces[SourceFace].m_Normal);
		int DestinationFace = -1;
		float BestDot = -2.0f;
		for(int Face = 0; Face < (int)gs_aFaces.size(); Face++)
		{
			const float Dot = dot(RotatedNormal, gs_aFaces[Face].m_Normal);
			if(Dot > BestDot)
			{
				BestDot = Dot;
				DestinationFace = Face;
			}
		}
		if(DestinationFace >= 0)
			m_aFaceValues[DestinationFace] = gs_aFaces[SourceFace].m_Value;
	}
	for(int Face = 0; Face < (int)m_aFaceValues.size(); Face++)
	{
		if(m_aFaceValues[Face] == 0)
			m_aFaceValues[Face] = gs_aFaces[Face].m_Value;
	}
}

void CDice3D::BuildRollTrajectory(vec2 Impulse)
{
	struct STranslationFrame
	{
		vec2 m_Pos;
		bool m_Supported;
		float m_ImpactStrength;
	};

	m_vRecordedFrames.clear();
	std::vector<STranslationFrame> vTranslation;
	vTranslation.reserve(Server()->TickSpeed() * 4);

	vec2 SimPos = m_Pos;
	vec2 SimVel = Impulse;
	int StillTicks = 0;
	const int MinFrames = Server()->TickSpeed() * 3 / 2;
	const int MaxFrames = Server()->TickSpeed() * 4;
	for(int Frame = 0; Frame < MaxFrames; Frame++)
	{
		SimVel.y = minimum(SimVel.y + 0.35f, 18.0f);
		const float IncomingDownSpeed = SimVel.y;
		bool Grounded = false;
		MovePhysicsBox(&SimPos, &SimVel, vec2(0.42f, 0.42f), &Grounded);
		const bool Supported = Grounded || TestPhysicsBox(SimPos + vec2(0.0f, 2.0f));
		const float ImpactStrength = Grounded && IncomingDownSpeed > 1.0f ? std::clamp(IncomingDownSpeed / 12.0f, 0.0f, 1.0f) : 0.0f;
		SimVel.x *= Supported ? 0.88f : 0.995f;
		SimVel.y *= 0.995f;
		vTranslation.push_back({SimPos, Supported, ImpactStrength});

		if(Frame + 1 >= MinFrames && Supported && length(SimVel) < 0.32f)
			StillTicks++;
		else
			StillTicks = 0;
		if(StillTicks >= 8)
			break;
	}
	if(vTranslation.empty())
		return;

	float HorizontalDirection = Impulse.x;
	if(std::abs(HorizontalDirection) < 0.05f)
		HorizontalDirection = RandomSigned() < 0.0f ? -1.0f : 1.0f;
	vec3 HammerAxis(-Impulse.y, Impulse.x, std::copysign(maximum(std::abs(Impulse.x), std::abs(Impulse.y) * 0.45f), HorizontalDirection));
	if(length(HammerAxis) < 0.001f)
		HammerAxis = vec3(0.7f, 0.5f, 0.6f);
	HammerAxis = normalize(HammerAxis);

	size_t FirstSupported = vTranslation.size();
	for(size_t Frame = 0; Frame < vTranslation.size(); Frame++)
	{
		if(vTranslation[Frame].m_Supported)
		{
			FirstSupported = Frame;
			break;
		}
	}
	if(FirstSupported == vTranslation.size())
		FirstSupported = vTranslation.size() / 2;
	const size_t SettleStart = minimum(vTranslation.size() - 1, maximum(FirstSupported + (size_t)Server()->TickSpeed() / 3, vTranslation.size() * 3 / 5));

	auto NearestStableOrientation = [&](const TQuaternion &Orientation, TQuaternion &Target, int &TargetFaceValue) {
		float BestAlignment = -1.0f;
		for(int FaceValue = 1; FaceValue <= 6; FaceValue++)
		{
			for(int QuarterTurn = 0; QuarterTurn < 4; QuarterTurn++)
			{
				const TQuaternion ScreenRotation = QuaternionFromAxisAngle(vec3(0.0f, 0.0f, 1.0f), QuarterTurn * DICE_PI * 0.5f);
				TQuaternion Candidate = QuaternionNormalize(QuaternionMultiply(ScreenRotation, CanonicalResultOrientation(FaceValue)));
				const float Alignment = std::abs(QuaternionDot(Orientation, Candidate));
				if(Alignment > BestAlignment)
				{
					BestAlignment = Alignment;
					if(QuaternionDot(Orientation, Candidate) < 0.0f)
					{
						for(float &Value : Candidate)
							Value = -Value;
					}
					Target = Candidate;
					TargetFaceValue = FaceValue;
				}
			}
		}
	};

	auto RotationError = [](const TQuaternion &Orientation, const TQuaternion &Target, vec3 &Axis) {
		TQuaternion Error = QuaternionNormalize(QuaternionMultiply(Target, QuaternionConjugate(Orientation)));
		if(Error[0] < 0.0f)
		{
			for(float &Value : Error)
				Value = -Value;
		}
		const float Angle = 2.0f * std::acos(std::clamp(Error[0], -1.0f, 1.0f));
		Axis = vec3(Error[1], Error[2], Error[3]);
		if(length(Axis) > 0.0001f)
			Axis = normalize(Axis);
		else
			Axis = vec3();
		return Angle;
	};

	auto DominantFace = [](const TQuaternion &Orientation) {
		int BestFace = 0;
		float BestDepth = -2.0f;
		for(int Face = 0; Face < (int)gs_aFaces.size(); Face++)
		{
			const float Depth = QuaternionRotate(Orientation, gs_aFaces[Face].m_Normal).z;
			if(Depth > BestDepth)
			{
				BestDepth = Depth;
				BestFace = Face;
			}
		}
		return BestFace;
	};

	struct SGeneratedCandidate
	{
		std::vector<SPersistentFrame> m_vFrames;
		TQuaternion m_TargetOrientation{1.0f, 0.0f, 0.0f, 0.0f};
		int m_TargetFaceValue = 1;
		float m_Score;
		bool m_Valid;
	};

	SGeneratedCandidate SelectedCandidate{};
	SGeneratedCandidate BestFallback{};
	bool HasFallback = false;
	int ValidCandidates = 0;
	constexpr int CandidateAttempts = 8;
	for(int Attempt = 0; Attempt < CandidateAttempts; Attempt++)
	{
		SGeneratedCandidate Candidate{};
		Candidate.m_vFrames.reserve(vTranslation.size() + Server()->TickSpeed());

		vec3 RandomAxis(RandomSigned(), RandomSigned(), RandomSigned());
		if(length(RandomAxis) < 0.15f)
			RandomAxis = vec3(0.5f, -0.7f, 0.6f);
		RandomAxis = normalize(RandomAxis);
		vec3 InitialAxis = HammerAxis * 0.72f + RandomAxis * (0.48f + std::abs(RandomSigned()) * 0.28f);
		if(length(InitialAxis) < 0.15f)
			InitialAxis = HammerAxis;
		InitialAxis = normalize(InitialAxis);
		vec3 AngularVelocity = InitialAxis * (0.17f + std::abs(RandomSigned()) * 0.11f);
		TQuaternion Orientation = m_Orientation;
		TQuaternion TargetOrientation{};
		int TargetFaceValue = 1;
		bool HasTarget = false;
		float AxisChange = 0.0f;
		int FaceChanges = 0;
		int DistinctFaceMask = 0;
		int LongestFaceRun = 0;
		int CurrentFaceRun = 0;
		int PreviousFace = -1;
		int EvaluationFrames = 0;
		int StableTicks = 0;
		float FinalError = DICE_PI;
		const size_t FrameLimit = vTranslation.size() + Server()->TickSpeed();
		for(size_t Frame = 0; Frame < FrameLimit; Frame++)
		{
			const STranslationFrame &Translation = vTranslation[minimum(Frame, vTranslation.size() - 1)];
			if(Frame < SettleStart && Translation.m_ImpactStrength > 0.0f && length(AngularVelocity) > 0.001f)
			{
				const vec3 OldAxis = normalize(AngularVelocity);
				vec3 Perturbation(RandomSigned(), RandomSigned(), RandomSigned());
				Perturbation -= OldAxis * dot(Perturbation, OldAxis);
				if(length(Perturbation) < 0.05f)
					Perturbation = cross(OldAxis, std::abs(OldAxis.x) < 0.8f ? vec3(1, 0, 0) : vec3(0, 1, 0));
				Perturbation = normalize(Perturbation);
				AngularVelocity += Perturbation * (0.025f + Translation.m_ImpactStrength * 0.055f);
				const vec3 NewAxis = normalize(AngularVelocity);
				AxisChange += std::acos(std::clamp(dot(OldAxis, NewAxis), -1.0f, 1.0f));
			}

			if(!HasTarget && Frame >= SettleStart)
			{
				NearestStableOrientation(Orientation, TargetOrientation, TargetFaceValue);
				HasTarget = true;
			}

			if(HasTarget)
			{
				vec3 ErrorAxis;
				const float ErrorAngle = RotationError(Orientation, TargetOrientation, ErrorAxis);
				const float Torque = minimum(ErrorAngle * 0.026f, 0.036f);
				AngularVelocity += ErrorAxis * Torque;
				AngularVelocity *= Translation.m_Supported ? 0.84f : 0.90f;
			}
			else
			{
				AngularVelocity *= Translation.m_Supported ? 0.972f : 0.993f;
			}

			const float AngularSpeed = length(AngularVelocity);
			if(AngularSpeed > 0.00001f)
				Orientation = QuaternionNormalize(QuaternionMultiply(QuaternionFromAxisAngle(AngularVelocity, AngularSpeed), Orientation));
			Candidate.m_vFrames.push_back({Translation.m_Pos, Orientation});

			if(Frame < SettleStart)
			{
				const int Face = DominantFace(Orientation);
				DistinctFaceMask |= 1 << Face;
				if(Face != PreviousFace)
				{
					if(PreviousFace >= 0)
						FaceChanges++;
					PreviousFace = Face;
					CurrentFaceRun = 1;
				}
				else
					CurrentFaceRun++;
				LongestFaceRun = maximum(LongestFaceRun, CurrentFaceRun);
				EvaluationFrames++;
			}

			if(HasTarget && Frame + 1 >= vTranslation.size())
			{
				vec3 ErrorAxis;
				FinalError = RotationError(Orientation, TargetOrientation, ErrorAxis);
				if(FinalError < 0.008f && length(AngularVelocity) < 0.003f)
					StableTicks++;
				else
					StableTicks = 0;
				if(StableTicks >= 4)
					break;
			}
		}

		if(!HasTarget)
			NearestStableOrientation(Orientation, TargetOrientation, TargetFaceValue);
		vec3 FinalErrorAxis;
		FinalError = RotationError(Orientation, TargetOrientation, FinalErrorAxis);
		Candidate.m_TargetOrientation = TargetOrientation;
		Candidate.m_TargetFaceValue = TargetFaceValue;
		if(!Candidate.m_vFrames.empty())
			Candidate.m_vFrames.back().m_Orientation = TargetOrientation;

		int DistinctFaces = 0;
		for(int Face = 0; Face < 6; Face++)
			DistinctFaces += (DistinctFaceMask >> Face) & 1;
		const float LongestRunRatio = EvaluationFrames > 0 ? (float)LongestFaceRun / EvaluationFrames : 1.0f;
		const float LargestInitialComponent = maximum(std::abs(InitialAxis.x), maximum(std::abs(InitialAxis.y), std::abs(InitialAxis.z)));
		Candidate.m_Valid = DistinctFaces >= 3 && FaceChanges >= 3 && LongestRunRatio < 0.55f && LargestInitialComponent < 0.94f && AxisChange > 0.10f && FinalError < 0.035f;
		Candidate.m_Score = DistinctFaces * 4.0f + FaceChanges * 2.0f + AxisChange * 5.0f - LongestRunRatio * 8.0f - FinalError * 20.0f - maximum(LargestInitialComponent - 0.90f, 0.0f) * 20.0f;

		if(!HasFallback || Candidate.m_Score > BestFallback.m_Score)
		{
			BestFallback = Candidate;
			HasFallback = true;
		}
		if(Candidate.m_Valid)
		{
			ValidCandidates++;
			if(secure_rand_below(ValidCandidates) == 0)
				SelectedCandidate = Candidate;
		}
	}

	if(ValidCandidates == 0)
		SelectedCandidate = BestFallback;
	m_vRecordedFrames = std::move(SelectedCandidate.m_vFrames);
	AssignFaceValues(SelectedCandidate.m_TargetFaceValue);
}

void CDice3D::Tick()
{
	if(IsFlightChessDice() && !GameServer()->IsFlightChessTeamActive(m_FlightChessTeam))
	{
		Destroy();
		return;
	}
	if(m_State == STATE_RESTING)
		return;
	if(GameLayerClipped(m_Pos))
	{
		Destroy();
		return;
	}
	if(m_State == STATE_REPLAYING)
	{
		if(m_ReplayFrame < m_vRecordedFrames.size())
		{
			m_Pos = m_vRecordedFrames[m_ReplayFrame].m_Pos;
			m_Orientation = m_vRecordedFrames[m_ReplayFrame].m_Orientation;
			m_ReplayFrame++;
		}
		if(m_ReplayFrame >= m_vRecordedFrames.size())
		{
			m_vRecordedFrames.clear();
			m_ReplayFrame = 0;
			m_Vel = vec2();
			m_State = STATE_RESTING;
			if(m_HasPendingResult)
			{
				m_HasPendingResult = false;
				if(IsFlightChessDice())
					GameServer()->OnFlightChessDiceSettled(this);
			}
		}
		return;
	}

	m_Vel.y = minimum(m_Vel.y + 0.35f, 18.0f);
	bool Grounded = false;
	MovePhysicsBox(&m_Pos, &m_Vel, vec2(0.42f, 0.42f), &Grounded);
	const bool Supported = Grounded || TestPhysicsBox(m_Pos + vec2(0.0f, 2.0f));
	m_Vel.x *= Supported ? 0.88f : 0.995f;
	m_Vel.y *= 0.995f;

	if(m_State == STATE_FALLING)
	{
		if(Supported && std::abs(m_Vel.y) < 0.7f)
			m_StillTicks++;
		else
			m_StillTicks = 0;
		if(m_StillTicks >= 5)
		{
			m_Vel = vec2();
			m_State = STATE_RESTING;
		}
		return;
	}

}

void CDice3D::Snap(int SnappingClient)
{
	if(!CanSee(SnappingClient) || NetworkClipped(SnappingClient, m_Pos))
		return;
	const CSnapContext Context(GameServer()->GetClientVersion(SnappingClient), Server()->IsSixup(SnappingClient), SnappingClient);

	std::array<vec3, 8> aRotated{};
	std::array<vec2, 8> aProjected{};
	for(int i = 0; i < 8; i++)
	{
		aRotated[i] = QuaternionRotate(m_Orientation, gs_aVertices[i]);
		// Deliberately orthographic: z never affects the displayed size.
		aProjected[i] = m_Pos + vec2(aRotated[i].x, aRotated[i].y);
	}

	std::array<bool, 6> aVisible{};
	for(int Face = 0; Face < 6; Face++)
		aVisible[Face] = QuaternionRotate(m_Orientation, gs_aFaces[Face].m_Normal).z > 0.035f;

	std::array<bool, 8> aVisibleVertices{};
	for(int EdgeIndex = 0; EdgeIndex < (int)gs_aEdges.size(); EdgeIndex++)
	{
		const SEdge &Edge = gs_aEdges[EdgeIndex];
		if(!aVisible[Edge.m_FaceA] && !aVisible[Edge.m_FaceB])
			continue;
		aVisibleVertices[Edge.m_VertexA] = true;
		aVisibleVertices[Edge.m_VertexB] = true;
		const int SnapId = EdgeIndex == 0 ? GetId() : m_aSnapIds[EdgeIndex - 1];
		GameServer()->SnapLaserObject(Context, SnapId, aProjected[Edge.m_VertexB], aProjected[Edge.m_VertexA], -1, -1, LASERTYPE_DOOR);
	}
	// Door beams do not reliably display a cap on both ends. Snap one explicit
	// zero-length Door head on every vertex belonging to a rendered edge, so a
	// corner cannot disappear based on the direction of its adjacent beams.
	for(int Vertex = 0; Vertex < (int)aProjected.size(); Vertex++)
	{
		if(!aVisibleVertices[Vertex])
			continue;
		const vec2 Pos = aProjected[Vertex];
		GameServer()->SnapLaserObject(Context, m_aSnapIds[29 + Vertex], Pos, Pos + vec2(1.0f, 0.0f), -1, -1, LASERTYPE_DOOR);
	}

	constexpr float PipX = 17.0f;
	constexpr float PipY = 19.0f;
	const std::array<vec2, 4> Corners = {vec2(-PipX, -PipY), vec2(PipX, -PipY), vec2(-PipX, PipY), vec2(PipX, PipY)};
	const std::array<vec2, 6> Six = {vec2(-PipX, -PipY), vec2(PipX, -PipY), vec2(-PipX, 0), vec2(PipX, 0), vec2(-PipX, PipY), vec2(PipX, PipY)};
	int PipSnapIndex = 11;
	for(int FaceIndex = 0; FaceIndex < 6; FaceIndex++)
	{
		if(!aVisible[FaceIndex])
			continue;
		std::array<vec2, 6> aPips{};
		int NumPips = 0;
		auto AddPip = [&](vec2 Offset) { aPips[NumPips++] = Offset; };
		switch(m_aFaceValues[FaceIndex])
		{
		case 1: AddPip(vec2()); break;
		case 2: AddPip(Corners[0]); AddPip(Corners[3]); break;
		case 3: AddPip(Corners[0]); AddPip(vec2()); AddPip(Corners[3]); break;
		case 4: for(const vec2 Offset : Corners) AddPip(Offset); break;
		case 5: for(const vec2 Offset : Corners) AddPip(Offset); AddPip(vec2()); break;
		case 6: for(const vec2 Offset : Six) AddPip(Offset); break;
		}
		const SFace &Face = gs_aFaces[FaceIndex];
		for(int Pip = 0; Pip < NumPips && PipSnapIndex < 29; Pip++)
		{
			const vec3 Local = Face.m_Normal * (DICE_HALF_SIZE + 1.0f) + Face.m_Right * aPips[Pip].x + Face.m_Down * aPips[Pip].y;
			const vec3 Rotated = QuaternionRotate(m_Orientation, Local);
			const vec2 Pos = m_Pos + vec2(Rotated.x, Rotated.y);
			GameServer()->SnapLaserObject(Context, m_aSnapIds[PipSnapIndex++], Pos, Pos + vec2(1.0f, 0.0f), -1, -1, LASERTYPE_RIFLE);
		}
	}
}
