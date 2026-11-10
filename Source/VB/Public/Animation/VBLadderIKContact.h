#pragma once

#include "CoreMinimal.h"

// 사다리 지역 공간. 표시용 가로대와 같은 간격/범위로만 접촉점을 만든다.
struct FVBLadderIKGeometry
{
	FTransform ToWorld = FTransform::Identity;
	float Height = 0.0f;
	float Width = 0.0f;
	float Spacing = 0.0f;
	float Drop = 0.0f;
	float Depth = 0.0f;
	float Radius = 0.0f;

	FVector GetPoseDepthOffset(float AuthoredDepth, float BodyDepth) const
	{
		// 클립/캐릭터 거리는 월드 cm, 발판 깊이는 사다리 지역 cm다.
		return ToWorld.GetUnitAxis(EAxis::X) * (AuthoredDepth - BodyDepth)
			- ToWorld.TransformVector(FVector(Depth, 0.0f, 0.0f));
	}

	bool FindContact(const FVector& Raw, float Clearance, FVector& Out) const
	{
		if (Spacing <= KINDA_SMALL_NUMBER || Height <= 0.0f || Width <= 0.0f)
		{
			return false;
		}

		const int32 First = FMath::CeilToInt(Drop / Spacing);
		const int32 Last = FMath::FloorToInt((Height + Drop) / Spacing);
		if (First > Last)
		{
			return false;
		}

		const int32 Rung = FMath::Clamp(FMath::RoundToInt((Raw.Z - Clearance + Drop) / Spacing), First, Last);
		Out = FVector(-Depth, FMath::Clamp(Raw.Y, -Width * 0.5f, Width * 0.5f), Rung * Spacing - Drop + Clearance);
		return true;
	}
};

// 클립 배속의 출처. 접촉 문턱은 1배속 기준이라 배속 r 이면 속도는 r 배, 시간은 1/r 로 바꿔 비교한다.
enum class EVBLadderIKRateSource : uint8
{
	Clip, // 사다리 몽타주가 이 배속으로 돈다
	NoMontage, // 사다리 몽타주가 없다 - 월드 단위로 본다
	Stopped, // 몽타주가 멈췄다(일시정지, 배속 0, 구간 밖) - 월드 단위로 본다
	Invalid, // 배속이 수가 아니다 - 월드 단위로 보되 따로 알린다
};

struct FVBLadderIKClipRate
{
	// 이보다 느리면 멈춘 것으로 본다.
	static constexpr float MinClipRate = 0.01f;

	float Rate = 1.0f;
	EVBLadderIKRateSource Source = EVBLadderIKRateSource::NoMontage;

	// bPlaying 을 따로 받는 이유: Montage_Pause 는 배속을 그대로 두고 진행만 멈춘다.
	static FVBLadderIKClipRate FromEvaluated(bool bHasMontage, bool bPlaying, float EvaluatedRate)
	{
		FVBLadderIKClipRate Out;
		if (!bHasMontage)
		{
			return Out;
		}

		if (!FMath::IsFinite(EvaluatedRate))
		{
			Out.Source = EVBLadderIKRateSource::Invalid;
			return Out;
		}

		const bool bMoving = bPlaying && FMath::Abs(EvaluatedRate) > MinClipRate;
		Out.Source = bMoving ? EVBLadderIKRateSource::Clip : EVBLadderIKRateSource::Stopped;
		Out.Rate = bMoving ? FMath::Abs(EvaluatedRate) : 1.0f;
		return Out;
	}

	float Speed(float ClipSpeed) const { return ClipSpeed * Rate; }
	float Time(float ClipTime) const { return ClipTime / Rate; }

	const TCHAR* GetSourceName() const
	{
		switch (Source)
		{
		case EVBLadderIKRateSource::Clip: return TEXT("Clip");
		case EVBLadderIKRateSource::Stopped: return TEXT("Stopped");
		case EVBLadderIKRateSource::Invalid: return TEXT("Invalid");
		default: return TEXT("NoMontage");
		}
	}
};

// 노드의 지지 출처: 사다리 위(Ladder), 올라서기 중 마지막 지지 유지(Support), 남은 보정 줄이기(Fade), 꺼짐(Off).
// 출처가 끝나도 보정을 한 프레임에 지우지 않고 시간에 걸쳐 줄인다(발 튐 방지).
enum class EVBLadderIKMode : uint8
{
	Off,
	Ladder,
	Support,
	Fade,
};

struct FVBLadderIKModeRule
{
	// bOnLadder: 사다리 위. bSupportRequested: 올라서기 지지 요청. bSupportLadder: 그 사다리가 아직 있다.
	// bHasGeometry: 노드가 마지막으로 읽은 사다리 기하가 있다. bResidual: 아직 남은 접촉, 기본 보정이 있다.
	static EVBLadderIKMode Resolve(bool bOnLadder, bool bSupportRequested, bool bSupportLadder, bool bHasGeometry, bool bResidual)
	{
		if (bOnLadder)
		{
			return EVBLadderIKMode::Ladder;
		}
		// 사다리가 사라져도 요청이 살아 있으면 마지막 기하로 줄인다 - 새로 잡지 않으니 Support 가 아니라 Fade 다.
		if (bSupportRequested && bSupportLadder && bHasGeometry)
		{
			return EVBLadderIKMode::Support;
		}

		return bResidual && bHasGeometry ? EVBLadderIKMode::Fade : EVBLadderIKMode::Off;
	}
};

// 올라서기 동안 한 발의 기본 보정(발판 깊이) 가중치. 발이 떠날 때까지 붙들고, 떠나면 블렌드 시간에 걸쳐 줄인다.
struct FVBLadderIKSupportBase
{
	float Weight = 0.0f;
	bool bLifted = false;

	void Begin(float Current)
	{
		Weight = Current;
		bLifted = false;
	}

	// bLift: 잡고 있던 것이 풀렸거나 해제 속도 이상으로 움직였다. bEnding: 출처가 끝났다(남은 것을 줄이기만 한다).
	void Update(bool bLift, bool bEnding, float Dt, float BlendTime)
	{
		bLifted = bLifted || bLift || bEnding;
		if (bLifted)
		{
			Weight = FMath::FInterpConstantTo(Weight, 0.0f, Dt, 1.0f / FMath::Max(BlendTime, 0.001f));
		}
	}
};

// 올라서기로 넘어갈 때 한 손의 마지막 보정을 얼려 블렌드 시간에 걸쳐 줄인다. 새로 잡지 않는다.
struct FVBLadderIKHandHold
{
	FVector Frozen = FVector::ZeroVector;
	float Weight = 0.0f;

	void Begin(const FVector& LastRequested)
	{
		Frozen = LastRequested;
		Weight = 1.0f;
	}

	void Clear()
	{
		Frozen = FVector::ZeroVector;
		Weight = 0.0f;
	}

	// 이번 프레임 보정을 돌려주고 가중치를 줄인다. 첫 프레임은 얼린 값 그대로다.
	FVector Step(float Dt, float BlendTime)
	{
		const FVector Out = Frozen * Weight;
		Weight = FMath::FInterpConstantTo(Weight, 0.0f, Dt, 1.0f / FMath::Max(BlendTime, 0.001f));
		return Out;
	}

	bool IsActive() const { return Weight > SMALL_NUMBER && !Frozen.IsNearlyZero(); }
};

// 보정 전 포즈만 입력으로 받는다. IK 출력은 속도 판정/접촉점 선택에 되먹이지 않는다.
struct FVBLadderIKContact
{
	FVector PreviousRaw = FVector::ZeroVector;
	FVector Anchor = FVector::ZeroVector;
	FVector PlantRaw = FVector::ZeroVector;
	float Weight = 0.0f;
	float QuietTime = 0.0f;
	bool bPrimed = false;
	bool bPlanted = false;

	// bAllowPlant=false 면 새 가로대를 잡지 않고 잡은 것만 놓는다(올라서기, 페이드 중).
	void Update(const FVBLadderIKGeometry& Geometry, const FVector& Raw, float Dt,
		float Clearance, float PlantSpeed, float ReleaseSpeed, float BlendTime, float PlantDwellTime = 0.06f,
		float MaxPlantDistance = MAX_flt, bool bUseAxialMotion = false, float ReleaseTravel = 0.0f, bool bAllowPlant = true)
	{
		if (Dt <= SMALL_NUMBER)
		{
			return;
		}

		if (!bPrimed)
		{
			PreviousRaw = Raw;
			bPrimed = true;
			return; // 진입 블렌드의 첫 표본을 '정지'로 오인하지 않는다.
		}

		const FVector Velocity = Geometry.ToWorld.TransformVector(Raw - PreviousRaw) / Dt;
		const FVector Axis = Geometry.ToWorld.GetUnitAxis(EAxis::Z);
		// 손목의 앞뒤 정렬/회전은 봉을 떠나 다음 봉으로 이동하는 동작과 구분한다.
		const float Speed = bUseAxialMotion ? FMath::Abs(FVector::DotProduct(Velocity, Axis)) : Velocity.Size();
		PreviousRaw = Raw;
		FVector Candidate;
		const bool bNearRung = Geometry.FindContact(Raw, Clearance, Candidate)
			&& Geometry.ToWorld.TransformVector(Candidate - Raw).Size() <= FMath::Max(MaxPlantDistance, 0.0f);
		QuietTime = Speed <= PlantSpeed && bNearRung ? QuietTime + Dt : 0.0f;
		const float AxialTravel = FMath::Abs(FVector::DotProduct(Geometry.ToWorld.TransformVector(Raw - PlantRaw), Axis));
		const bool bLeaving = Speed > ReleaseSpeed && (!bUseAxialMotion || AxialTravel > ReleaseTravel);
		if (bPlanted && (bLeaving || FMath::Abs(Raw.Z - Anchor.Z) > Geometry.Spacing * 0.65f))
		{
			bPlanted = false;
		}
		// 놓는 동안 목표를 다른 가로대로 바꾸면 페이드 중에도 손발이 한 칸 튄다.
		// 스윙 꼭짓점에서 잠깐 느려진 것과 실제 붙잡기를 구별한다.
		if (bAllowPlant && !bPlanted && Weight <= SMALL_NUMBER && bNearRung && Speed <= PlantSpeed && QuietTime >= PlantDwellTime)
		{
			Anchor = Candidate;
			PlantRaw = Raw;
			bPlanted = true;
		}

		Weight = FMath::FInterpConstantTo(Weight, bPlanted ? 1.0f : 0.0f, Dt,
			1.0f / FMath::Max(BlendTime, 0.001f));
	}
};
