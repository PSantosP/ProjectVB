// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Data/VBAttackConfig.h"
#include "VBComboSelectionPolicy.generated.h"

// 고르는 데 쓰는 사실. 준비 단계가 세계를 읽어 채우고, 정책은 이것만 본다(다시 세계를 읽지 않는다).
struct FVBComboSelectContext
{
	bool bHasTarget = false;
	float DistanceCm = 0.0f; // 표적까지 수평 중심 거리
	bool bSprinting = false;
};

// 선택을 낸 규칙 분기. 규칙이 도는 자리에서 채워 개발 표시(UVBCombatReadoutSubsystem)가 실제 분기를 보여 준다.
enum class EVBComboChoiceReason : uint8
{
	NoCandidates,
	Sprint, // 질주 중이고 자격 있는 Sprint 변형이 있음
	Band, // 거리 띠 역할에 자격 있는 후보가 있음
	BandNeutral, // 띠 역할엔 없고 Neutral 에 있음
	NoTarget, // 표적 없음: 일반 공격
	NoneInReach, // 표적은 있으나 자격 후보 없음: 일반 공격, bTargetInReach=false
	Forced, // 개발 cvar vb.Combat.ForceCombo 가 고름(Manual 포함), 비출시 빌드 전용
};

// 선택 결과. bTargetInReach=false 면 자격 후보가 없어 일반 공격을 남긴 것이고, 첫 타를 표적까지 늘이지 않는다.
struct FVBComboChoice
{
	int32 Variant = 0;
	bool bTargetInReach = true;
	EVBComboChoiceReason Reason = EVBComboChoiceReason::NoCandidates;
	EVBComboRole Band = EVBComboRole::Neutral; // 표적 거리의 띠(표적이 있을 때만 의미)
	int32 Qualified = 0; // 고른 분기의 후보 수
	TOptional<EVBComboRole> RotationRole; // 순환 선택이 나온 역할 목록, 비면 순환 안 하는 분기
	int32 RotationAfter = INDEX_NONE; // 이 선택 직전의 마지막 시작(INDEX_NONE = 아직 없음)
};

// 콤보 시작 사이에 서버가 기억하는 것. 공격 어빌리티가 소유하고 실제로 실행된 시작만 기록한다.
// 변형 번호는 그 설정 안에서만 의미가 있어, 다른 설정의 기억은 빈 것으로 읽힌다.
struct VB_API FVBComboStartMemory
{
	// 종류 무관 마지막 시작(회피 후 재개가 이어 간다).
	int32 VariantFor(const UVBAttackConfig* InConfig) const;
	// Role 띠 목록(Close/Neutral/Far)의 마지막 시작. 다른 역할은 INDEX_NONE.
	int32 LastStartFor(const UVBAttackConfig* InConfig, EVBComboRole Role) const;
	// 시작 기록. 다른 설정의 시작이면 기존 기억을 모두 잊는다.
	void NoteStart(const UVBAttackConfig* InConfig, int32 Variant, EVBComboRole Role);

private:
	static int32 RotationSlot(EVBComboRole Role); // INDEX_NONE for Sprint/Manual

	TWeakObjectPtr<const UVBAttackConfig> Config;
	int32 LastVariant = INDEX_NONE;
	int32 LastByRole[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE};
};

/**
 * 새 콤보를 무엇으로 시작할지. 순서: 질주 Sprint -> 무표적 Neutral -> 거리 띠 역할 -> Neutral -> 일반 공격(bTargetInReach=false).
 * 띠 목록에 자격 후보가 여럿이면 마지막 시작 다음 것을 돌아가며 골라 모든 콤보가 나온다. Manual 은 여기서 고르지 않는다.
 */
UCLASS(EditInlineNew, DefaultToInstanced, CollapseCategories)
class VB_API UVBComboSelectionPolicy : public UObject
{
	GENERATED_BODY()

public:
	// 이 거리 이하면 Close 띠. 역할표 첫 적용값 - 변형별 자격(TargetReach)이 띠 안에서 다시 거른다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="0", Units="cm"))
	float CloseMaxDistance = 150.0f;

	// 이 거리 초과면 Far 띠.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="0", Units="cm"))
	float FarMinDistance = 280.0f;

	// Config = 후보가 나온 설정. Memory 는 그 설정 기준으로만 읽는다.
	FVBComboChoice Select(const FVBComboSelectContext& Context, TConstArrayView<FVBComboCandidate> Candidates,
		const FVBComboStartMemory& Memory, const UVBAttackConfig* Config) const;

	// 규칙의 홈. 시험이 직접 부른다.
	static FVBComboChoice SelectVariant(const FVBComboSelectContext& Context, TConstArrayView<FVBComboCandidate> Candidates,
		float CloseMax, float FarMin, const FVBComboStartMemory& Memory, const UVBAttackConfig* Config);
	static EVBComboRole BandRole(float DistanceCm, float CloseMax, float FarMin);

	// 개발용 강제 선택(vb.Combat.ForceCombo). 범위 안 번호면 선택을 바꾸고(Manual 포함) 강제 여부를 돌려준다. -1 = 끔.
	static bool ApplyForcedVariant(int32 ForcedVariant, int32 NumCandidates, bool bHasTarget, FVBComboChoice& InOutChoice);

	// Manual 행이 있을 때 행이 하나뿐인 순환 띠 목록. UVBAttackConfig::IsDataValid 가 보고한다.
	static TArray<EVBComboRole> FindLoneBandsBesideManual(TConstArrayView<FVBComboCandidate> Candidates);
};
