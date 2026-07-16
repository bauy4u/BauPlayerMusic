#ifndef GAME_SERVER_ENTITIES_DICE_3D_H
#define GAME_SERVER_ENTITIES_DICE_3D_H

#include <array>
#include <vector>

#include <game/server/entity.h>

class CDice3D : public CEntity
{
public:
	struct SPersistentFrame
	{
		vec2 m_Pos{};
		std::array<float, 4> m_Orientation{1.0f, 0.0f, 0.0f, 0.0f};
	};

	struct SPersistentState
	{
		bool m_Valid = false;
		vec2 m_Pos{};
		vec2 m_Vel{};
		std::array<float, 4> m_Orientation{1.0f, 0.0f, 0.0f, 0.0f};
		std::array<int, 6> m_aFaceValues{1, 2, 3, 4, 5, 6};
		std::vector<SPersistentFrame> m_vRecordedFrames;
		size_t m_ReplayFrame = 0;
		int m_State = 0;
		int m_Result = 1;
		int m_StillTicks = 0;
		bool m_HasPendingResult = false;
	};

	CDice3D(CGameWorld *pGameWorld, vec2 Pos, int FlightChessTeam = -1, int OwnerClientId = -1);
	~CDice3D() override;

	void Reset() override { Destroy(); }
	void Tick() override;
	void Snap(int SnappingClient) override;

	bool HammerHit(vec2 Impulse);
	bool IsRolling() const { return m_State != STATE_RESTING; }
	int Result() const { return m_Result; }
	bool IsFlightChessDice() const { return m_FlightChessTeam > 0; }
	int FlightChessTeam() const { return m_FlightChessTeam; }
	int OwnerClientId() const { return m_OwnerClientId; }
	void SetOwnerClientId(int ClientId) { m_OwnerClientId = ClientId; }
	void SavePersistentState(SPersistentState *pState) const;
	void LoadPersistentState(const SPersistentState &State);

private:
	enum EState
	{
	STATE_FALLING,
	STATE_RESTING,
	STATE_REPLAYING,
	};

	void BuildRollTrajectory(vec2 Impulse);
	void AssignFaceValues(int TargetFaceValue);
	bool CanSee(int SnappingClient);
	bool TestPhysicsBox(vec2 Pos);
	void MovePhysicsBox(vec2 *pInoutPos, vec2 *pInoutVel, vec2 Elasticity, bool *pGrounded = nullptr);

	vec2 m_Vel{};
	std::array<float, 4> m_Orientation{1.0f, 0.0f, 0.0f, 0.0f};
	std::array<int, 6> m_aFaceValues{1, 2, 3, 4, 5, 6};
	std::vector<SPersistentFrame> m_vRecordedFrames;
	size_t m_ReplayFrame = 0;
	std::array<int, 40> m_aSnapIds{};
	int m_State = STATE_FALLING;
	int m_Result = 1;
	int m_StillTicks = 0;
	int m_FlightChessTeam = -1;
	int m_OwnerClientId = -1;
	bool m_HasPendingResult = false;
};

#endif
